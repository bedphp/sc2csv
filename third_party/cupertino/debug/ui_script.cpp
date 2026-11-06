#include "ui_script.hpp"

#include "ui_probe.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace Cupertino::debug {
namespace {

std::string Trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string Lower(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

std::string Unquote(const std::string& text) {
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::vector<std::string> SplitTokens(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    bool quoted = false;
    for (const char ch : text) {
        if (ch == '"') {
            quoted = !quoted;
        } else if (!quoted && (ch == ' ' || ch == '\t')) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

bool ParseFloat(const std::string& token, float& value) {
    if (token.empty()) return false;
    char* end = nullptr;
    const float parsed = std::strtof(token.c_str(), &end);
    if (end == token.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool ParseInt(const std::string& token, int& value) {
    if (token.empty()) return false;
    char* end = nullptr;
    const long parsed = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0' || parsed < 0 ||
        parsed > 0x7FFFFFFFL) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool ParseId(const std::string& token, ImGuiID& value) {
    if (token.empty()) return false;
    char* end = nullptr;
    const unsigned long parsed =
        std::strtoul(token.c_str(), &end, 0);
    if (end == token.c_str() || *end != '\0') return false;
    value = static_cast<ImGuiID>(parsed);
    return true;
}

bool KeyFromName(std::string name, ImGuiKey& key) {
    name = Lower(name);
    if (name.size() == 1) {
        const char ch = name[0];
        if (ch >= 'a' && ch <= 'z') {
            key = static_cast<ImGuiKey>(ImGuiKey_A + (ch - 'a'));
            return true;
        }
        if (ch >= '0' && ch <= '9') {
            key = static_cast<ImGuiKey>(ImGuiKey_0 + (ch - '0'));
            return true;
        }
    }
    struct Named {
        const char* name;
        ImGuiKey key;
    };
    static const Named kNames[] = {
        {"enter", ImGuiKey_Enter},       {"return", ImGuiKey_Enter},
        {"escape", ImGuiKey_Escape},     {"esc", ImGuiKey_Escape},
        {"tab", ImGuiKey_Tab},           {"space", ImGuiKey_Space},
        {"backspace", ImGuiKey_Backspace},
        {"delete", ImGuiKey_Delete},     {"del", ImGuiKey_Delete},
        {"insert", ImGuiKey_Insert},     {"up", ImGuiKey_UpArrow},
        {"down", ImGuiKey_DownArrow},    {"left", ImGuiKey_LeftArrow},
        {"right", ImGuiKey_RightArrow},  {"home", ImGuiKey_Home},
        {"end", ImGuiKey_End},           {"pageup", ImGuiKey_PageUp},
        {"pagedown", ImGuiKey_PageDown},
    };
    for (const Named& entry : kNames) {
        if (name == entry.name) {
            key = entry.key;
            return true;
        }
    }
    if (name.size() >= 2 && name[0] == 'f') {
        const int number = std::atoi(name.c_str() + 1);
        if (number >= 1 && number <= 12) {
            key = static_cast<ImGuiKey>(ImGuiKey_F1 + (number - 1));
            return true;
        }
    }
    return false;
}

struct Step {
    enum class Op {
        Wait,
        Move,
        Click,
        DoubleClick,
        RightClick,
        Drag,
        Scroll,
        Key,
        Text,
        Screenshot,
        DumpItems,
        ClickItem,
        ClickId,
        Quit,
    };

    Op op = Op::Quit;
    int line = 0;
    float x = 0.0f;
    float y = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    int frames = 0;
    int index = 0;
    ImGuiID id = 0;
    ImGuiKey key = ImGuiKey_None;
    std::vector<ImGuiKey> mods;
    std::string text;
};

bool ParseKeySpec(const std::string& spec, Step& step, std::string* error) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= spec.size()) {
        const size_t plus = spec.find('+', start);
        const size_t end =
            plus == std::string::npos ? spec.size() : plus;
        parts.push_back(Trim(spec.substr(start, end - start)));
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    step.key = ImGuiKey_None;
    step.mods.clear();
    for (const std::string& part : parts) {
        const std::string name = Lower(part);
        if (name.empty()) continue;
        if (name == "ctrl" || name == "control") {
            step.mods.push_back(ImGuiMod_Ctrl);
        } else if (name == "shift") {
            step.mods.push_back(ImGuiMod_Shift);
        } else if (name == "alt") {
            step.mods.push_back(ImGuiMod_Alt);
        } else if (name == "super" || name == "cmd" || name == "win") {
            step.mods.push_back(ImGuiMod_Super);
        } else if (step.key == ImGuiKey_None &&
                   KeyFromName(name, step.key)) {
            // Key found.
        } else {
            if (error) {
                *error = "unknown key '" + part + "' in '" + spec + "'";
            }
            return false;
        }
    }
    if (step.key == ImGuiKey_None) {
        if (error) *error = "missing key in '" + spec + "'";
        return false;
    }
    return true;
}

bool ParseStep(const std::string& raw, int line, Step& step,
               std::string* error) {
    const std::string trimmed = Trim(raw);
    if (trimmed.empty() || trimmed[0] == '#') return false;

    size_t command_end = 0;
    while (command_end < trimmed.size() &&
           !std::isspace(static_cast<unsigned char>(trimmed[command_end]))) {
        ++command_end;
    }
    const std::string command =
        Lower(trimmed.substr(0, command_end));
    const std::string rest =
        command_end < trimmed.size()
            ? Trim(trimmed.substr(command_end + 1))
            : std::string();
    const std::vector<std::string> tokens = SplitTokens(rest);

    step = Step{};
    step.line = line;

    const auto fail = [&](const std::string& message) {
        if (error) {
            *error = "line " + std::to_string(line) + ": " + message;
        }
        return false;
    };
    const auto need_floats = [&](size_t count) {
        if (tokens.size() != count) {
            return fail("'" + command + "' expects " +
                        std::to_string(count) + " number(s)");
        }
        return true;
    };

    if (command == "wait") {
        step.op = Step::Op::Wait;
        if (tokens.size() != 1 || !ParseInt(tokens[0], step.frames)) {
            return fail("'wait' expects a frame count");
        }
    } else if (command == "drag") {
        if (!need_floats(4)) return false;
        step.op = Step::Op::Drag;
        if (!ParseFloat(tokens[0], step.x) ||
            !ParseFloat(tokens[1], step.y) ||
            !ParseFloat(tokens[2], step.x2) ||
            !ParseFloat(tokens[3], step.y2)) {
            return fail("invalid drag coordinates");
        }
    } else if (command == "move" || command == "click" ||
               command == "doubleclick" || command == "rightclick") {
        if (!need_floats(2)) return false;
        if (command == "move") step.op = Step::Op::Move;
        else if (command == "click") step.op = Step::Op::Click;
        else if (command == "doubleclick") step.op = Step::Op::DoubleClick;
        else step.op = Step::Op::RightClick;
        if (!ParseFloat(tokens[0], step.x) ||
            !ParseFloat(tokens[1], step.y)) {
            return fail("invalid coordinates");
        }
    } else if (command == "scroll") {
        if (!need_floats(2)) return false;
        step.op = Step::Op::Scroll;
        if (!ParseFloat(tokens[0], step.x) ||
            !ParseFloat(tokens[1], step.y)) {
            return fail("invalid scroll delta");
        }
    } else if (command == "key") {
        step.op = Step::Op::Key;
        std::string spec = rest;
        if (!ParseKeySpec(spec, step, error)) {
            if (error) *error = "line " + std::to_string(line) + ": " + *error;
            return false;
        }
    } else if (command == "text") {
        step.op = Step::Op::Text;
        step.text = rest;
    } else if (command == "screenshot" || command == "dump_items") {
        if (rest.empty()) return fail("'" + command + "' needs a path");
        step.op = command == "screenshot" ? Step::Op::Screenshot
                                          : Step::Op::DumpItems;
        step.text = Unquote(rest);
    } else if (command == "click_item") {
        if (tokens.size() != 2 || !ParseInt(tokens[1], step.index)) {
            return fail("'click_item' expects \"window\" index");
        }
        step.op = Step::Op::ClickItem;
        step.text = tokens[0];
    } else if (command == "click_id") {
        if (tokens.size() != 1 || !ParseId(tokens[0], step.id)) {
            return fail("'click_id' expects a hex id");
        }
        step.op = Step::Op::ClickId;
    } else if (command == "quit") {
        if (!tokens.empty()) return fail("'quit' takes no arguments");
        step.op = Step::Op::Quit;
    } else {
        return fail("unknown command '" + command + "'");
    }
    if (error) error->clear();
    return true;
}

}  // namespace

struct UiScript::Impl {
    std::vector<Step> steps;
    size_t index = 0;
    int phase = 0;
    int cooldown = 0;
    int startup = 3;
    bool loaded = false;
    bool finished = false;
    bool failed = false;
    bool quit = false;
    bool pending_shot = false;
    std::filesystem::path shot_path;
    bool has_mouse = false;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    std::vector<ImGuiKey> active_mods;

    void inject(ImGuiIO& io) {
        if (has_mouse) {
            io.AddMousePosEvent(mouse_x, mouse_y);
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        }
        // The GLFW backend re-sends the real modifier state every frame, so
        // re-assert ours after it (tick runs after backend NewFrame).
        for (const ImGuiKey mod : active_mods) {
            io.AddKeyEvent(mod, true);
        }
    }

    void advance(int wait_frames) {
        ++index;
        phase = 0;
        cooldown = wait_frames;
    }

    void fail(const Step& step, const std::string& message) {
        std::fprintf(stderr, "[ui-script] line %d: %s\n", step.line,
                     message.c_str());
        failed = true;
        finished = true;
        quit = true;
        index = steps.size();
    }

    void log_step(const Step& step, const char* what) {
        std::fprintf(stderr, "[ui-script] line %d: %s\n", step.line, what);
    }

    bool resolve_item(const Step& step, float& x, float& y) {
        const std::vector<UiProbeItem>& items = ui_probe_items();
        if (step.op == Step::Op::ClickId) {
            for (const UiProbeItem& item : items) {
                if (item.id != step.id) continue;
                if (item.disabled) {
                    fail(step, "the target item is disabled");
                    return false;
                }
                x = (item.min_x + item.max_x) * 0.5f;
                y = (item.min_y + item.max_y) * 0.5f;
                return true;
            }
            fail(step, "no item with the given id was recorded");
            return false;
        }
        const std::string needle = Lower(step.text);
        bool exact_window = false;
        for (const UiProbeItem& item : items) {
            if (Lower(item.window) == needle) {
                exact_window = true;
                break;
            }
        }
        int seen = 0;
        for (const UiProbeItem& item : items) {
            const std::string window = Lower(item.window);
            if (exact_window ? window != needle
                             : window.find(needle) == std::string::npos) {
                continue;
            }
            if (seen++ != step.index) continue;
            if (item.disabled) {
                fail(step, "the target item is disabled");
                return false;
            }
            x = (item.min_x + item.max_x) * 0.5f;
            y = (item.min_y + item.max_y) * 0.5f;
            return true;
        }
        fail(step, "no item #" + std::to_string(step.index) +
                       " in a window matching \"" + step.text + "\"");
        return false;
    }

    bool tick_click(ImGuiIO& io, const Step& step) {
        if (phase == 0) {
            if (step.op == Step::Op::ClickItem ||
                step.op == Step::Op::ClickId) {
                if (!resolve_item(step, mouse_x, mouse_y)) return true;
            } else {
                mouse_x = step.x;
                mouse_y = step.y;
            }
            has_mouse = true;
            io.AddMousePosEvent(mouse_x, mouse_y);
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            phase = 1;
            return true;
        }
        if (step.op == Step::Op::DoubleClick) {
            if (phase == 1) {
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                phase = 2;
            } else if (phase == 2) {
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                phase = 3;
            } else if (phase == 3) {
                io.AddMousePosEvent(mouse_x, mouse_y);
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                phase = 4;
            } else {
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                advance(1);
            }
            return true;
        }
        const int button = step.op == Step::Op::RightClick
                               ? ImGuiMouseButton_Right
                               : ImGuiMouseButton_Left;
        if (phase == 1) {
            io.AddMouseButtonEvent(button, true);
            phase = 2;
        } else {
            io.AddMouseButtonEvent(button, false);
            advance(1);
        }
        return true;
    }

    bool tick_drag(ImGuiIO& io, const Step& step) {
        if (phase == 0) {
            mouse_x = step.x;
            mouse_y = step.y;
            has_mouse = true;
            io.AddMousePosEvent(mouse_x, mouse_y);
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            phase = 1;
            return true;
        }
        if (phase == 1) {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            phase = 2;
            return true;
        }
        constexpr int kMoveSteps = 8;
        if (phase <= 1 + kMoveSteps) {
            const float t =
                static_cast<float>(phase - 1) / kMoveSteps;
            mouse_x = step.x + (step.x2 - step.x) * t;
            mouse_y = step.y + (step.y2 - step.y) * t;
            io.AddMousePosEvent(mouse_x, mouse_y);
            ++phase;
            return true;
        }
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        advance(1);
        return true;
    }

    bool tick_key(ImGuiIO& io, const Step& step) {
        if (phase == 0) {
            active_mods = step.mods;
            for (const ImGuiKey mod : active_mods) {
                io.AddKeyEvent(mod, true);
            }
            phase = 1;
            if (active_mods.empty()) {
                io.AddKeyEvent(step.key, true);
                phase = 2;
            }
            return true;
        }
        if (phase == 1) {
            io.AddKeyEvent(step.key, true);
            phase = 2;
            return true;
        }
        if (phase == 2) {
            io.AddKeyEvent(step.key, false);
            phase = 3;
            return true;
        }
        for (const ImGuiKey mod : active_mods) {
            io.AddKeyEvent(mod, false);
        }
        active_mods.clear();
        advance(1);
        return true;
    }

    void tick(ImGuiIO& io) {
        if (!loaded || finished) return;
        inject(io);
        if (startup > 0) {
            --startup;
            return;
        }
        if (cooldown > 0) {
            --cooldown;
            return;
        }
        while (index < steps.size()) {
            const Step step = steps[index];
            switch (step.op) {
                case Step::Op::Wait:
                    advance(step.frames);
                    return;
                case Step::Op::Move:
                    mouse_x = step.x;
                    mouse_y = step.y;
                    has_mouse = true;
                    io.AddMousePosEvent(mouse_x, mouse_y);
                    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                    advance(1);
                    return;
                case Step::Op::Click:
                case Step::Op::DoubleClick:
                case Step::Op::RightClick:
                case Step::Op::ClickItem:
                case Step::Op::ClickId:
                    if (tick_click(io, step)) return;
                    break;
                case Step::Op::Drag:
                    if (tick_drag(io, step)) return;
                    break;
                case Step::Op::Scroll:
                    io.AddMouseWheelEvent(step.x, step.y);
                    advance(1);
                    return;
                case Step::Op::Key:
                    if (tick_key(io, step)) return;
                    break;
                case Step::Op::Text:
                    io.AddInputCharactersUTF8(step.text.c_str());
                    advance(1);
                    return;
                case Step::Op::Screenshot:
                    if (!pending_shot) {
                        pending_shot = true;
                        shot_path = std::filesystem::path(step.text);
                        log_step(step, ("screenshot " + step.text).c_str());
                    }
                    return;
                case Step::Op::DumpItems: {
                    std::string dump_error;
                    if (!dump_ui_probe_items(
                            std::filesystem::path(step.text), &dump_error)) {
                        fail(step, dump_error);
                        return;
                    }
                    std::fprintf(stderr,
                                 "[ui-script] line %d: dumped items to %s\n",
                                 step.line, step.text.c_str());
                    advance(1);
                    return;
                }
                case Step::Op::Quit:
                    log_step(step, "quit");
                    finished = true;
                    quit = true;
                    index = steps.size();
                    return;
            }
        }
        finished = true;
        quit = true;
    }

    void notify_screenshot(bool ok) {
        if (!pending_shot) return;
        pending_shot = false;
        const Step& step = steps[index];
        if (!ok) {
            fail(step, "screenshot failed");
            return;
        }
        advance(1);
    }
};

UiScript::UiScript() : impl_(std::make_unique<Impl>()) {}

UiScript::~UiScript() = default;

bool UiScript::load(const std::filesystem::path& path, std::string* error) {
    std::ifstream input(path);
    if (!input) {
        if (error) *error = "cannot open " + path.string();
        return false;
    }
    impl_->steps.clear();
    impl_->index = 0;
    impl_->phase = 0;
    impl_->cooldown = 0;
    impl_->startup = 3;
    impl_->finished = false;
    impl_->failed = false;
    impl_->quit = false;
    impl_->pending_shot = false;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        Step step;
        std::string line_error;
        if (!ParseStep(line, line_number, step, &line_error)) {
            if (!line_error.empty()) {
                if (error) {
                    *error = path.string() + ": " + line_error;
                }
                return false;
            }
            continue;
        }
        impl_->steps.push_back(std::move(step));
    }
    impl_->loaded = true;
    if (impl_->steps.empty()) {
        if (error) *error = path.string() + ": the script is empty";
        impl_->loaded = false;
        return false;
    }
    if (error) error->clear();
    std::fprintf(stderr, "[ui-script] loaded %zu command(s) from %s\n",
                 impl_->steps.size(), path.string().c_str());
    return true;
}

bool UiScript::active() const {
    return impl_->loaded && !impl_->finished;
}

void UiScript::tick(ImGuiIO& io) {
    impl_->tick(io);
}

std::optional<std::filesystem::path> UiScript::pending_screenshot() const {
    if (!impl_->pending_shot) return std::nullopt;
    return impl_->shot_path;
}

void UiScript::notify_screenshot(bool ok) {
    impl_->notify_screenshot(ok);
}

bool UiScript::quit_requested() const {
    return impl_->quit;
}

int UiScript::exit_code() const {
    return impl_->failed ? 3 : 0;
}

}  // namespace Cupertino::debug
