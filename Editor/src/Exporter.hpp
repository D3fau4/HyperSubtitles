#pragma once

#include <string>

class Project;

// Same output as `lines.py export-subtitles`.
std::string BuildSubtitlesJson(const Project& project, size_t* count = nullptr);
