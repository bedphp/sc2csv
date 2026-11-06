// sc2csv-cli — console version of sc2csv.
//
// It shares the conversion core (sc_csv) with the windowed program, so both directions, skip/overwrite semantics and
// the in-place mode behave the same way. The interface follows the system language, like the window, and --lang
// overrides it.

#include "csv_project.h"
#include "error_text.h"
#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
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
#define SC2CSV_VERSION "0.1.1"
#endif

namespace {

using sc2csv::ConvertOptions;
using sc2csv::CsvFile;
using sc2csv::DecodeOutcome;
using sc2csv::FileStatus;
using sc2csv::OutputFormat;

sc2csv::Translator g_translator;

std::string Tr(std::string_view key) {
    return g_translator.Text(key);
}

// A translated printf-style template. The tables keep the format specifiers (%zu, %d, %s, %.2f).
std::string TrFormat(const char* key, ...) {
    const std::string format = g_translator.Text(key);
    char buffer[512];
    va_list args;
    va_start(args, key);
    const int needed = std::vsnprintf(buffer, sizeof(buffer), format.c_str(), args);
    va_end(args);
    if (needed < 0)
        return format;
    if (std::size_t(needed) < sizeof(buffer))
        return std::string(buffer, std::size_t(needed));

    std::string result(std::size_t(needed), '\0');
    va_start(args, key);
    std::vsnprintf(result.data(), result.size() + 1, format.c_str(), args);
    va_end(args);
    return result;
}

struct Options {
    bool decode = false;
    bool encode = false;
    bool overwrite = false;
    bool flat = false;
    bool in_place = false;
    bool quiet = false;
    std::string language;
    std::filesystem::path input;
    std::filesystem::path output;
};

void PrintUsage(std::FILE* out) {
    std::fprintf(out, "%s", TrFormat("cli.help", SC2CSV_VERSION).c_str());
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
            error = TrFormat("cli.error.not_csv", sc2csv::PathToUtf8(input).c_str());
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
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string& argument = args[index];
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
        } else if (argument == "--lang") {
            if (index + 1 >= args.size()) {
                error = Tr("cli.error.missing_lang");
                return false;
            }
            options.language = args[++index];
        } else if (!argument.empty() && argument.front() == '-') {
            error = TrFormat("cli.error.unknown_option", argument.c_str());
            return false;
        } else {
            positional.push_back(argument);
        }
    }

    if (help || version)
        return true;

    if (options.decode == options.encode) {
        error = Tr("cli.error.mode");
        return false;
    }
    if (positional.empty()) {
        error = Tr("cli.error.missing_input");
        return false;
    }
    options.input = sc2csv::PathFromUtf8(positional[0]);

    if (options.in_place) {
        if (positional.size() > 1) {
            error = Tr("cli.error.in_place_args");
            return false;
        }
    } else {
        if (positional.size() < 2) {
            error = Tr("cli.error.missing_output");
            return false;
        }
        if (positional.size() > 2) {
            error = TrFormat("cli.error.too_many", positional[2].c_str());
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
        std::fprintf(stderr, "%s\n", TrFormat("cli.error.scan", error.c_str()).c_str());
        return 2;
    }
    if (files.empty()) {
        std::fprintf(stderr, "%s\n",
                     TrFormat("cli.error.no_files", sc2csv::PathToUtf8(options.input).c_str()).c_str());
        return 2;
    }

    ConvertOptions convert;
    convert.overwrite = options.overwrite;
    convert.keep_structure = !options.flat;
    convert.in_place = options.in_place;
    convert.format = options.decode ? OutputFormat::Text : OutputFormat::Container;

    const std::string mode = Tr(options.decode ? "mode.unpack" : "mode.pack");
    if (!options.quiet) {
        const std::string output = options.in_place ? Tr("cli.in_place") : sc2csv::PathToUtf8(options.output);
        std::printf("%s\n", TrFormat("cli.start", mode.c_str(), sc2csv::PathToUtf8(options.input).c_str(),
                                     output.c_str(), files.size())
                                .c_str());
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
                std::printf("%s\n", TrFormat("cli.line.ok", index, files.size(), file.relative.c_str()).c_str());
        } else if (outcome.status == FileStatus::Skipped) {
            ++skipped;
            if (!options.quiet)
                std::printf("%s\n", TrFormat("cli.line.skip", index, files.size(), file.relative.c_str()).c_str());
        } else {
            ++failed;
            std::fprintf(stderr, "%s\n",
                         TrFormat("cli.line.fail", index, files.size(), file.relative.c_str(),
                                  outcome.error.c_str())
                             .c_str());
        }
    }

    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("%s\n", TrFormat("cli.summary", mode.c_str(), files.size(), converted, skipped, failed, seconds).c_str());
    return failed == 0 ? 0 : 1;
}

int RunMain(const std::vector<std::string>& args) {
    // Resolve the language before parsing, so even a usage error is printed in the language --lang asks for.
    std::string requested;
    for (std::size_t index = 0; index + 1 < args.size(); ++index) {
        if (args[index] == "--lang")
            requested = args[index + 1];
    }

    std::string load_error;
    if (!g_translator.LoadEmbedded(load_error)) {
        std::fprintf(stderr, "sc2csv-cli: %s\n", load_error.c_str());
        return 2;
    }
    if (!requested.empty()) {
        if (!g_translator.SetLocale(requested)) {
            // An unknown code warns and continues in English rather than refusing to run.
            g_translator.SetLocale("en-us");
            std::fprintf(stderr, "%s\n", TrFormat("cli.error.language", requested.c_str()).c_str());
        }
    } else {
        g_translator.SetLocale(sc2csv::SystemLanguage());
    }
    // Conversion errors come from the shared core; route them through the selected table as well.
    sc2csv::SetErrorTranslator([](std::string_view key) { return g_translator.Text(key); });

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
