#include "Analysis.hpp"
#include "Preview.hpp"
#include "Project.hpp"
#include "Settings.hpp"
#include "../../shared/SubtitleText.hpp"

#include <algorithm>

LineWarnings Analysis::Compute(const Project& project, Preview& preview, const Settings& settings, size_t index) const
{
    LineWarnings w;
    const ojson& e = *project.lines[index].entry;
    const std::string translation = Project::Text(e);
    if (SubtitleText::CollapseSpaces(translation).empty())
        return w;  // only translations are checked
    const std::string text = Project::DisplayText(e);

    size_t i = 0, chars = 0;
    while (i < text.size())
    {
        const size_t start = i;
        const unsigned int cp = SubtitleText::NextCodepoint(text, i);
        if (cp != '\n')
            ++chars;
        if (!preview.FontCovers(cp))
        {
            const std::string ch = text.substr(start, i - start);
            if (w.unsupported.find(ch) == std::string::npos)
                w.unsupported += ch;
        }
    }

    const DialogueBoxCore::Layout l = preview.Measure(project.boxConfig, text, settings.previewWidth, settings.previewHeight);
    w.widened = l.widened;
    w.shrunk = l.shrunk;
    w.overflow = l.overflow;

    double shortest = 0;
    for (const AudioLang& lang : kAudioLangs)
    {
        if (!Project::HasAudio(e, lang.key))
            continue;
        const double d = Project::EffectiveDuration(e, lang.key);
        if (d > 0 && (shortest == 0 || d < shortest))
            shortest = d;
    }
    if (shortest > 0)
    {
        w.charsPerSecond = float(chars / shortest);
        w.tooFast = settings.maxCharsPerSecond > 0 && w.charsPerSecond > settings.maxCharsPerSecond;
    }
    return w;
}

std::string Analysis::SourceKey(const Project& project, size_t line) const
{
    return SubtitleText::CollapseSpaces(Project::SourceText(*project.lines[line].entry, "en"));
}

void Analysis::Update(Project& project, Preview& preview, const Settings& settings, uint64_t configRevision)
{
    if (project.Revision() == m_projectRevision && configRevision == m_configRevision &&
        project.LoadGeneration() == m_loadGeneration)
        return;

    const bool full = project.LoadGeneration() != m_loadGeneration || m_warnings.size() != project.lines.size() ||
                      configRevision != m_configRevision;
    std::vector<size_t> changed = project.TakeChangedLines();
    if (full)
    {
        // English text never changes in the editor, so the index is built once per load.
        m_bySource.clear();
        m_sourceKeys.assign(project.lines.size(), {});
        for (size_t i = 0; i < project.lines.size(); ++i)
        {
            m_sourceKeys[i] = SourceKey(project, i);
            if (!m_sourceKeys[i].empty())
                m_bySource[m_sourceKeys[i]].push_back(i);
        }
        m_warnings.assign(project.lines.size(), {});
        for (size_t i = 0; i < project.lines.size(); ++i)
            m_warnings[i] = Compute(project, preview, settings, i);
    }
    else
    {
        for (size_t i : changed)
            if (i < m_warnings.size())
                m_warnings[i] = Compute(project, preview, settings, i);
    }

    for (Counts& c : counts)
        c = {};
    for (size_t i = 0; i < project.lines.size(); ++i)
    {
        const ojson& e = *project.lines[i].entry;
        for (Counts* c : { &counts[project.lines[i].category], &counts[2] })
        {
            ++c->total;
            switch (Project::GetStatus(e))
            {
            case Status::Pending:    ++c->pending; break;
            case Status::Translated: ++c->translated; break;
            case Status::Reviewed:   ++c->reviewed; break;
            }
            if (m_warnings[i].Any())
                ++c->warnings;
        }
    }
    m_loadGeneration = project.LoadGeneration();
    m_projectRevision = project.Revision();
    m_configRevision = configRevision;
}

const std::vector<size_t>& Analysis::SameSource(const Project& project, size_t line) const
{
    static const std::vector<size_t> empty;
    if (line >= m_sourceKeys.size() || m_sourceKeys[line].empty())
        return empty;
    (void)project;
    auto it = m_bySource.find(m_sourceKeys[line]);
    return it != m_bySource.end() ? it->second : empty;
}
