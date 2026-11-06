#include "error_text.h"

#include <cstdarg>
#include <cstdio>

namespace sc2csv {
namespace {

ErrorTranslator g_translator = nullptr;

} // namespace

void SetErrorTranslator(ErrorTranslator translator) {
    g_translator = translator;
}

std::string ErrorText(std::string_view key, std::string_view fallback) {
    if (!g_translator)
        return std::string(fallback);
    std::string text = g_translator(key);
    // Translator::Text returns the key itself when the language does not have it.
    if (text.empty() || text == key)
        return std::string(fallback);
    return text;
}

std::string ErrorFormat(std::string_view key, const char* fallback, ...) {
    const std::string format = ErrorText(key, fallback);
    char buffer[256];
    va_list args;
    va_start(args, fallback);
    const int needed = std::vsnprintf(buffer, sizeof(buffer), format.c_str(), args);
    va_end(args);
    if (needed < 0)
        return format;
    if (std::size_t(needed) < sizeof(buffer))
        return std::string(buffer, std::size_t(needed));

    std::string result(std::size_t(needed), '\0');
    va_start(args, fallback);
    std::vsnprintf(result.data(), result.size() + 1, format.c_str(), args);
    va_end(args);
    return result;
}

} // namespace sc2csv
