#include "Exporter.hpp"
#include "Project.hpp"

#include <algorithm>
#include <vector>

std::string BuildSubtitlesJson(const Project& project, size_t* count)
{
    // lines.py: for category in (BATTLE, EVENT), for id in sorted ids, for lang in (ja, en)
    std::vector<const Line*> ordered;
    ordered.reserve(project.lines.size());
    for (const Line& l : project.lines)
        ordered.push_back(&l);
    std::stable_sort(ordered.begin(), ordered.end(), [](const Line* a, const Line* b) {
        return a->category != b->category ? a->category < b->category : a->id < b->id;
    });

    ojson subtitles = ojson::array();
    for (const Line* line : ordered)
    {
        const ojson& e = *line->entry;
        for (const AudioLang& lang : kAudioLangs)
        {
            auto audio = e.find(lang.key);
            if (audio == e.end() || !audio->is_object() || !audio->contains("duration"))
                continue;
            const std::string text = Project::DisplayText(e);
            if (text.empty())
                continue;
            ojson sub = ojson::object();
            sub["audioFile"] = std::string("/") + lang.folder + "/" + kCategories[line->category] + "/" + line->id + ".hca";
            sub["character"] = e.contains("character") ? e["character"] : ojson(-1);
            sub["text"] = text;
            sub["duration"] = audio->contains("displayDuration") ? (*audio)["displayDuration"] : (*audio)["duration"];
            subtitles.push_back(std::move(sub));
        }
    }
    if (count)
        *count = subtitles.size();
    ojson root = ojson::object();
    root["subtitles"] = std::move(subtitles);
    return root.dump(4, ' ', false, ojson::error_handler_t::replace) + "\n";
}
