#pragma once

// Translations, ported from borealis (brls/core/i18n) as used by Mara_nx.
// Strings live in Editor/i18n/<locale>/<file>.json (embedded in the exe) and are
// looked up as "<file>/<key>/<subkey>"_i18n. Missing strings fall back to the
// default locale, then to the string name itself.

#include <string>
#include <vector>

namespace i18n
{

inline const std::string LOCALE_EN_US = "en-US";
inline const std::string LOCALE_ES    = "es";
inline const std::string LOCALE_AUTO  = "auto";

inline const std::string LOCALE_DEFAULT = LOCALE_EN_US;

namespace internal
{
    struct Arg
    {
        enum class Kind { String, Int, Float } kind;
        std::string str;
        long long   i = 0;
        double      f = 0;

        Arg(const std::string& s) : kind(Kind::String), str(s) {}
        Arg(const char* s) : kind(Kind::String), str(s ? s : "") {}
        Arg(int v) : kind(Kind::Int), i(v) {}
        Arg(long v) : kind(Kind::Int), i(v) {}
        Arg(long long v) : kind(Kind::Int), i(v) {}
        Arg(unsigned v) : kind(Kind::Int), i(v) {}
        Arg(unsigned long v) : kind(Kind::Int), i(static_cast<long long>(v)) {}
        Arg(unsigned long long v) : kind(Kind::Int), i(static_cast<long long>(v)) {}
        Arg(float v) : kind(Kind::Float), f(v) {}
        Arg(double v) : kind(Kind::Float), f(v) {}
    };

    std::string getRawStr(const std::string& stringName);
    // fmt-style subset: {} / {N} with an optional spec [+][.precision][f|d], and {{ }}.
    std::string format(const std::string& rawStr, const std::vector<Arg>& args);
} // namespace internal

/**
 * Returns the translation for the given string,
 * after injecting format parameters (if any)
 */
template <typename... Args>
std::string getStr(const std::string& stringName, Args&&... args)
{
    return internal::format(internal::getRawStr(stringName), { internal::Arg(args)... });
}

/**
 * Loads the translations of the given locale + default locale.
 * LOCALE_AUTO picks the best match for the system languages.
 * Must be called before trying to get a translation!
 */
void loadTranslations(const std::string& locale = LOCALE_AUTO);

// Locale in use (never LOCALE_AUTO).
const std::string& getLocale();

// Locales with embedded translations, sorted.
std::vector<std::string> getAvailableLocales();

// Native name of a locale ("<file>/language_name" of that locale), else the locale code.
std::string getLocaleName(const std::string& locale);

inline namespace literals
{
    /**
     * Returns the translation for the given string, without
     * injecting any parameters
     * Shortcut to i18n::getStr(stringName)
     */
    std::string operator""_i18n(const char* str, size_t len);
} // namespace literals
} // namespace i18n
