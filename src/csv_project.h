#pragma once

#include "csv_codec.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sc2csv {

// What a run writes: the CSV text, or the packed container.
enum class OutputFormat {
    Text,
    Container,
};

enum class FileStatus {
    Pending,
    Running,
    Done,
    Failed,
    Skipped,
};

struct CsvFile {
    std::string name;
    std::string folder;    // csv_logic / csv_client / localization / other
    std::string relative;  // path relative to the input root, with '/' separators
    std::filesystem::path path;
    CsvFormat format = CsvFormat::Unknown;
    std::uint64_t compressed = 0;
    std::uint64_t decoded = 0;
    FileStatus status = FileStatus::Pending;
    std::string error;
};

// Finds the .csv files under input, reading each one's header so the decoded size is known before decoding.
bool ScanCsvFiles(const std::filesystem::path& input, std::vector<CsvFile>& files, std::string& error);

std::filesystem::path OutputPathFor(const CsvFile& file, const std::filesystem::path& output, bool keep_structure);

struct DecodeOutcome {
    FileStatus status = FileStatus::Failed;
    std::string error;
    std::uint64_t decoded = 0;
};

// How a run writes its files.
struct ConvertOptions {
    // Keep the input's subfolders under the output folder (always on in the window; the CLI's --flat turns it off).
    bool keep_structure = true;
    // Overwrite an output that already exists; otherwise such a file is skipped.
    bool overwrite = false;
    // Write back over the source file instead of into the output folder: unpacking replaces a container with the text,
    // packing replaces the text with a container, and no output folder is involved.
    bool in_place = false;
    OutputFormat format = OutputFormat::Text;
};

// Where one file goes with these options; the source itself when overwriting in place.
std::filesystem::path DestinationFor(const CsvFile& file, const std::filesystem::path& output,
                                     const ConvertOptions& options);

// Writes one file as text or as a container. An existing destination is skipped unless overwrite (or in_place) is set.
DecodeOutcome ConvertCsvFile(const CsvFile& file, const std::filesystem::path& output, const ConvertOptions& options);

// Converts bytes between the two forms in memory, for a preview. *decoded_size receives the size of the CSV text
// after decoding when it is not null.
bool ConvertCsvBytes(std::span<const std::uint8_t> input, OutputFormat format, std::vector<std::uint8_t>& output,
                     std::string& error, std::uint64_t* decoded_size = nullptr);

bool ReadFileBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::string& error);
bool WriteFileBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes, std::string& error);

std::string FormatBytes(std::uint64_t bytes);

// UTF-8 text (what the text fields hold) to a filesystem path and back.
std::filesystem::path PathFromUtf8(const std::string& text);
std::string PathToUtf8(const std::filesystem::path& path);

} // namespace sc2csv
