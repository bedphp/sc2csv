#pragma once

#include <string>
#include <string_view>

namespace sc2csv {

// The conversion core reports failures as text. The applications install a translator here so those messages follow
// the selected interface language; without one, each message keeps the Chinese text it is written with.
using ErrorTranslator = std::string (*)(std::string_view key);

void SetErrorTranslator(ErrorTranslator translator);

// The translated text for key, or fallback when no translator is installed or the language has no such key.
std::string ErrorText(std::string_view key, std::string_view fallback);

// Same, with a printf-style fallback and arguments for the value inserted into it.
std::string ErrorFormat(std::string_view key, const char* fallback, ...);

} // namespace sc2csv
