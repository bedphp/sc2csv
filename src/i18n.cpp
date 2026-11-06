#include "i18n.h"

#include "i18n_data.inc"

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace sc2csv {
namespace {

// A translation table keeps one entry per line, so a line break inside a text is written as \n (and a backslash as \\:
// the About panel's body uses it).
std::string Unescape(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\\' || index + 1 == text.size()) {
            result.push_back(text[index]);
            continue;
        }
        switch (text[++index]) {
            case 'n': result.push_back('\n'); break;
            case 't': result.push_back('\t'); break;
            case '\\': result.push_back('\\'); break;
            default:
                result.push_back('\\');
                result.push_back(text[index]);
                break;
        }
    }
    return result;
}

bool ParseRecord(std::string_view text, std::size_t& cursor, std::vector<std::string>& fields, std::string& error) {
    fields.clear();
    std::string field;
    bool quoted = false;
    while (cursor < text.size()) {
        const char ch = text[cursor++];
        if (quoted) {
            if (ch == '"') {
                if (cursor < text.size() && text[cursor] == '"') {
                    field.push_back('"');
                    ++cursor;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(ch);
            }
        } else if (ch == '"' && field.empty()) {
            quoted = true;
        } else if (ch == ',') {
            fields.push_back(std::move(field));
            field.clear();
        } else if (ch == '\n') {
            if (!field.empty() && field.back() == '\r') field.pop_back();
            fields.push_back(std::move(field));
            return true;
        } else {
            field.push_back(ch);
        }
    }
    if (quoted) {
        error = "unterminated quoted field";
        return false;
    }
    if (!field.empty() || !fields.empty()) fields.push_back(std::move(field));
    return !fields.empty();
}

} // namespace

std::span<const LanguageInfo> AvailableLanguages() {
    static const std::vector<LanguageInfo> languages = [] {
        std::vector<LanguageInfo> result;
        result.reserve(generated::kLanguageCount);
        for (const auto& source : generated::kLanguages)
            result.push_back({source.code, source.name});
        return result;
    }();
    return languages;
}

std::string SystemLanguage() {
#ifdef _WIN32
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_CHINESE: return "zh-cn";
        case LANG_JAPANESE: return "ja-jp";
        default: return "en-us";
    }
#else
    return "en-us";
#endif
}

// The three-line Supercell layout is irrelevant here; a table is $lang/$name/$order, an ID,Text header, then rows.
bool Translator::ParseTable(std::string_view source, Table& table, std::string& error) {
    table = Table{};
    if (source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB && static_cast<unsigned char>(source[2]) == 0xBF)
        source.remove_prefix(3);

    std::size_t cursor = 0;
    bool has_lang = false;
    bool has_header = false;
    std::vector<std::string> fields;
    while (cursor < source.size()) {
        if (!ParseRecord(source, cursor, fields, error))
            return false;
        if (fields.empty() || (fields.size() == 1 && fields[0].empty()))
            continue;
        if (!fields[0].empty() && fields[0][0] == '$') {
            const std::string& line = fields[0];
            if (line.rfind("$lang:", 0) == 0) {
                if (has_lang || line.size() <= 6) {
                    error = "invalid $lang";
                    return false;
                }
                table.code = line.substr(6);
                has_lang = true;
            } else if (line.rfind("$name:", 0) == 0) {
                table.name = line.substr(6);
                if (table.name.empty()) {
                    error = "invalid $name";
                    return false;
                }
            } else if (line.rfind("$order:", 0) == 0) {
                int order = 0;
                const std::string value = line.substr(7);
                const auto result = std::from_chars(value.data(), value.data() + value.size(), order);
                if (result.ec != std::errc() || result.ptr != value.data() + value.size()) {
                    error = "invalid $order";
                    return false;
                }
            } else {
                error = "unknown metadata";
                return false;
            }
            continue;
        }
        if (!has_header) {
            if (fields.size() != 2 || fields[0] != "ID" || fields[1] != "Text") {
                error = "missing ID,Text header";
                return false;
            }
            has_header = true;
            continue;
        }
        if (fields.size() != 2 || fields[0].empty()) {
            error = "invalid translation row";
            return false;
        }
        if (!table.values.emplace(fields[0], Unescape(fields[1])).second) {
            error = "duplicate translation ID";
            return false;
        }
    }
    if (!has_lang || !has_header) {
        error = "missing required metadata or header";
        return false;
    }
    if (table.name.empty())
        table.name = table.code;
    return true;
}

bool Translator::LoadEmbedded(std::string& error) {
    tables_.clear();
    error.clear();
    for (const auto& source : generated::kLanguages) {
        Table table;
        if (!ParseTable(source.csv, table, error)) {
            error = std::string(source.code) + ": " + error;
            tables_.clear();
            return false;
        }
        // The generated names are authoritative, so the data and the menu cannot drift apart.
        table.code = source.code;
        table.name = source.name;
        tables_.push_back(std::move(table));
    }
    if (tables_.empty()) {
        error = "no embedded language tables";
        return false;
    }

    // Missing keys fall back to Simplified Chinese when it is embedded, otherwise to the first language.
    fallback_ = 0;
    for (std::size_t i = 0; i < tables_.size(); ++i) {
        if (tables_[i].code == "zh-cn") {
            fallback_ = i;
            break;
        }
    }
    current_ = fallback_;
    return true;
}

bool Translator::SetLocale(std::string_view code) {
    for (std::size_t i = 0; i < tables_.size(); ++i) {
        if (tables_[i].code == code) {
            current_ = i;
            return true;
        }
    }
    return false;
}

void Translator::SetLocaleIndex(std::size_t index) {
    if (index < tables_.size())
        current_ = index;
}

std::string_view Translator::LocaleCode() const {
    if (current_ >= tables_.size())
        return {};
    return tables_[current_].code;
}

std::string_view Translator::LocaleName() const {
    if (current_ >= tables_.size())
        return {};
    return tables_[current_].name;
}

const std::string* Translator::Find(std::size_t index, std::string_view key) const {
    if (index >= tables_.size())
        return nullptr;
    const auto& values = tables_[index].values;
    const auto found = values.find(std::string(key));
    return found == values.end() ? nullptr : &found->second;
}

std::string_view Translator::Get(std::string_view key) const {
    if (const std::string* value = Find(current_, key))
        return *value;
    if (const std::string* value = Find(fallback_, key))
        return *value;
    return key;
}

std::string Translator::Text(std::string_view key) const { return std::string(Get(key)); }

} // namespace sc2csv
