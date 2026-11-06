#include "csv_codec.h"

#include "LzmaLib.h"
#include "error_text.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace sc2csv {
namespace {

constexpr std::uint32_t kMaxUnpackedSize = 512u * 1024u * 1024u;
constexpr std::uint32_t kUnknownSize = 0xFFFFFFFFu;
constexpr std::uint32_t kDefaultDictionary = 256u * 1024u;
constexpr std::uint8_t kDefaultProperties = 0x5D;

bool ValidProperties(std::uint8_t properties) {
    const int lc = properties % 9;
    const int rest = properties / 9;
    const int lp = rest % 5;
    const int pb = rest / 5;
    return lc <= 8 && lp <= 4 && pb <= 4 && lc + lp <= 4;
}

std::uint32_t ReadLe32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}

bool LooksLikeText(std::span<const std::uint8_t> data) {
    if (data.empty())
        return false;
    // A CSV starts with a quoted or bare field name; a BOM may precede it.
    std::size_t i = 0;
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        i = 3;
    if (i >= data.size())
        return false;
    const std::uint8_t first = data[i];
    if (first == '"' || first == ',' || first == '\'' || first == '\n' || first == '\r')
        return true;
    if ((first >= '0' && first <= '9') || (first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') ||
        first == '_')
        return true;
    return false;
}

void AppendQuoted(std::string& out, const std::string& field) {
    out.push_back('"');
    for (const char ch : field) {
        if (ch == '"')
            out.push_back('"');
        out.push_back(ch);
    }
    out.push_back('"');
}

void AppendRow(std::string& out, const std::vector<std::string>& row) {
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (i > 0)
            out.push_back(',');
        AppendQuoted(out, row[i]);
    }
    out.push_back('\n');
}

} // namespace

CsvFormat DetectCsvFormat(std::span<const std::uint8_t> data) {
    CsvHeader header;
    if (ReadCsvHeader(data, header))
        return CsvFormat::Lzma;
    if (LooksLikeText(data))
        return CsvFormat::Plain;
    return CsvFormat::Unknown;
}

bool ReadCsvHeader(std::span<const std::uint8_t> data, CsvHeader& header) {
    if (data.size() < 9)
        return false;
    if (!ValidProperties(data[0]))
        return false;
    // A text file's first bytes almost never form valid LZMA properties; the dictionary check rules out the rest.
    const std::uint32_t dictionary = ReadLe32(data.data() + 1);
    if (dictionary < 4096 || dictionary > (1u << 30))
        return false;
    const std::uint32_t unpacked = ReadLe32(data.data() + 5);
    if (unpacked != kUnknownSize && unpacked > kMaxUnpackedSize)
        return false;
    if (unpacked != kUnknownSize && unpacked == 0)
        return false;
    header.properties = data[0];
    header.dictionary_size = dictionary;
    header.unpacked_size = unpacked;
    return true;
}

std::uint64_t CsvUnpackedSize(std::span<const std::uint8_t> data) {
    CsvHeader header;
    if (!ReadCsvHeader(data, header))
        return 0;
    if (header.unpacked_size == kUnknownSize)
        return 0;
    return header.unpacked_size;
}

DecodeResult DecodeCsvData(std::span<const std::uint8_t> data) {
    DecodeResult result;
    const CsvFormat format = DetectCsvFormat(data);
    result.format = format;
    if (format == CsvFormat::Plain) {
        result.ok = true;
        result.text.assign(reinterpret_cast<const char*>(data.data()), data.size());
        result.decoded_size = data.size();
        return result;
    }
    if (format != CsvFormat::Lzma) {
        result.error = ErrorText("error.unknown_format", "无法识别的 CSV 格式");
        return result;
    }

    CsvHeader header;
    ReadCsvHeader(data, header);
    const bool known_size = header.unpacked_size != kUnknownSize;
    // An unknown size still needs a buffer: the game never writes one, but other packs might.
    const std::uint64_t guessed =
        known_size ? header.unpacked_size
                   : std::max<std::uint64_t>(std::uint64_t(data.size()) * 32u, std::uint64_t(1) << 20);
    const std::uint32_t guess = std::uint32_t(std::min<std::uint64_t>(guessed, kMaxUnpackedSize));

    // LzmaDecode with LZMA_FINISH_ANY stops when the output buffer is full and still reports success, so a full
    // buffer means "try a larger one". Without a declared size the stream also has no EOS marker: a complete
    // decode ends with SZ_ERROR_INPUT_EOF once the input is used up, and the bytes written are all there is.
    std::string output;
    std::size_t dest_length = 0;
    std::uint32_t buffer_size = guess;
    SRes code = SZ_ERROR_OUTPUT_EOF;
    const std::uint8_t* payload = data.data() + 9;
    for (;;) {
        output.assign(buffer_size, '\0');
        dest_length = SizeT(buffer_size);
        SizeT source_length = SizeT(data.size() - 9);
        code = LzmaUncompress(reinterpret_cast<Byte*>(output.data()), &dest_length, payload, &source_length,
                              data.data(), 5);
        if (known_size)
            break;
        const bool retryable = code == SZ_OK || code == SZ_ERROR_OUTPUT_EOF || code == SZ_ERROR_INPUT_EOF;
        if (!retryable || dest_length != SizeT(buffer_size) || buffer_size >= kMaxUnpackedSize)
            break;
        buffer_size = std::min<std::uint32_t>(buffer_size * 2u, kMaxUnpackedSize);
    }
    if (!known_size && dest_length == SizeT(buffer_size)) {
        result.error = ErrorText("error.unpacked_too_large", "解压数据超过上限（512 MiB）");
        return result;
    }
    if (code != SZ_OK && !(!known_size && code == SZ_ERROR_INPUT_EOF)) {
        result.error = ErrorFormat("error.lzma_decode_failed", "LZMA 解码失败（错误码 %d）", int(code));
        return result;
    }
    if (known_size && dest_length != SizeT(header.unpacked_size)) {
        result.error = ErrorFormat("error.unpacked_size_mismatch", "解压长度不符：期望 %zu 字节，实际 %zu 字节",
                                   std::size_t(header.unpacked_size), std::size_t(dest_length));
        return result;
    }
    if (dest_length == 0) {
        result.error = ErrorText("error.unpacked_empty", "解压结果为空");
        return result;
    }
    output.resize(std::size_t(dest_length));
    result.ok = true;
    result.text = std::move(output);
    result.decoded_size = std::size_t(dest_length);
    return result;
}

EncodeResult EncodeCsvData(std::string_view text) {
    EncodeResult result;
    if (text.empty()) {
        result.error = ErrorText("error.empty_csv", "CSV 内容为空");
        return result;
    }
    // The container header stores the unpacked size as u32; a larger CSV cannot be described by it.
    if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.error = ErrorText("error.csv_too_large", "CSV 过大，无法打包（超过 4 GiB）");
        return result;
    }

    std::size_t capacity = text.size() + text.size() / 2 + 1024;
    std::vector<std::uint8_t> compressed(capacity);
    std::uint8_t properties[8] = {};
    std::size_t properties_size = sizeof(properties);
    std::size_t compressed_size = capacity;

    // level 5 is the SDK's default; fb 32 is what the game's own files look like.
    const SRes code = LzmaCompress(compressed.data(), &compressed_size,
                                   reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), properties,
                                   &properties_size, 5, kDefaultDictionary, 3, 0, 2, 32, 1);
    if (code != SZ_OK) {
        result.error = ErrorFormat("error.lzma_encode_failed", "LZMA 压缩失败（错误码 %d）", int(code));
        return result;
    }

    compressed.resize(compressed_size);
    result.bytes.reserve(9 + compressed_size);
    if (properties_size >= 5) {
        result.bytes.insert(result.bytes.end(), properties, properties + 5);
    } else {
        // Fall back to the fixed properties the game uses.
        result.bytes.push_back(kDefaultProperties);
        for (int i = 0; i < 4; ++i)
            result.bytes.push_back(std::uint8_t((kDefaultDictionary >> (8 * i)) & 0xFF));
    }
    const std::uint32_t size = std::uint32_t(text.size());
    for (int i = 0; i < 4; ++i)
        result.bytes.push_back(std::uint8_t((size >> (8 * i)) & 0xFF));
    result.bytes.insert(result.bytes.end(), compressed.begin(), compressed.end());
    result.ok = true;
    return result;
}

bool ParseCsv(std::string_view text, CsvTable& table, std::size_t max_rows, std::size_t* total_rows) {
    table = CsvTable{};
    if (total_rows)
        *total_rows = 0;

    // A UTF-8 BOM is not part of the first header's name.
    if (text.size() >= 3 && std::uint8_t(text[0]) == 0xEF && std::uint8_t(text[1]) == 0xBB &&
        std::uint8_t(text[2]) == 0xBF)
        text.remove_prefix(3);

    std::vector<std::string> record;
    std::string field;
    bool quoted = false;
    std::size_t row_index = 0;
    std::size_t data_rows = 0;

    const auto end_record = [&] {
        record.push_back(std::move(field));
        field.clear();
        if (record.size() == 1 && record[0].empty()) {
            record.clear();
            return;
        }
        if (row_index == 0)
            table.headers = record;
        else if (row_index == 1)
            table.types = record;
        else {
            ++data_rows;
            if (table.rows.size() < max_rows)
                table.rows.push_back(record);
        }
        ++row_index;
        record.clear();
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(ch);
            }
        } else if (ch == '"') {
            quoted = true;
        } else if (ch == ',') {
            record.push_back(std::move(field));
            field.clear();
        } else if (ch == '\n') {
            end_record();
        } else if (ch != '\r') {
            field.push_back(ch);
        }
    }
    if (!field.empty() || !record.empty() || quoted)
        end_record();

    if (total_rows)
        *total_rows = data_rows;
    return !table.headers.empty();
}

std::string WriteCsv(const CsvTable& table) {
    std::string out;
    out.reserve(table.rows.size() * 64);
    if (!table.headers.empty())
        AppendRow(out, table.headers);
    if (!table.types.empty())
        AppendRow(out, table.types);
    for (const std::vector<std::string>& row : table.rows)
        AppendRow(out, row);
    return out;
}

} // namespace sc2csv
