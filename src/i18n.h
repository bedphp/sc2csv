#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sc2csv {

// One language compiled into the binary: the code used for selection ("zh-cn") and the name shown in menus ("中文").
struct LanguageInfo {
    std::string_view code;
    std::string_view name;
};

// The languages built into this binary, in menu order (the $order line of each CSV under src/i18n).
std::span<const LanguageInfo> AvailableLanguages();

class Translator {
public:
    // Parses every embedded table. Call once before SetLocale/Get.
    bool LoadEmbedded(std::string& error);

    // Selects a language by code. Returns false and keeps the current one when the code is not embedded.
    bool SetLocale(std::string_view code);
    void SetLocaleIndex(std::size_t index);

    std::size_t locale_index() const { return current_; }
    std::string_view LocaleCode() const;
    std::string_view LocaleName() const;

    // Looks a key up in the current language, then in the fallback (zh-cn when embedded), then returns the key.
    std::string_view Get(std::string_view key) const;
    std::string Text(std::string_view key) const;

private:
    struct Table {
        std::string code;
        std::string name;
        std::unordered_map<std::string, std::string> values;
    };

    static bool ParseTable(std::string_view source, Table& table, std::string& error);
    const std::string* Find(std::size_t index, std::string_view key) const;

    std::vector<Table> tables_;
    std::size_t current_ = 0;
    std::size_t fallback_ = 0;
};

} // namespace sc2csv
