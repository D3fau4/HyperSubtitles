#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_AudioStream;

// Decoded wave from SOUND.xwb.
struct Wave
{
    int channels = 0;
    int sampleRate = 0;
    std::vector<int16_t> samples;  // interleaved
    double Seconds() const { return channels && sampleRate ? double(samples.size()) / channels / sampleRate : 0.0; }
};

// Reads voices straight from the game's XACT banks (SOUND.xsb cue names +
// SOUND.xwb MS-ADPCM waves), like tools/extract/parse_xsb.py + extract_xwb.py.
class XactBank
{
public:
    // dir: folder with SOUND.xsb and SOUND.xwb (the game's data/ folder).
    bool Open(const std::string& dir, std::string& error);
    void Close();
    bool IsOpen() const { return m_open; }

    bool Has(const std::string& cue) const { return m_cues.count(cue) != 0; }
    bool Decode(const std::string& cue, Wave& out, std::string& error);
    size_t CueCount() const { return m_cues.size(); }

private:
    bool ParseXsb(const std::vector<uint8_t>& data, std::string& error);

    bool m_open = false;
    std::ifstream m_xwb;
    uint32_t m_numEntries = 0;
    uint32_t m_metaElemSize = 24;
    uint32_t m_metaBase = 0;
    uint32_t m_waveBase = 0;
    std::unordered_map<std::string, uint32_t> m_cues;  // cue name -> wave index (bank 0 = SOUND.xwb)
};

// MS-ADPCM block decoder (same as tools/transcribe/transcribe_wavs.py).
bool DecodeMsAdpcm(const uint8_t* data, size_t size, int channels, int blockAlign, std::vector<int16_t>& out);

// Plays one wave at a time through SDL.
class AudioPlayer
{
public:
    ~AudioPlayer();
    bool Play(const Wave& wave, float volume, std::string& error);
    void Stop();
    bool IsPlaying() const;
    // Seconds played of the current wave.
    double Position() const;
    double Length() const { return m_length; }
    const std::string& Tag() const { return m_tag; }
    void SetTag(const std::string& tag) { m_tag = tag; }
    void SetVolume(float volume);

private:
    SDL_AudioStream* m_stream = nullptr;
    double m_length = 0;
    int    m_bytesPerSecond = 0;
    std::string m_tag;
};
