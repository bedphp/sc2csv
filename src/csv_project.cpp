#include "csv_project.h"

#include "csv_codec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace sc2csv {
namespace {

#ifdef _WIN32
std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring wide(std::size_t(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), length);
    return wide;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(std::size_t(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}
#endif

bool HasCsvExtension(const std::filesystem::path& path) {
    std::string extension = PathToUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return char(std::tolower(ch)); });
    return extension == ".csv";
}

int FolderRank(std::string_view folder) {
    if (folder == "csv_logic") return 0;
    if (folder == "csv_client") return 1;
    if (folder == "localization") return 2;
    return 3;
}

void AddFile(const std::filesystem::path& path, const std::string& folder, const std::string& relative,
             std::vector<CsvFile>& files) {
    std::error_code size_error;
    const std::uintmax_t size = std::filesystem::file_size(path, size_error);
    if (size_error)
        return;

    CsvFile file;
    file.name = PathToUtf8(path.filename());
    file.folder = folder;
    file.relative = relative;
    file.path = path;
    file.compressed = size;

    // The header carries the exact unpacked size, so the table can show it before any decoding. A plaintext CSV
    // is its own decoded size.
    std::ifstream input(path, std::ios::binary);
    std::array<std::uint8_t, 9> head{};
    input.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()));
    const std::span<const std::uint8_t> head_span(head.data(), std::size_t(input.gcount()));
    file.format = DetectCsvFormat(head_span);
    if (file.format == CsvFormat::Plain) {
        file.decoded = size;
    } else if (file.format == CsvFormat::Lzma) {
        CsvHeader header;
        if (ReadCsvHeader(head_span, header) && header.unpacked_size != 0xFFFFFFFFu)
            file.decoded = header.unpacked_size;
    }

    files.push_back(std::move(file));
}

std::string RelativeText(const std::filesystem::path& relative) {
    std::string text = PathToUtf8(relative);
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

} // namespace

std::filesystem::path PathFromUtf8(const std::string& text) {
#ifdef _WIN32
    return std::filesystem::path(Utf8ToWide(text));
#else
    return std::filesystem::path(text);
#endif
}

std::string PathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    return WideToUtf8(path.wstring());
#else
    return path.string();
#endif
}

std::string FormatBytes(std::uint64_t bytes) {
    char buffer[64];
    if (bytes < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024ull * 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", double(bytes) / 1024.0);
    } else if (bytes < 1024ull * 1024ull * 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB", double(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
    return buffer;
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::string& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "无法打开文件";
        return false;
    }
    const std::streamsize size = input.tellg();
    if (size < 0) {
        error = "无法读取文件大小";
        return false;
    }
    input.seekg(0, std::ios::beg);
    bytes.resize(std::size_t(size));
    if (size > 0 && !input.read(reinterpret_cast<char*>(bytes.data()), size)) {
        error = "读取文件失败";
        return false;
    }
    return true;
}

bool WriteFileBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "无法写入输出文件";
        return false;
    }
    if (!bytes.empty())
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!out) {
        error = "写入输出文件失败";
        return false;
    }
    return true;
}

bool ScanCsvFiles(const std::filesystem::path& input, std::vector<CsvFile>& files, std::string& error) {
    files.clear();
    std::error_code ec;
    if (!std::filesystem::exists(input, ec) || !std::filesystem::is_directory(input, ec)) {
        error = "输入目录不存在";
        return false;
    }

    static const char* const kKnownFolders[] = {"csv_logic", "csv_client", "localization"};
    bool found_known = false;
    for (const char* folder : kKnownFolders) {
        const std::filesystem::path directory = input / folder;
        if (!std::filesystem::is_directory(directory, ec))
            continue;
        found_known = true;
        const std::filesystem::directory_options options =
            std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::recursive_directory_iterator it(directory, options, ec), end; it != end;
             it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!it->is_regular_file(ec) || !HasCsvExtension(it->path()))
                continue;
            const std::filesystem::path relative = folder / std::filesystem::relative(it->path(), directory, ec);
            if (ec) {
                ec.clear();
                continue;
            }
            AddFile(it->path(), folder, RelativeText(relative), files);
        }
    }

    if (!found_known) {
        const std::filesystem::directory_options options =
            std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::recursive_directory_iterator it(input, options, ec), end; it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            if (!it->is_regular_file(ec) || !HasCsvExtension(it->path()))
                continue;
            const std::filesystem::path relative = std::filesystem::relative(it->path(), input, ec);
            if (ec) {
                ec.clear();
                continue;
            }
            const std::string parent = PathToUtf8(it->path().parent_path().filename());
            AddFile(it->path(), parent.empty() ? "other" : parent, RelativeText(relative), files);
        }
    }

    std::sort(files.begin(), files.end(), [](const CsvFile& a, const CsvFile& b) {
        const int rank_a = FolderRank(a.folder);
        const int rank_b = FolderRank(b.folder);
        if (rank_a != rank_b)
            return rank_a < rank_b;
        if (a.folder != b.folder)
            return a.folder < b.folder;
        return a.relative < b.relative;
    });
    error.clear();
    return true;
}

std::filesystem::path OutputPathFor(const CsvFile& file, const std::filesystem::path& output, bool keep_structure) {
    if (keep_structure)
        return output / PathFromUtf8(file.relative);
    return output / PathFromUtf8(file.name);
}

bool ConvertCsvBytes(std::span<const std::uint8_t> input, OutputFormat format, std::vector<std::uint8_t>& output,
                     std::string& error, std::uint64_t* decoded_size) {
    // Reading the text is the same both ways: decode whatever form the file is in.
    DecodeResult decoded = DecodeCsvData(input);
    if (!decoded.ok) {
        error = decoded.error;
        return false;
    }
    if (decoded_size)
        *decoded_size = decoded.decoded_size;
    if (format == OutputFormat::Text) {
        output.assign(decoded.text.begin(), decoded.text.end());
        return true;
    }
    EncodeResult encoded = EncodeCsvData(decoded.text);
    if (!encoded.ok) {
        error = encoded.error;
        return false;
    }
    output = std::move(encoded.bytes);
    return true;
}

std::filesystem::path DestinationFor(const CsvFile& file, const std::filesystem::path& output,
                                     const ConvertOptions& options) {
    if (options.in_place)
        return file.path;
    return OutputPathFor(file, output, options.keep_structure);
}

DecodeOutcome ConvertCsvFile(const CsvFile& file, const std::filesystem::path& output, const ConvertOptions& options) {
    DecodeOutcome outcome;
    const std::filesystem::path destination = DestinationFor(file, output, options);

    std::error_code ec;
    // In place the file is always rewritten: the source it would skip over is the input itself.
    if (!options.in_place && !options.overwrite && std::filesystem::exists(destination, ec)) {
        outcome.status = FileStatus::Skipped;
        outcome.error = "输出已存在，已跳过";
        return outcome;
    }

    std::vector<std::uint8_t> bytes;
    if (!ReadFileBytes(file.path, bytes, outcome.error))
        return outcome;

    std::vector<std::uint8_t> converted;
    std::string conversion_error;
    std::uint64_t decoded_size = 0;
    if (!ConvertCsvBytes(bytes, options.format, converted, conversion_error, &decoded_size)) {
        outcome.error = conversion_error;
        return outcome;
    }

    const std::filesystem::path parent = destination.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            outcome.error = "无法创建输出目录";
            return outcome;
        }
    }
    if (!WriteFileBytes(destination, converted, outcome.error))
        return outcome;

    // The size the CSV text takes, whichever form was written.
    outcome.status = FileStatus::Done;
    outcome.decoded = decoded_size;
    return outcome;
}

} // namespace sc2csv
