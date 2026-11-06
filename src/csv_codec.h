#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sc2csv {

// How a CSV sits in an asset folder. Newer packs ship the text as is, so a container is not the only form.
enum class CsvFormat {
    Unknown,
    Lzma,   // 9-byte Supercell header + raw LZMA1 stream (older packs, current CN build)
    Plain,  // the CSV text itself
};

// A Supercell CSV container: 9-byte header followed by a raw LZMA1 stream without an end marker.
struct CsvHeader {
    std::uint8_t properties = 0x5D;
    std::uint32_t dictionary_size = 0;
    // The unpacked size; 0xFFFFFFFF when the header does not carry it.
    std::uint32_t unpacked_size = 0;
};

CsvFormat DetectCsvFormat(std::span<const std::uint8_t> data);

// Reads the container header. Fails when the data is not an LZMA container or the sizes are implausible.
bool ReadCsvHeader(std::span<const std::uint8_t> data, CsvHeader& header);

struct DecodeResult {
    bool ok = false;
    CsvFormat format = CsvFormat::Unknown;
    std::string text;
    std::string error;
    std::size_t decoded_size = 0;
};

// Decodes a container; a plaintext CSV comes back untouched with format Plain.
DecodeResult DecodeCsvData(std::span<const std::uint8_t> data);

// The unpacked size a container announces, or 0 when it does not say (or the data is plain text).
std::uint64_t CsvUnpackedSize(std::span<const std::uint8_t> data);

struct EncodeResult {
    bool ok = false;
    std::vector<std::uint8_t> bytes;
    std::string error;
};

// Packs CSV text into a Supercell container the game reads: props 0x5D, 256 KiB dictionary, u32 size.
EncodeResult EncodeCsvData(std::string_view text);

// The three-line Supercell layout: a header row, a type row, then the data rows.
struct CsvTable {
    std::vector<std::string> headers;
    std::vector<std::string> types;
    std::vector<std::vector<std::string>> rows;
};

// Parses UTF-8 CSV text with quoted fields and doubled quotes. Only the first max_rows rows are kept, while
// *total_rows still counts all of them.
bool ParseCsv(std::string_view text, CsvTable& table, std::size_t max_rows = static_cast<std::size_t>(-1),
              std::size_t* total_rows = nullptr);

// Writes a table back as three-line CSV with every field quoted, as the game ships them.
std::string WriteCsv(const CsvTable& table);

} // namespace sc2csv
