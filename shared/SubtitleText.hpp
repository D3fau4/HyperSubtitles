#pragma once

// Text shown in game for a line. Must match display_text() in tools/lines/lines.py:
// the translation keeps its line breaks (whitespace inside each line collapsed,
// empty lines dropped); without a translation the English text is shown on one line.

#include <string>
#include <string_view>

namespace SubtitleText
{
    // Decodes one UTF-8 codepoint at s[i] and advances i. Invalid bytes decode as U+FFFD.
    inline unsigned int NextCodepoint(std::string_view s, size_t& i)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > s.size()) { ++i; return 0xFFFD; }
        unsigned int cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < len; ++k)
        {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { ++i; return 0xFFFD; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += len;
        return cp;
    }

    // Same set as Python's str.isspace().
    inline bool IsSpace(unsigned int cp)
    {
        return (cp >= 0x09 && cp <= 0x0D) || (cp >= 0x1C && cp <= 0x20) || cp == 0x85 || cp == 0xA0 ||
               cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 ||
               cp == 0x202F || cp == 0x205F || cp == 0x3000;
    }

    // " ".join(text.split())
    inline std::string CollapseSpaces(std::string_view text)
    {
        std::string out;
        bool pendingSpace = false;
        size_t i = 0;
        while (i < text.size())
        {
            const size_t start = i;
            const unsigned int cp = NextCodepoint(text, i);
            if (IsSpace(cp))
            {
                pendingSpace = !out.empty();
                continue;
            }
            if (pendingSpace)
                out += ' ';
            pendingSpace = false;
            out.append(text.substr(start, i - start));
        }
        return out;
    }

    inline std::string DisplayText(std::string_view translation, std::string_view english)
    {
        if (CollapseSpaces(translation).empty())
            return CollapseSpaces(english);

        std::string out;
        size_t pos = 0;
        while (pos <= translation.size())
        {
            size_t nl = translation.find('\n', pos);
            if (nl == std::string_view::npos)
                nl = translation.size();
            std::string line = CollapseSpaces(translation.substr(pos, nl - pos));
            if (!line.empty())
            {
                if (!out.empty())
                    out += '\n';
                out += line;
            }
            pos = nl + 1;
        }
        return out;
    }
}
