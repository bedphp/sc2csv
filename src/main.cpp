// sc2csv — Brawl Stars CSV 解包工具
//
// 解码 assets/csv_logic、assets/csv_client、assets/localization 下由「9 字节头 + raw LZMA1」
// 构成的 CSV 容器。页面：解包 / 浏览 / 日志。
//
// 两个方向：
//   解包  容器（9 字节头 + LZMA1） → 明文 CSV
//   打包  明文 CSV → 容器
// 明文 CSV 也直接识别（新版安装包就是明文），解包时原样输出。
//
// 命令行用法（无界面）：
//   sc2csv --decode <输入目录> <输出目录> [--overwrite] [--flat]
//   sc2csv --encode <输入目录> <输出目录> [--overwrite] [--flat]

#include "Cupertino.h"

#include "csv_codec.h"
#include "csv_project.h"
#include "error_text.h"
#include "generated/AboutIcon.h"
#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <GLFW/glfw3.h>
// <wingdi.h>, which <windows.h> pulls in, declares LoadBitmap() as a macro; undefining it lets Cupertino::LoadBitmap
// (the image loader of the UI kit) be called by name in this translation unit.
#ifdef LoadBitmap
#undef LoadBitmap
#endif
#endif

using namespace Cupertino;
using sc2csv::CsvFile;
using sc2csv::CsvHeader;
using sc2csv::CsvTable;
using sc2csv::FileStatus;

namespace {

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class LogLevel { Info = 0, Success = 1, Warning = 2, Error = 3 };

struct LogEntry {
    std::string time;
    std::string text;
    LogLevel level = LogLevel::Info;
};

struct State {
    int mode = 0;  // 0: 解包（容器 → 文本）, 1: 打包（文本 → 容器）
    int language = 0;

    // Empty until the user picks a folder: the window opens on its empty state rather than on someone else's paths.
    std::string input;
    std::string output;
    std::string search;
    bool notice_presented = false;
    std::string notice_title_key = "notice.title";
    std::string notice_body_key;
    // The About panel opened from the app menu.
    bool about_presented = false;

    bool keep_structure = true;
    // Skip a destination that is already there instead of overwriting it. The CLI's --overwrite clears it.
    bool skip_existing = true;
    // Write back over the files in the input folder; no output folder takes part.
    bool in_place = false;

    std::vector<CsvFile> files;
    std::vector<bool> selection;
    std::vector<int> visible;
    bool scanned = false;
    bool scan_failed = false;
    std::string scanned_input;

    bool running = false;
    bool cancel_requested = false;
    std::vector<int> jobs;
    std::size_t job_cursor = 0;
    int succeeded = 0;
    int skipped = 0;
    int failed = 0;
    float progress = 0.0f;
    std::string current_name;
    std::chrono::steady_clock::time_point run_started;

    // Debug: shrink the window from the frame loop, which goes through the same GLFW path a drag takes.
    int resize_after_frame = -1;
    ImVec2 resize_after_size = ImVec2(0.0f, 0.0f);
    // Debug: a drag of the window edge, one step per frame, so the frames in between can be looked at.
    int resize_drag_start = -1;
    int resize_drag_frames = 0;
    ImVec2 resize_drag_to = ImVec2(0.0f, 0.0f);

    std::vector<LogEntry> log;
    int log_filter = 0;
    float log_offset = 0.0f;
    std::size_t log_seen = 0;
    bool log_expanded = false;
};

State g;

// ---------------------------------------------------------------------------
// i18n
// ---------------------------------------------------------------------------

sc2csv::Translator g_translator;

std::string Tr(std::string_view key) { return g_translator.Text(key); }

// A translated printf-style template. Language files keep the format specifiers (%zu, %d, %s, %.1f).
std::string TrFormat(const char* key, ...) {
    const std::string format = g_translator.Text(key);
    char buffer[256];
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

// Switches every label on the next frame; the log keeps the entries it already wrote.
void ApplyLanguage(State& state, int index) {
    const auto languages = sc2csv::AvailableLanguages();
    if (index < 0 || std::size_t(index) >= languages.size())
        return;
    g_translator.SetLocaleIndex(std::size_t(index));
    state.language = index;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// "MM-DD HH:MM" of the executable on disk, i.e. when this build was linked. __DATE__/__TIME__ would freeze at the last
// edit of this file, so a rebuilt exe would keep the old stamp and look unchanged.
const char* BuildStamp() {
    static const std::string stamp = [] {
        char buffer[32];
#ifdef _WIN32
        wchar_t path[MAX_PATH];
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0 &&
            GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
            SYSTEMTIME utc{};
            SYSTEMTIME local{};
            if (FileTimeToSystemTime(&data.ftLastWriteTime, &utc) &&
                SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
                std::snprintf(buffer, sizeof(buffer), "%02u-%02u %02u:%02u",
                              unsigned(local.wMonth), unsigned(local.wDay),
                              unsigned(local.wHour), unsigned(local.wMinute));
                return std::string(buffer);
            }
        }
#endif
        static const char* const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        const std::string date = __DATE__;  // "Nov  4 2026"
        const std::string time = __TIME__;  // "23:15:36"
        const int month = int(std::find(std::begin(months), std::end(months), date.substr(0, 3)) -
                              std::begin(months)) + 1;
        const int day = std::atoi(date.substr(4, 2).c_str());
        std::snprintf(buffer, sizeof(buffer), "%02d-%02d %s", month, day, time.substr(0, 5).c_str());
        return std::string(buffer);
    }();
    return stamp.c_str();
}

std::string LowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return char(std::tolower(ch)); });
    return text;
}

std::string Timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    return buffer;
}

void AddLog(State& state, LogLevel level, std::string text) {
    state.log.push_back(LogEntry{Timestamp(), std::move(text), level});
    if (state.log.size() > 4000)
        state.log.erase(state.log.begin(), state.log.begin() + 1000);
}

sc2csv::OutputFormat CurrentFormat(const State& state) {
    return state.mode == 0 ? sc2csv::OutputFormat::Text : sc2csv::OutputFormat::Container;
}

std::string FormatName(sc2csv::CsvFormat format) {
    switch (format) {
        case sc2csv::CsvFormat::Lzma: return "LZMA";
        case sc2csv::CsvFormat::Plain: return Tr("format.plain");
        default: return Tr("format.unknown");
    }
}

std::string StatusText(FileStatus status) {
    switch (status) {
        case FileStatus::Pending: return Tr("status.pending");
        case FileStatus::Running: return Tr("status.running");
        case FileStatus::Done:    return Tr("status.done");
        case FileStatus::Failed:  return Tr("status.failed");
        case FileStatus::Skipped: return Tr("status.skipped");
    }
    return {};
}

void RebuildVisible(State& state) {
    state.visible.clear();
    const std::string needle = LowerAscii(state.search);
    for (int i = 0; i < int(state.files.size()); ++i) {
        const CsvFile& file = state.files[size_t(i)];
        if (!needle.empty()) {
            if (LowerAscii(file.name).find(needle) == std::string::npos &&
                LowerAscii(file.folder).find(needle) == std::string::npos &&
                LowerAscii(file.relative).find(needle) == std::string::npos)
                continue;
        }
        state.visible.push_back(i);
    }
}

int SelectedCount(const State& state) {
    int count = 0;
    for (bool selected : state.selection)
        if (selected) ++count;
    return count;
}

// Marks files whose destination already exists, so the table shows what a run that skips existing files would leave out.
// Statuses left by a finished run (done/failed) are not rewritten.
void RefreshSkipMarkers(State& state) {
    if (state.files.empty())
        return;
    const std::filesystem::path output = sc2csv::PathFromUtf8(state.output);
    sc2csv::ConvertOptions options;
    options.keep_structure = state.keep_structure;
    options.overwrite = !state.skip_existing;
    options.in_place = state.in_place;
    for (CsvFile& file : state.files) {
        if (file.status == FileStatus::Running || file.status == FileStatus::Done || file.status == FileStatus::Failed)
            continue;
        bool present = false;
        // In place every file already exists by definition, so there is nothing to mark.
        if (!state.in_place && state.skip_existing && !state.output.empty()) {
            std::error_code ec;
            present = std::filesystem::exists(sc2csv::DestinationFor(file, output, options), ec);
        }
        file.status = present ? FileStatus::Skipped : FileStatus::Pending;
    }
}

// Clears the folders and starts the new direction (defined after Rescan).
void SetMode(State& state, int mode);

void Rescan(State& state) {
    state.scanned_input = state.input;
    state.files.clear();
    state.selection.clear();
    state.visible.clear();
    state.scan_failed = false;

    // Nothing to scan until a folder is chosen; that is the empty state, not an error.
    if (state.input.empty())
        return;

    std::string error;
    if (!sc2csv::ScanCsvFiles(sc2csv::PathFromUtf8(state.input), state.files, error)) {
        state.scan_failed = true;
        AddLog(state, LogLevel::Error, TrFormat("log.scan_failed", error.c_str()));
        return;
    }
    state.selection.assign(state.files.size(), true);

    // Files whose decoded copy is already on disk are marked as such, so the list reflects what a run would do.
    RefreshSkipMarkers(state);

    int logic = 0;
    int client = 0;
    int localization = 0;
    for (const CsvFile& file : state.files) {
        if (file.folder == "csv_logic") ++logic;
        else if (file.folder == "csv_client") ++client;
        else if (file.folder == "localization") ++localization;
    }
    AddLog(state, LogLevel::Info, TrFormat("log.scan_summary", state.files.size(), logic, client, localization));
}

// Switching direction starts over: the folders a run used say nothing about the other direction (one reads containers,
// the other writes them), so both are cleared and the user picks them again for the mode they are in. Nothing is
// guessed and nothing is swapped, which keeps the window predictable for anyone opening it the first time.
void SetMode(State& state, int mode) {
    if (state.mode == mode)
        return;
    state.mode = mode;
    state.input.clear();
    state.output.clear();
    Rescan(state);
}

// ---------------------------------------------------------------------------
// Window icon
// ---------------------------------------------------------------------------

// The PNG the About panel draws, baked into the executable at build time (see cmake/EmbedIcon.py).
Cupertino::Bitmap AppIcon() {
    const std::span<const unsigned char> png(sc2csv::kAboutIconPng, sc2csv::kAboutIconPngSize);
    static const Cupertino::Bitmap icon = Cupertino::LoadBitmap(std::string("icon.about"), png, 2.0f);
    return icon;
}

// The icon the executable carries (embedded by rc.exe from icon/ico.png), for the window's title bar, the taskbar
// button and Alt+Tab: GLFW needs the actual pixels, so the resource is read back.
constexpr int kAppIconResource = 101;  // IDI_APPICON in the generated sc2csv.rc

void ApplyWindowIcon() {
#ifdef _WIN32
    GLFWimage images[2];
    const int sizes[2] = {32, 64};   // the .ico members GLFW uses for its small and large icon
    const HMODULE module = GetModuleHandleW(nullptr);
    for (int i = 0; i < 2; ++i) {
        const HICON handle = static_cast<HICON>(
            LoadImageW(module, MAKEINTRESOURCEW(kAppIconResource), IMAGE_ICON, sizes[i], sizes[i], 0));
        if (!handle) {
            std::fprintf(stderr, "icon: cannot load resource %d\n", kAppIconResource);
            return;
        }
        ICONINFO info{};
        if (!GetIconInfo(handle, &info)) {
            DestroyIcon(handle);
            return;
        }
        BITMAP bitmap{};
        GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap);
        const int width = bitmap.bmWidth;
        const int height = bitmap.bmHeight;
        static std::vector<std::vector<unsigned char>> keep;  // GLFW copies the pixels; the buffers stay alive here
        keep.emplace_back(std::size_t(width) * std::size_t(height) * 4);
        std::vector<unsigned char>& pixels = keep.back();
        BITMAPINFO header{};
        header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        header.bmiHeader.biWidth = width;
        header.bmiHeader.biHeight = -height;  // top-down, the order GLFW wants
        header.bmiHeader.biPlanes = 1;
        header.bmiHeader.biBitCount = 32;
        header.bmiHeader.biCompression = BI_RGB;
        HDC dc = GetDC(nullptr);
        GetDIBits(dc, info.hbmColor, 0, UINT(height), pixels.data(), &header, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        // GetDIBits writes 32-bit pixels as B,G,R,A; GLFW's GLFWimage is R,G,B,A (createIcon swaps R and B again when
        // it builds the Windows icon). Without this the window's taskbar icon comes out with red and blue exchanged.
        for (std::size_t pixel = 0; pixel + 3 < pixels.size(); pixel += 4)
            std::swap(pixels[pixel], pixels[pixel + 2]);
        DeleteObject(info.hbmColor);
        DeleteObject(info.hbmMask);
        DestroyIcon(handle);
        images[i].width = width;
        images[i].height = height;
        images[i].pixels = pixels.data();
    }
    if (GLFWwindow* window = glfwGetCurrentContext())
        glfwSetWindowIcon(window, 2, images);
#endif
}

// The icon the About panel shows above its title: the same picture the executable carries.
void DrawAboutIcon(ImDrawList* draw, const ImRect& frame) {
    const Bitmap icon = AppIcon();
    if (icon.IsEmpty())
        return;
    Draw::Image(draw, frame, icon, Px(12.0f));
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

void FinishRun(State& state) {
    state.running = false;
    state.current_name.clear();
    state.jobs.clear();
    state.job_cursor = 0;
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - state.run_started).count();
    const std::string summary =
        TrFormat(state.cancel_requested ? "log.cancelled" : "log.finished", state.succeeded, state.skipped,
                 state.failed, seconds);
    if (state.cancel_requested) {
        AddLog(state, LogLevel::Warning, summary);
    } else {
        AddLog(state, state.failed > 0 ? LogLevel::Warning : LogLevel::Success, summary);
    }
    state.cancel_requested = false;
}

void StartUnpack(State& state) {
    if (state.running)
        return;
    // In place needs no output folder: the run writes over the files it read.
    const bool needs_output = !state.in_place;
    const auto ask = [&](const char* log_key, const char* notice_key) {
        AddLog(state, LogLevel::Warning, Tr(log_key));
        state.notice_title_key = "notice.title";
        state.notice_body_key = notice_key;
        state.notice_presented = true;
    };
    // A run reads the input folder first and then needs somewhere to write, unless it overwrites in place.
    if (state.input.empty()) {
        ask("log.need_input", "notice.input");
        return;
    }
    if (state.scanned_input != state.input)
        Rescan(state);
    if (needs_output && state.output.empty()) {
        ask("log.need_output", "notice.output");
        return;
    }
    state.jobs.clear();
    for (int i = 0; i < int(state.selection.size()); ++i)
        if (state.selection[size_t(i)])
            state.jobs.push_back(i);
    if (state.jobs.empty()) {
        AddLog(state, LogLevel::Warning,
               Tr(state.mode == 0 ? "log.no_selection_unpack" : "log.no_selection_pack"));
        state.notice_title_key = "notice.selection_title";
        state.notice_body_key = "notice.selection";
        state.notice_presented = true;
        return;
    }
    state.running = true;
    state.cancel_requested = false;
    state.job_cursor = 0;
    state.succeeded = 0;
    state.skipped = 0;
    state.failed = 0;
    state.progress = 0.0f;
    state.run_started = std::chrono::steady_clock::now();
    AddLog(state, LogLevel::Info,
           TrFormat(state.mode == 0 ? "log.start_unpack" : "log.start_pack", state.jobs.size(),
                    (state.in_place ? state.input : state.output).c_str()));
}

// A few files per frame keep the interface responsive without a worker thread.
void ProcessJobs(State& state) {
    if (!state.running)
        return;
    if (state.cancel_requested) {
        FinishRun(state);
        return;
    }

    const auto frame_start = std::chrono::steady_clock::now();
    const std::filesystem::path output = sc2csv::PathFromUtf8(state.output);
    sc2csv::ConvertOptions options;
    options.keep_structure = state.keep_structure;
    // Skipping is decided in ConvertCsvFile, which only looks at it when the run writes into an output folder.
    options.overwrite = !state.skip_existing;
    options.in_place = state.in_place;
    options.format = CurrentFormat(state);

    while (state.job_cursor < state.jobs.size()) {
        const int index = state.jobs[state.job_cursor];
        if (index < 0 || index >= int(state.files.size())) {
            ++state.job_cursor;
            continue;
        }
        CsvFile& file = state.files[size_t(index)];
        file.status = FileStatus::Running;
        file.error.clear();
        state.current_name = file.name;

        const sc2csv::DecodeOutcome outcome = sc2csv::ConvertCsvFile(file, output, options);
        file.status = outcome.status;
        file.error = outcome.error;

        if (outcome.status == FileStatus::Done) {
            ++state.succeeded;
            file.decoded = outcome.decoded;
            // The written size differs by direction: packing trades the text for the container.
            std::error_code size_error;
            const std::uintmax_t written =
                std::filesystem::file_size(sc2csv::DestinationFor(file, output, options), size_error);
            const std::uint64_t shown = size_error ? outcome.decoded : std::uint64_t(written);
            const std::string result_size = sc2csv::FormatBytes(shown);
            AddLog(state, LogLevel::Success,
                   file.name + "  " +
                       (state.mode == 0 ? sc2csv::FormatBytes(file.compressed) + " → " + result_size
                                        : sc2csv::FormatBytes(outcome.decoded) + " → " + result_size));
        } else if (outcome.status == FileStatus::Skipped) {
            ++state.skipped;
            AddLog(state, LogLevel::Warning, TrFormat("log.skipped", file.name.c_str()));
        } else {
            ++state.failed;
            AddLog(state, LogLevel::Error,
                   TrFormat(state.mode == 0 ? "log.decode_failed" : "log.encode_failed", file.name.c_str(),
                            outcome.error.c_str()));
        }

        ++state.job_cursor;
        state.progress = float(state.job_cursor) / float(state.jobs.size());

        if (std::chrono::steady_clock::now() - frame_start > std::chrono::milliseconds(8))
            break;
    }

    if (state.job_cursor >= state.jobs.size())
        FinishRun(state);
}

// ---------------------------------------------------------------------------
// Windows integration
// ---------------------------------------------------------------------------

#ifdef _WIN32
std::string PickFolder(const std::string& initial) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* dialog = nullptr;
    std::string result;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options)))
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (!initial.empty()) {
            IShellItem* folder = nullptr;
            const std::wstring wide = sc2csv::PathFromUtf8(initial).wstring();
            if (SUCCEEDED(SHCreateItemFromParsingName(wide.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = sc2csv::PathToUtf8(std::filesystem::path(path));
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return result;
}

void RevealInExplorer(const std::filesystem::path& path) {
    const std::wstring arguments = L"/select,\"" + path.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}
#else
std::string PickFolder(const std::string&) { return {}; }
void RevealInExplorer(const std::filesystem::path&) {}
#endif

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------

// The width of the sidebar column, in points.
constexpr float kSidebarWidth = 240.0f;

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

// Asks the frame loop to close the window. The app runs through Cupertino::Run, which owns the Application object, so
// the GLFW flag it watches is the way back to its close path.
void RequestQuit() {
#ifdef _WIN32
    if (GLFWwindow* window = glfwGetCurrentContext())
        glfwSetWindowShouldClose(window, GLFW_TRUE);
#endif
}

// The bar under the title bar: the files, the mode, the interface language and finally About. The mode menu shows the
// current mode in its title and About runs on a click, so it needs no menu of its own.
void DrawMenuBar(State& state) {
    MenuBarInWindow({.boldFirstMenu = false}, [&] {
        Menu(Tr("menu.file").c_str(), [&] {
            if (Button(Tr("menu.input").c_str(), {.shortcut = {.key = "o"}})) {
                const std::string folder = PickFolder(state.input);
                if (!folder.empty()) {
                    state.input = folder;
                    Rescan(state);
                }
            }
            if (Button(Tr("menu.output").c_str(),
                       {.shortcut = {.key = "o", .modifiers = EventModifiers::Command | EventModifiers::Shift}})) {
                const std::string folder = PickFolder(state.output);
                if (!folder.empty()) {
                    state.output = folder;
                    RefreshSkipMarkers(state);
                }
            }
            Divider();
            if (Button(Tr("menu.rescan").c_str(), {.shortcut = {.key = "r"}}))
                Rescan(state);
            Divider();
            if (Button(Tr("menu.quit").c_str(), {.shortcut = {.key = "q"}}))
                RequestQuit();
        });

        // The mode the bar shows in its title, and the menu that switches it. "##mode" keeps the menu's identity while
        // the title changes with the mode.
        const std::string mode_title =
            TrFormat("menu.mode_title", Tr(state.mode == 0 ? "mode.unpack" : "mode.pack").c_str());
        const std::string mode_label = mode_title + "##mode";
        Menu(mode_label.c_str(), [&] {
            // One checked item per mode; a click on the checked one turns it off and is ignored, like the segments of the
            // picker this menu replaced.
            bool unpack = state.mode == 0;
            if (Toggle(Tr("mode.unpack").c_str(), &unpack) && unpack)
                SetMode(state, 0);
            bool pack = state.mode == 1;
            if (Toggle(Tr("mode.pack").c_str(), &pack) && pack)
                SetMode(state, 1);
        });

        Menu(Tr("menu.language").c_str(), [&] {
            const auto languages = sc2csv::AvailableLanguages();
            for (std::size_t index = 0; index < languages.size(); ++index) {
                // A checkbox item per language; a click on the checked one turns it off and is ignored, so the menu
                // behaves like the radio buttons of a picker.
                bool checked = std::size_t(state.language) == index;
                const std::string language_name(languages[index].name);
                if (Toggle(language_name.c_str(), &checked) && checked)
                    ApplyLanguage(state, int(index));
            }
        });

        // About runs straight from the bar instead of opening a menu of its own.
        MenuBarAction(Tr("menu.about").c_str(), [&] { state.about_presented = true; });
    });
}

// ---------------------------------------------------------------------------
// UI 组件
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 新布局：侧边栏 + 主内容
// ---------------------------------------------------------------------------

void DrawStatusCell(const TableCell& cell, const CsvFile& file);

// Drawn under the run's buttons; defined with the rest of the log further down.
void DrawLogPanel(State& state);

void DrawSidebar(State& state) {
    const Palette& colors = Theme::Colors();
    Background(
        [&](ImDrawList* draw, const ImRect& rect) {
            draw->AddRectFilled(rect.Min, rect.Max, colors.sidebarBackground.Packed());
            draw->AddLine(ImVec2(rect.Max.x - Px(1.0f), rect.Min.y),
                          ImVec2(rect.Max.x - Px(1.0f), rect.Max.y), colors.sidebarDivider.Packed());
        },
        [&] {
            VStack({.alignment = HorizontalAlignment::Leading, .spacing = 0.0f}, [&] {
                Padding(EdgeInsets{.top = 18.0f, .leading = 14.0f, .bottom = 16.0f, .trailing = 14.0f}, [&] {
                    VStack({.alignment = HorizontalAlignment::Leading, .spacing = 14.0f}, [&] {
                        Text(Tr("sidebar.workdir"),
                             {.font = Font::Style(TextStyle::Subheadline).Weight(FontWeight::Semibold)});
                        VStack({.alignment = HorizontalAlignment::Leading, .spacing = 6.0f}, [&] {
                            Text(Tr("sidebar.input"),
                                 {.font = Font::Style(TextStyle::Caption), .foreground = Foreground::Secondary});
                            if (Button(Tr("sidebar.choose_input").c_str(), {.symbol = Symbols::FolderFill})) {
                                const std::string folder = PickFolder(state.input);
                                if (!folder.empty()) {
                                    state.input = folder;
                                    Rescan(state);
                                }
                            }
                            Text(state.input.empty() ? Tr("sidebar.not_selected") : state.input,
                                 {.font = Font::Style(TextStyle::Footnote), .foreground = Foreground::Tertiary,
                                  .truncation = TruncationMode::Middle});
                        });
                        VStack({.alignment = HorizontalAlignment::Leading, .spacing = 6.0f}, [&] {
                            Text(Tr("sidebar.output"),
                                 {.font = Font::Style(TextStyle::Caption), .foreground = Foreground::Secondary});
                            // In place the run writes over the files it read, so there is no output folder at all.
                            Disabled(state.in_place, [&] {
                                if (Button(Tr("sidebar.choose_output").c_str(),
                                           {.symbol = Symbols::Internaldrive})) {
                                    const std::string folder = PickFolder(state.output);
                                    if (!folder.empty()) {
                                        state.output = folder;
                                        RefreshSkipMarkers(state);
                                    }
                                }
                            });
                            Text(state.in_place
                                     ? Tr("sidebar.output_in_place")
                                     : (state.output.empty() ? Tr("sidebar.not_selected") : state.output),
                                 {.font = Font::Style(TextStyle::Footnote), .foreground = Foreground::Tertiary,
                                  .truncation = TruncationMode::Middle});
                            // Skipping is about the output folder, which an in-place run does not use: every file it
                            // writes is the file it reads, so the switch is hidden rather than shown dead.
                            if (!state.in_place) {
                                if (Toggle(Tr("sidebar.skip_existing").c_str(), &state.skip_existing,
                                           {.style = ToggleStyle::Checkbox}))
                                    RefreshSkipMarkers(state);
                            }
                            if (Toggle(Tr("sidebar.in_place").c_str(), &state.in_place,
                                       {.style = ToggleStyle::Checkbox}))
                                RefreshSkipMarkers(state);
                            if (state.in_place) {
                                Text(Tr("sidebar.in_place_note"),
                                     {.font = Font::Style(TextStyle::Footnote), .foreground = Foreground::Tertiary,
                                      .wraps = true});
                            }
                        });
                    });
                });

                Divider(colors.sidebarDivider);

                Padding(EdgeInsets{.top = 12.0f, .leading = 14.0f, .bottom = 7.0f, .trailing = 14.0f}, [&] {
                    Text(Tr("sidebar.overview"),
                         {.font = Font::Style(TextStyle::Caption), .foreground = Foreground::Secondary});
                });
                Padding(EdgeInsets{.leading = 14.0f, .trailing = 14.0f}, [&] {
                    VStack({.alignment = HorizontalAlignment::Leading, .spacing = 8.0f}, [&] {
                        HStack({.spacing = 6.0f}, [&] {
                            Text(Tr("sidebar.discovered"),
                                 {.font = Font::Style(TextStyle::Body), .foreground = Foreground::Secondary});
                            Spacer();
                            Text(TrFormat("common.files", state.files.size()),
                                 {.font = Font::Style(TextStyle::Body).MonospacedDigit(),
                                  .foreground = Foreground::Primary});
                        });
                        HStack({.spacing = 6.0f}, [&] {
                            Text(Tr("sidebar.selected"),
                                 {.font = Font::Style(TextStyle::Body), .foreground = Foreground::Secondary});
                            Spacer();
                            // Selected / visible, so a search filter still shows how much of the list is ticked.
                            Text(std::to_string(SelectedCount(state)) + " / " +
                                     std::to_string(state.visible.size()),
                                 {.font = Font::Style(TextStyle::Body).MonospacedDigit(),
                                  .foreground = Foreground::Primary});
                        });
                    });
                });

                Spacer();

                Padding(EdgeInsets{.top = 8.0f, .leading = 14.0f, .bottom = 12.0f, .trailing = 14.0f}, [&] {
                    // Link-time stamp (the exe's own timestamp), so two builds can be told apart at a glance.
                    Text(Tr("app.build") + " " + BuildStamp(),
                         {.font = Font::Style(TextStyle::Footnote), .foreground = Foreground::Tertiary,
                          .truncation = TruncationMode::Middle});
                });
            });
        });
}

void DrawMainContent(State& state) {
    const Palette& colors = Theme::Colors();
    VStack({.alignment = HorizontalAlignment::Leading, .spacing = 0.0f, .clipsContent = true}, [&] {
        // 搜索和操作栏
        Padding(EdgeInsets{.top = 10.0f, .leading = 16.0f, .bottom = 10.0f, .trailing = 16.0f}, [&] {
            VStack({.alignment = HorizontalAlignment::Leading, .spacing = 8.0f}, [&] {
                HStack({.spacing = 10.0f}, [&] {
                    SearchField("##search", &state.search,
                                {.placeholder = Tr("main.search").c_str(), .width = 240.0f});
                    if (Button(Tr("main.select_all").c_str(),
                               {.style = ButtonStyle::Borderless, .font = Font::Style(TextStyle::Footnote)})) {
                        for (int index : state.visible)
                            state.selection[size_t(index)] = true;
                    }
                    if (Button(Tr("main.select_none").c_str(),
                               {.style = ButtonStyle::Borderless, .font = Font::Style(TextStyle::Footnote)})) {
                        for (int index : state.visible)
                            state.selection[size_t(index)] = false;
                    }
                    Spacer();
                    if (state.running) {
                        if (Button(Tr("menu.cancel").c_str(), {.role = ButtonRole::Cancel}))
                            state.cancel_requested = true;
                    } else if (Button(Tr(state.mode == 0 ? "mode.unpack" : "mode.pack").c_str(),
                                      {.role = ButtonRole::Default, .symbol = Symbols::PlayFill})) {
                        if (state.files.empty() && !state.scan_failed) Rescan(state);
                        StartUnpack(state);
                    }
                });
                if (state.running) {
                    HStack({.spacing = 10.0f}, [&] {
                        Text(TrFormat(state.mode == 0 ? "main.unpacking" : "main.packing",
                                      state.current_name.c_str()),
                             {.font = Font::Style(TextStyle::Footnote), .foreground = Foreground::Secondary,
                              .truncation = TruncationMode::Middle});
                        ProgressView(state.progress, {.width = 160.0f});
                        Text(std::to_string(int(state.progress * 100.0f)) + "%",
                             {.font = Font::Style(TextStyle::Footnote).MonospacedDigit(),
                              .foreground = Foreground::Tertiary});
                        Spacer();
                    });
                }
            });
        });

        Divider(colors.toolbarLine);

        // 文件表格
        Frame({.maxWidth = Infinity, .maxHeight = Infinity, .clipsContent = true}, [&] {
            if (state.visible.empty()) {
                if (state.input.empty()) {
                    const std::string empty_title = Tr("main.empty_title");
                    const std::string empty_desc = Tr("main.empty_desc");
                    const std::string empty_button = Tr("main.empty_button");
                    ContentUnavailableView(empty_title.c_str(),
                                           {.symbol = Symbols::FolderFill,
                                            .description = empty_desc.c_str()},
                                           [&] {
                                               if (Button(empty_button.c_str(),
                                                          {.role = ButtonRole::Default,
                                                           .symbol = Symbols::FolderFill})) {
                                                   const std::string folder = PickFolder(state.input);
                                                   if (!folder.empty()) {
                                                       state.input = folder;
                                                       Rescan(state);
                                                   }
                                               }
                                           });
                } else {
                    const std::string empty_title = Tr(state.scan_failed ? "main.scan_failed" : "main.no_match");
                    const std::string empty_desc =
                        Tr(state.scan_failed ? "main.scan_failed_desc" : "main.no_match_desc");
                    ContentUnavailableView(empty_title.c_str(),
                                           {.symbol = Symbols::FolderFill,
                                            .description = empty_desc.c_str()});
                }
            } else {
                const std::size_t rows = state.visible.size();
                const std::string name_header = Tr("table.name");
                const std::string format_header = Tr("table.format");
                const std::string size_header = Tr("table.size");
                const std::string status_header = Tr("table.status");
                Padding(EdgeInsets{.leading = 4.0f, .trailing = 4.0f}, [&] {
                    Table("##files",
                          {.columns = {
                               {"", 36.0f},
                               {name_header.c_str(), 0.0f},
                               {format_header.c_str(), 88.0f},
                               {size_header.c_str(), 88.0f, TextAlignment::Trailing},
                               {status_header.c_str(), 104.0f},
                           },
                           .rows = int(rows),
                           .rowHeight = 26.0f,
                           .initialFocus = false},
                          nullptr, [&](const TableCell& cell) {
                              const int file = state.visible[size_t(cell.row)];
                              const CsvFile& row = state.files[size_t(file)];
                              switch (cell.column) {
                                  case 0:
                                      if (TableCheckbox(cell, state.selection[size_t(file)]))
                                          state.selection[size_t(file)] = !state.selection[size_t(file)];
                                      break;
                                  case 1: TableText(cell, row.name); break;
                                  case 2: TableText(cell, FormatName(row.format), true); break;
                                  case 3: TableText(cell, sc2csv::FormatBytes(row.compressed), true); break;
                                  case 4: DrawStatusCell(cell, row); break;
                                  default: break;
                              }
                          });
                });
            }
        });

        Divider();

        // 日志面板
        DrawLogPanel(state);
    });
}

void DrawNewUnpackPage(State& state) {
    HStack({.alignment = VerticalAlignment::Top, .spacing = 0.0f}, [&] {
        Frame({.width = kSidebarWidth, .maxHeight = Infinity, .clipsContent = true}, [&] { DrawSidebar(state); });
        Frame({.maxWidth = Infinity, .maxHeight = Infinity, .clipsContent = true}, [&] { DrawMainContent(state); });
    });
}

void DrawStatusCell(const TableCell& cell, const CsvFile& file) {
    const Palette& colors = Theme::Colors();
    Rgba color;
    if (cell.emphasized) {
        color = colors.selectedContent;
    } else {
        switch (file.status) {
            case FileStatus::Done:    color = Rgba::Hex(0x30D158); break;
            case FileStatus::Failed:  color = Rgba::Hex(0xFF6961); break;
            case FileStatus::Running: color = colors.accent; break;
            case FileStatus::Skipped: color = colors.tertiaryLabel; break;
            default:                  color = colors.secondaryLabel; break;
        }
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImRect rect = cell.rect;
    if (!cell.emphasized) {
        const float radius = Px(3.0f);
        const float center_y = (rect.Min.y + rect.Max.y) * 0.5f;
        draw->AddCircleFilled(ImVec2(rect.Min.x + radius, center_y), radius, color.Packed());
        rect.Min.x += radius * 2.0f + Px(7.0f);
    }
    Typography::Draw(draw, cell.font, rect, color, StatusText(file.status), cell.alignment);
}

// ---------------------------------------------------------------------------
// 日志
// ---------------------------------------------------------------------------

void ExportLog(State& state) {
    if (state.output.empty()) {
        AddLog(state, LogLevel::Warning, Tr("log.need_output"));
        return;
    }
    const std::filesystem::path path = sc2csv::PathFromUtf8(state.output) / "sc2csv.log";
    const std::string path_text = sc2csv::PathToUtf8(path);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::FILE* file = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&file, path.wstring().c_str(), L"wb") != 0)
        file = nullptr;
#else
    file = std::fopen(path.string().c_str(), "wb");
#endif
    if (!file) {
        AddLog(state, LogLevel::Error, TrFormat("log.export_failed", path_text.c_str()));
        return;
    }
    for (const LogEntry& entry : state.log) {
        std::fprintf(file, "[%s] %s\n", entry.time.c_str(), entry.text.c_str());
    }
    std::fclose(file);
    AddLog(state, LogLevel::Success, TrFormat("log.exported", path_text.c_str()));
}

// The log under the file list, folded away until it is asked for: a run says what it did in one line, and the
// detail is there when something fails.
void DrawLogPanel(State& state) {
    // New entries bring the view to the newest line; scrolling up stays put until the next entry.
    if (state.log_seen != state.log.size()) {
        state.log_offset = 1.0e9f;
        state.log_seen = state.log.size();
    }

    const Palette& colors = Theme::Colors();

    Divider(colors.toolbarLine);
    Background(
        [&](ImDrawList* draw, const ImRect& rect) {
            draw->AddRectFilled(rect.Min, rect.Max, colors.toolbarBackground.Packed());
        },
        [&] {
            Padding(EdgeInsets{.top = 7.0f, .leading = 16.0f, .bottom = 7.0f, .trailing = 16.0f}, [&] {
                HStack({.spacing = 8.0f}, [&] {
                    if (DisclosureButton("##log-toggle", &state.log_expanded))
                        state.log_offset = 1.0e9f;
                    Text(Tr("log.title"),
                         {.font = Font::Style(TextStyle::Subheadline).Weight(FontWeight::Semibold)});
                    Text(TrFormat("log.count", state.log.size()),
                         {.font = Font::Style(TextStyle::Footnote).MonospacedDigit(),
                          .foreground = Foreground::Tertiary});
                    Spacer();
                    if (state.log_expanded) {
                        const std::string all_label = Tr("log.all");
                        const std::string info_label = Tr("log.info");
                        const std::string warning_label = Tr("log.warning");
                        const std::string error_label = Tr("log.error");
                        Picker("##level", &state.log_filter,
                               {all_label.c_str(), info_label.c_str(), warning_label.c_str(), error_label.c_str()},
                               {.style = PickerStyle::Menu, .width = 104.0f});
                        if (Button(Tr("log.clear").c_str(),
                                   {.style = ButtonStyle::Borderless, .font = Font::Style(TextStyle::Footnote)}))
                            state.log.clear();
                        if (Button(Tr("log.export").c_str(),
                                   {.style = ButtonStyle::Borderless, .font = Font::Style(TextStyle::Footnote)}))
                            ExportLog(state);
                    }
                });
            });
        });

    if (!state.log_expanded)
        return;

    // A fixed height, so the table keeps the rest of the window rather than the log growing into it.
    Frame({.height = 130.0f, .maxWidth = Infinity}, [&] {
        ScrollView({.padding = EdgeInsets{.top = 4.0f, .leading = 14.0f, .bottom = 8.0f, .trailing = 14.0f},
                    .offset = &state.log_offset},
                   [&] {
                       VStack({.alignment = HorizontalAlignment::Leading, .spacing = 4.0f}, [&] {
                           int shown = 0;
                           for (const LogEntry& entry : state.log) {
                               const int level = int(entry.level);
                               if (state.log_filter == 1 && level > 1) continue;
                               if (state.log_filter == 2 && level < 2) continue;
                               if (state.log_filter == 3 && level != 3) continue;
                               ++shown;
                               HStack({.spacing = 10.0f}, [&] {
                                   Text(entry.time,
                                        {.font = Font::Style(TextStyle::Footnote).Monospaced(),
                                         .foreground = Foreground::Tertiary});
                                   Rgba color;
                                   Foreground foreground = Foreground::Secondary;
                                   if (entry.level == LogLevel::Error) {
                                       foreground = Foreground::Custom;
                                       color = Rgba::Hex(0xFF6961);
                                   } else if (entry.level == LogLevel::Warning) {
                                       foreground = Foreground::Custom;
                                       color = Rgba::Hex(0xFFB340);
                                   } else if (entry.level == LogLevel::Success) {
                                       foreground = Foreground::Primary;
                                   }
                                   Text(entry.text,
                                        {.font = Font::Style(TextStyle::Footnote),
                                         .foreground = foreground,
                                         .color = color,
                                         .truncation = TruncationMode::Middle});
                               });
                           }
                           if (shown == 0)
                               Text(state.log.empty() ? Tr("log.none") : Tr("log.filtered"),
                                    {.foreground = Foreground::Tertiary});
                       });
                   });
    });
}

// ---------------------------------------------------------------------------
// Headless decoding
// ---------------------------------------------------------------------------

int RunConvertCli(const std::filesystem::path& input, const std::filesystem::path& output,
                  const sc2csv::ConvertOptions& options) {
    std::vector<CsvFile> files;
    std::string error;
    if (!sc2csv::ScanCsvFiles(input, files, error)) {
        std::fprintf(stderr, "scan failed: %s\n", error.c_str());
        return 2;
    }
    int succeeded = 0;
    int skipped = 0;
    int failed = 0;
    for (const CsvFile& file : files) {
        const sc2csv::DecodeOutcome outcome = sc2csv::ConvertCsvFile(file, output, options);
        if (outcome.status == FileStatus::Done) {
            ++succeeded;
        } else if (outcome.status == FileStatus::Skipped) {
            ++skipped;
        } else {
            ++failed;
            std::fprintf(stderr, "%s: %s\n", file.relative.c_str(), outcome.error.c_str());
        }
    }
    std::printf("files %zu, success %d, skipped %d, failed %d\n", files.size(), succeeded, skipped, failed);
    return failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    sc2csv::ConvertOptions convert_options;
    std::string language_code;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if ((argument == "--decode" || argument == "--encode") && i + 2 < argc) {
            const std::filesystem::path input = sc2csv::PathFromUtf8(argv[i + 1]);
            const std::filesystem::path output = sc2csv::PathFromUtf8(argv[i + 2]);
            for (int j = i + 3; j < argc; ++j) {
                const std::string flag = argv[j];
                if (flag == "--overwrite") convert_options.overwrite = true;
                else if (flag == "--flat") convert_options.keep_structure = false;
            }
            convert_options.format =
                argument == "--decode" ? sc2csv::OutputFormat::Text : sc2csv::OutputFormat::Container;
            return RunConvertCli(input, output, convert_options);
        }
    }

    ApplicationOptions options;
    options.title = "sc2csv";
    // The window keeps its size whatever the interface zoom is; zoom only scales what is drawn inside.
    options.size = ImVec2(940.0f, 660.0f);
    options.minSize = ImVec2(800.0f, 560.0f);
    options.zoom = 1.1f;
    options.appearance = Appearance::Dark;
    options.titleBar = true;
    options.debugTools = true;

    float zoom = 1.1f;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--ui-script" && i + 1 < argc)
            options.uiScriptPath = argv[++i];
        else if (argument == "--ui-dump-items" && i + 1 < argc)
            options.uiDumpItemsPath = argv[++i];
        else if (argument == "--input" && i + 1 < argc)
            g.input = argv[++i];
        else if (argument == "--output" && i + 1 < argc)
            g.output = argv[++i];
        else if (argument == "--lang" && i + 1 < argc)
            language_code = argv[++i];
        else if (argument == "--overwrite")
            g.skip_existing = false;
        else if (argument == "--zoom" && i + 1 < argc) {
            const float value = float(std::atof(argv[++i]));
            if (value >= 0.75f && value <= 2.0f)
                zoom = value;
        }
        else if (argument == "--size" && i + 1 < argc) {
            float width = 0.0f;
            float height = 0.0f;
            if (std::sscanf(argv[++i], "%fx%f", &width, &height) == 2)
                options.size = ImVec2(width, height);
        }
        else if (argument == "--resize-after" && i + 2 < argc) {
            g.resize_after_frame = std::atoi(argv[++i]);
            std::sscanf(argv[++i], "%fx%f", &g.resize_after_size.x, &g.resize_after_size.y);
        }
        else if (argument == "--resize-drag" && i + 3 < argc) {
            g.resize_drag_start = std::atoi(argv[++i]);
            g.resize_drag_frames = std::atoi(argv[++i]);
            std::sscanf(argv[++i], "%fx%f", &g.resize_drag_to.x, &g.resize_drag_to.y);
        }
    }

    options.zoom = zoom;

    std::string locale_error;
    if (!g_translator.LoadEmbedded(locale_error))
        std::fprintf(stderr, "i18n: %s\n", locale_error.c_str());
    if (!language_code.empty()) {
        if (!g_translator.SetLocale(language_code)) {
            std::fprintf(stderr, "unknown language: %s, falling back to en-us\n", language_code.c_str());
            g_translator.SetLocale("en-us");
        }
    } else {
        // No --lang: follow the system's UI language, with English for everything else.
        g_translator.SetLocale(sc2csv::SystemLanguage());
    }
    // Conversion errors come from the shared core; route them through the selected table as well.
    sc2csv::SetErrorTranslator([](std::string_view key) { return g_translator.Text(key); });
    g.language = int(g_translator.locale_index());

    return Run(options, [] {
        // Once the host's window exists: the title bar, taskbar button and Alt+Tab take the executable's icon.
        static bool icon_applied = false;
        if (!icon_applied) {
            ApplyWindowIcon();
            icon_applied = true;
        }
        // Warm the About panel's bitmap: its texture is created with the next frame, so loading it on the frame the
        // panel opens would draw a texture that does not exist yet. Once warmed, it stays in the loader's cache.
        (void)AppIcon();
        if (g.resize_after_frame >= 0) {
            static int frame = 0;
            if (frame++ == g.resize_after_frame) {
                if (GLFWwindow* window = glfwGetCurrentContext())
                    glfwSetWindowSize(window, int(g.resize_after_size.x), int(g.resize_after_size.y));
            }
        }
        if (g.resize_drag_start >= 0) {
            static int frame = 0;
            static int start_w = 0;
            static int start_h = 0;
            if (frame == g.resize_drag_start) {
                if (GLFWwindow* window = glfwGetCurrentContext())
                    glfwGetWindowSize(window, &start_w, &start_h);
            }
            if (frame >= g.resize_drag_start && frame < g.resize_drag_start + g.resize_drag_frames) {
                const float t = float(frame - g.resize_drag_start + 1) / float(g.resize_drag_frames);
                const int w = int(start_w + (g.resize_drag_to.x - start_w) * t);
                const int h = int(start_h + (g.resize_drag_to.y - start_h) * t);
                if (GLFWwindow* window = glfwGetCurrentContext())
                    glfwSetWindowSize(window, w, h);
            }
            ++frame;
        }
        if (!g.scanned) {
            Rescan(g);
            g.scanned = true;
        }
        ProcessJobs(g);
        RebuildVisible(g);
        VStack({.alignment = HorizontalAlignment::Leading, .spacing = 0.0f}, [&] {
            DrawMenuBar(g);
            Frame({.maxWidth = Infinity, .maxHeight = Infinity}, [&] { DrawNewUnpackPage(g); });
        });
        if (g.about_presented) {
            const std::string about_title = Tr("about.title");
            const std::string about_body = Tr("about.body");
            const std::string about_close = Tr("about.close");
            const AlertButton buttons[] = {{about_close.c_str(), ButtonRole::Default}};
            Alert(about_title.c_str(), &g.about_presented, buttons,
                  {.message = about_body.c_str(), .icon = DrawAboutIcon});
        }
        const std::string notice_title = Tr(g.notice_title_key);
        const std::string notice_body = Tr(g.notice_body_key);
        NotificationBanner(&g.notice_presented,
                           {.title = notice_title.c_str(),
                            .body = notice_body.c_str(),
                            .kind = NotificationKind::Warning,
                            .autoDismissAfter = 4.0f,
                            .placement = NotificationPlacement::BottomRight});
    });
}
