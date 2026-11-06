// sc2csv-cli — console version of sc2csv.
//
// It shares the conversion core (sc_csv) with the windowed program, so both directions, skip/overwrite semantics and
// the in-place mode behave the same way. The only differences are how the input is described and how the progress is
// printed.

#include "csv_project.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef SC2CSV_VERSION
#define SC2CSV_VERSION "0.1.0"
#endif

namespace {

using sc2csv::ConvertOptions;
using sc2csv::CsvFile;
using sc2csv::DecodeOutcome;
using sc2csv::FileStatus;
using sc2csv::OutputFormat;

struct Options {
    bool decode = false;
    bool encode = false;
    bool overwrite = false;
    bool flat = false;
    bool in_place = false;
    bool quiet = false;
    std::filesystem::path input;
    std::filesystem::path output;
};

void PrintUsage(std::FILE* out) {
    std::fprintf(out,
                 "sc2csv-cli %s - Brawl Stars CSV unpacker / packer\n"
                 "\n"
                 "Usage:\n"
                 "  sc2csv-cli --decode <input> <output> [options]\n"
                 "  sc2csv-cli --encode <input> <output> [options]\n"
                 "  sc2csv-cli --decode <input> --in-place [options]\n"
                 "  sc2csv-cli --encode <input> --in-place [options]\n"
                 "\n"
                 "<input> is a directory: its csv_logic, csv_client and localization folders are scanned, or every\n"
                 ".csv below it when those folders are absent. A single .csv file is accepted too.\n"
                 "\n"
                 "Options:\n"
                 "  --decode        unpack containers to plain CSV\n"
                 "  --encode        pack plain CSV into containers\n"
                 "  --overwrite     overwrite existing output files (default: skip them)\n"
                 "  --flat          do not keep the input's subdirectories under <output>\n"
                 "  --in-place      write back over the input files; no <output> is needed\n"
                 "  -q, --quiet     print only failures and the summary\n"
                 "  -h, --help      show this help\n"
                 "  --version       show the version\n"
                 "\n"
                 "Exit status:\n"
                 "  0  all files converted or skipped\n"
                 "  1  at least one file failed\n"
                 "  2  invalid arguments, or the input could not be scanned\n",
                 SC2CSV_VERSION);
}

bool HasCsvExtension(const std::filesystem::path& path) {
    std::string extension = sc2csv::PathToUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return char(std::tolower(ch)); });
    return extension == ".csv";
}

// A single file is described as if it were the only entry a scan would have found, so ConvertCsvFile can handle it
// like any other.
bool CollectFiles(const std::filesystem::path& input, std::vector<CsvFile>& files, std::string& error) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(input, ec)) {
        if (!HasCsvExtension(input)) {
            error = "input is not a .csv file: " + sc2csv::PathToUtf8(input);
            return false;
        }
        CsvFile file;
        file.name = sc2csv::PathToUtf8(input.filename());
        file.folder = "input";
        file.relative = file.name;
        file.path = input;
        file.compressed = std::filesystem::file_size(input, ec);
        files.push_back(std::move(file));
        error.clear();
        return true;
    }
    return sc2csv::ScanCsvFiles(input, files, error);
}

bool Parse(const std::vector<std::string>& args, Options& options, bool& help, bool& version, std::string& error) {
    std::vector<std::string> positional;
    for (const std::string& argument : args) {
        if (argument == "-h" || argument == "--help") {
            help = true;
        } else if (argument == "--version") {
            version = true;
        } else if (argument == "--decode") {
            options.decode = true;
        } else if (argument == "--encode") {
            options.encode = true;
        } else if (argument == "--overwrite") {
            options.overwrite = true;
        } else if (argument == "--flat") {
            options.flat = true;
        } else if (argument == "--in-place") {
            options.in_place = true;
        } else if (argument == "-q" || argument == "--quiet") {
            options.quiet = true;
        } else if (!argument.empty() && argument.front() == '-') {
            error = "unknown option: " + argument;
            return false;
        } else {
            positional.push_back(argument);
        }
    }

    if (help || version)
        return true;

    if (options.decode == options.encode) {
        error = "specify exactly one of --decode or --encode";
        return false;
    }
    if (positional.empty()) {
        error = "missing <input>";
        return false;
    }
    options.input = sc2csv::PathFromUtf8(positional[0]);

    if (options.in_place) {
        if (positional.size() > 1) {
            error = "--in-place takes only <input>";
            return false;
        }
    } else {
        if (positional.size() < 2) {
            error = "missing <output> (or use --in-place)";
            return false;
        }
        if (positional.size() > 2) {
            error = "too many arguments: " + positional[2];
            return false;
        }
        options.output = sc2csv::PathFromUtf8(positional[1]);
    }

    error.clear();
    return true;
}

int Run(const Options& options) {
    std::vector<CsvFile> files;
    std::string error;
    if (!CollectFiles(options.input, files, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }
    if (files.empty()) {
        std::fprintf(stderr, "error: no .csv files found under %s\n", sc2csv::PathToUtf8(options.input).c_str());
        return 2;
    }

    ConvertOptions convert;
    convert.overwrite = options.overwrite;
    convert.keep_structure = !options.flat;
    convert.in_place = options.in_place;
    convert.format = options.decode ? OutputFormat::Text : OutputFormat::Container;

    if (!options.quiet) {
        const std::string output =
            options.in_place ? "in place" : sc2csv::PathToUtf8(options.output);
        std::printf("%s: %s -> %s (%zu file(s))\n", options.decode ? "unpack" : "pack",
                    sc2csv::PathToUtf8(options.input).c_str(), output.c_str(), files.size());
    }

    const auto started = std::chrono::steady_clock::now();
    int converted = 0;
    int skipped = 0;
    int failed = 0;
    std::size_t index = 0;
    for (const CsvFile& file : files) {
        ++index;
        const DecodeOutcome outcome = sc2csv::ConvertCsvFile(file, options.output, convert);
        if (outcome.status == FileStatus::Done) {
            ++converted;
            if (!options.quiet)
                std::printf("[%zu/%zu] ok    %s\n", index, files.size(), file.relative.c_str());
        } else if (outcome.status == FileStatus::Skipped) {
            ++skipped;
            if (!options.quiet)
                std::printf("[%zu/%zu] skip  %s\n", index, files.size(), file.relative.c_str());
        } else {
            ++failed;
            std::fprintf(stderr, "[%zu/%zu] fail  %s: %s\n", index, files.size(), file.relative.c_str(),
                         outcome.error.c_str());
        }
    }

    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("%s: %zu file(s), %d converted, %d skipped, %d failed, %.2f s\n",
                options.decode ? "unpack" : "pack", files.size(), converted, skipped, failed, seconds);
    return failed == 0 ? 0 : 1;
}

int RunMain(const std::vector<std::string>& args) {
    Options options;
    bool help = false;
    bool version = false;
    std::string error;
    if (!Parse(args, options, help, version, error)) {
        std::fprintf(stderr, "sc2csv-cli: %s\n\n", error.c_str());
        PrintUsage(stderr);
        return 2;
    }
    if (help) {
        PrintUsage(stdout);
        return 0;
    }
    if (version) {
        std::printf("sc2csv-cli %s\n", SC2CSV_VERSION);
        return 0;
    }
    return Run(options);
}

#ifdef _WIN32
std::string WideToUtf8(std::wstring_view text) {
    if (text.empty())
        return {};
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(std::size_t(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}
#endif

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    // argv arrives as UTF-16, so paths with non-ASCII characters survive; the conversion itself works in UTF-8.
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> args;
    args.reserve(std::size_t(argc > 1 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index)
        args.push_back(WideToUtf8(argv[index]));
    return RunMain(args);
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    return RunMain(args);
}
#endif
