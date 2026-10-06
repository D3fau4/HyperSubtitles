#include "I18n.hpp"
#include "../../shared/json.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

// Editor/i18n/<locale>/<file>.json, embedded by CMake (cmake/embed_i18n.cmake)
struct EmbeddedI18nFile { const char* path; const unsigned char* data; unsigned size; };
extern const EmbeddedI18nFile g_i18nFiles[];
extern const unsigned g_i18nFileCount;

namespace i18n
{

static nlohmann::json defaultLocale = {};
static nlohmann::json currentLocale = {};
static std::string currentLocaleName = LOCALE_DEFAULT;

static void loadLocale(const std::string& locale, nlohmann::json* target)
{
    *target = nlohmann::json::object();
    if (locale.empty())
        return;
    const std::string prefix = locale + "/";
    bool found = false;
    for (unsigned i = 0; i < g_i18nFileCount; ++i)
    {
        const std::string path = g_i18nFiles[i].path;
        if (path.compare(0, prefix.size(), prefix) != 0 || path.size() < 5 || path.compare(path.size() - 5, 5, ".json") != 0)
            continue;
        const std::string name = path.substr(prefix.size(), path.size() - prefix.size() - 5);
        if (name.find('/') != std::string::npos)
            continue;
        found = true;

        const char* begin = reinterpret_cast<const char*>(g_i18nFiles[i].data);
        nlohmann::json strings = nlohmann::json::parse(begin, begin + g_i18nFiles[i].size, nullptr, false);
        if (strings.is_discarded())
        {
            SDL_Log("Error while loading \"i18n/%s\": invalid JSON", path.c_str());
            continue;
        }
        (*target)[name] = std::move(strings);
    }
    if (!found)
        SDL_Log("Cannot load locale %s: directory i18n/%s doesn't exist", locale.c_str(), locale.c_str());
}

std::vector<std::string> getAvailableLocales()
{
    std::set<std::string> locales;
    for (unsigned i = 0; i < g_i18nFileCount; ++i)
    {
        const std::string path = g_i18nFiles[i].path;
        const size_t slash = path.find('/');
        if (slash != std::string::npos)
            locales.insert(path.substr(0, slash));
    }
    return { locales.begin(), locales.end() };
}

// Best embedded locale for the system languages: exact "lang-COUNTRY", then "lang",
// then any "lang-*". Falls back to the default locale.
static std::string systemLocale()
{
    const std::vector<std::string> available = getAvailableLocales();
    auto has = [&](const std::string& l) { return std::find(available.begin(), available.end(), l) != available.end(); };

    std::string result = LOCALE_DEFAULT;
    int count = 0;
    SDL_Locale** preferred = SDL_GetPreferredLocales(&count);
    for (int i = 0; preferred && i < count; ++i)
    {
        const std::string lang = preferred[i]->language ? preferred[i]->language : "";
        const std::string country = preferred[i]->country ? preferred[i]->country : "";
        if (lang.empty())
            continue;
        if (!country.empty() && has(lang + "-" + country))
        {
            result = lang + "-" + country;
            break;
        }
        if (has(lang))
        {
            result = lang;
            break;
        }
        auto it = std::find_if(available.begin(), available.end(),
                               [&](const std::string& l) { return l.compare(0, lang.size() + 1, lang + "-") == 0; });
        if (it != available.end())
        {
            result = *it;
            break;
        }
    }
    SDL_free(preferred);
    return result;
}

void loadTranslations(const std::string& locale)
{
    loadLocale(LOCALE_DEFAULT, &defaultLocale);

    currentLocaleName = locale.empty() || locale == LOCALE_AUTO ? systemLocale() : locale;
    if (currentLocaleName != LOCALE_DEFAULT)
        loadLocale(currentLocaleName, &currentLocale);
    else
        currentLocale = nlohmann::json::object();
}

const std::string& getLocale()
{
    return currentLocaleName;
}

std::string getLocaleName(const std::string& locale)
{
    static std::map<std::string, std::string> cache;
    if (auto cached = cache.find(locale); cached != cache.end())
        return cached->second;
    std::string& name = cache[locale];
    name = locale;
    nlohmann::json strings;
    loadLocale(locale, &strings);
    for (auto it = strings.begin(); it != strings.end(); ++it)
        if (it->is_object() && it->contains("language_name") && (*it)["language_name"].is_string())
            return name = (*it)["language_name"].get<std::string>();
    return name;
}

namespace internal
{
    std::string getRawStr(const std::string& stringName)
    {
        nlohmann::json::json_pointer pointer;

        try
        {
            pointer = nlohmann::json::json_pointer("/" + stringName);
        }
        catch (const std::exception& e)
        {
            SDL_Log("Error while getting string \"%s\": %s", stringName.c_str(), e.what());
            return stringName;
        }

        // First look for translated string in current locale
        if (currentLocale.contains(pointer) && currentLocale.at(pointer).is_string())
            return currentLocale.at(pointer).get<std::string>();

        // Then look for default locale
        if (defaultLocale.contains(pointer) && defaultLocale.at(pointer).is_string())
            return defaultLocale.at(pointer).get<std::string>();

        // Fallback to returning the string name
        return stringName;
    }

    static std::string formatArg(const Arg& arg, const std::string& spec)
    {
        bool plus = false;
        int precision = -1;
        char type = 0;
        size_t i = 0;
        if (i < spec.size() && spec[i] == '+')
            plus = true, ++i;
        if (i < spec.size() && spec[i] == '.')
        {
            precision = 0;
            for (++i; i < spec.size() && spec[i] >= '0' && spec[i] <= '9'; ++i)
                precision = precision * 10 + (spec[i] - '0');
        }
        if (i < spec.size())
            type = spec[i];

        char buf[64];
        switch (arg.kind)
        {
        case Arg::Kind::String:
            return arg.str;
        case Arg::Kind::Int:
            if (type == 'f')
                std::snprintf(buf, sizeof buf, plus ? "%+.*f" : "%.*f", precision < 0 ? 6 : precision, double(arg.i));
            else
                std::snprintf(buf, sizeof buf, plus ? "%+lld" : "%lld", arg.i);
            return buf;
        case Arg::Kind::Float:
            if (precision < 0 && type != 'f')
                std::snprintf(buf, sizeof buf, plus ? "%+g" : "%g", arg.f);
            else
                std::snprintf(buf, sizeof buf, plus ? "%+.*f" : "%.*f", precision < 0 ? 6 : precision, arg.f);
            return buf;
        }
        return {};
    }

    std::string format(const std::string& rawStr, const std::vector<Arg>& args)
    {
        std::string out;
        size_t next = 0;
        for (size_t i = 0; i < rawStr.size(); ++i)
        {
            const char c = rawStr[i];
            if ((c == '{' || c == '}') && i + 1 < rawStr.size() && rawStr[i + 1] == c)
            {
                out += c;
                ++i;
                continue;
            }
            if (c != '{')
            {
                out += c;
                continue;
            }
            const size_t close = rawStr.find('}', i);
            if (close == std::string::npos)
            {
                SDL_Log("Invalid format \"%s\": unmatched '{'", rawStr.c_str());
                return rawStr;
            }
            const std::string field = rawStr.substr(i + 1, close - i - 1);
            const size_t colon = field.find(':');
            const std::string index = field.substr(0, colon);
            const std::string spec = colon == std::string::npos ? std::string() : field.substr(colon + 1);
            size_t n = next++;
            if (!index.empty())
            {
                n = 0;
                for (char d : index)
                {
                    if (d < '0' || d > '9')
                    {
                        SDL_Log("Invalid format \"%s\": bad argument index", rawStr.c_str());
                        return rawStr;
                    }
                    n = n * 10 + size_t(d - '0');
                }
            }
            if (n >= args.size())
            {
                SDL_Log("Invalid format \"%s\": missing argument %zu", rawStr.c_str(), n);
                return rawStr;
            }
            out += formatArg(args[n], spec);
            i = close;
        }
        return out;
    }
} // namespace internal

inline namespace literals
{
    std::string operator""_i18n(const char* str, size_t len)
    {
        return internal::getRawStr(std::string(str, len));
    }

} // namespace literals

} // namespace i18n
