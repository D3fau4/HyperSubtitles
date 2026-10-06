#pragma once

// UTF-8 std::string <-> std::filesystem::path (C++20 made u8path/u8string use char8_t).

#include <filesystem>
#include <string>

inline std::filesystem::path U8Path(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

inline std::string U8String(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
