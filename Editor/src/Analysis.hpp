#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Project;
class Preview;
struct Settings;

// Automatic warnings for a translated line.
struct LineWarnings
{
    std::string unsupported;  // characters the game font does not have (UTF-8, unique)
    bool  widened = false;    // the box grows to the screen width
    bool  shrunk = false;     // the font gets smaller
    bool  overflow = false;   // still too many lines at the smallest font
    float charsPerSecond = 0;
    bool  tooFast = false;
    bool Any() const { return !unsupported.empty() || widened || shrunk || overflow || tooFast; }
};

// Warnings for every line + translation memory, recomputed when the project,
// the box config, the font or the preview resolution change.
class Analysis
{
public:
    void Update(Project& project, Preview& preview, const Settings& settings, uint64_t configRevision);
    const LineWarnings& Warnings(size_t line) const { return m_warnings[line]; }
    LineWarnings Compute(const Project& project, Preview& preview, const Settings& settings, size_t line) const;

    // Lines whose English text matches line's English text (excluding itself).
    const std::vector<size_t>& SameSource(const Project& project, size_t line) const;
    std::string SourceKey(const Project& project, size_t line) const;

    struct Counts { int total = 0, pending = 0, translated = 0, reviewed = 0, warnings = 0; };
    Counts counts[3];  // BATTLE, EVENT, all

private:
    uint64_t m_projectRevision = ~0ull;
    uint64_t m_configRevision = ~0ull;
    std::vector<LineWarnings> m_warnings;
    std::unordered_map<std::string, std::vector<size_t>> m_bySource;
    std::vector<std::string> m_sourceKeys;
    uint64_t m_loadGeneration = ~0ull;
};
