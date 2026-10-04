#include "XactAudio.hpp"
#include "Paths.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

static uint16_t U16(const std::vector<uint8_t>& d, size_t o) { return o + 2 <= d.size() ? uint16_t(d[o] | d[o + 1] << 8) : 0; }
static uint32_t U32(const std::vector<uint8_t>& d, size_t o)
{
    return o + 4 <= d.size() ? uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24) : 0;
}
static uint32_t U32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }

// ---------------------------------------------------------------------------
// SOUND.xsb
// ---------------------------------------------------------------------------

bool XactBank::ParseXsb(const std::vector<uint8_t>& data, std::string& error)
{
    if (data.size() < 0x30)
    {
        error = "SOUND.xsb too small";
        return false;
    }
    const uint32_t simpleOff   = U32(data, 0x22);
    const uint32_t complexOff  = U32(data, 0x26);
    const uint16_t numSimple   = U16(data, 0x13);
    const uint16_t numComplex  = U16(data, 0x15);
    const uint32_t cueNamesOff = U32(data, 0x2A);

    // Cue names: NUL-terminated, simple cues first, then complex ones.
    std::vector<std::string> names;
    size_t off = cueNamesOff;
    while (off < data.size() && names.size() < size_t(numSimple) + numComplex)
    {
        const auto end = std::find(data.begin() + off, data.end(), uint8_t(0));
        names.emplace_back(data.begin() + off, end);
        off = size_t(end - data.begin()) + 1;
    }

    // Sound entry: byte[7] = length; bank/wave at 11/9 (19, 23 bytes) or 41/39 (47 bytes).
    auto addCue = [&](size_t nameIndex, uint32_t soundRef) {
        if (nameIndex >= names.size() || size_t(soundRef) + 42 > data.size())
            return;
        const uint8_t len = data[soundRef + 7];
        int bank;
        uint16_t wave;
        if (len == 19 || len == 23) { bank = data[soundRef + 11]; wave = U16(data, soundRef + 9); }
        else if (len == 47)         { bank = data[soundRef + 41]; wave = U16(data, soundRef + 39); }
        else return;
        if (bank == 0)
            m_cues.emplace(names[nameIndex], wave);
    };
    for (size_t i = 0; i < numSimple; ++i)
        addCue(i, U32(data, simpleOff + i * 5 + 1));
    for (size_t i = 0; i < numComplex; ++i)
        addCue(numSimple + i, U32(data, complexOff + i * 15 + 1));

    if (m_cues.empty())
    {
        error = "no cues found in SOUND.xsb";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SOUND.xwb
// ---------------------------------------------------------------------------

bool XactBank::Open(const std::string& dir, std::string& error)
{
    Close();
    const fs::path base = U8Path(dir);
    std::vector<uint8_t> xsb;
    {
        std::ifstream f(base / "SOUND.xsb", std::ios::binary);
        if (!f)
        {
            error = "SOUND.xsb not found in " + dir;
            return false;
        }
        xsb.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    if (!ParseXsb(xsb, error))
        return false;

    m_xwb.open(base / "SOUND.xwb", std::ios::binary);
    if (!m_xwb)
    {
        error = "SOUND.xwb not found in " + dir;
        m_cues.clear();
        return false;
    }
    uint8_t hdr[52] = {};
    m_xwb.read(reinterpret_cast<char*>(hdr), sizeof hdr);
    if (!m_xwb || std::memcmp(hdr, "WBND", 4) != 0)
    {
        error = "SOUND.xwb: not an XACT wave bank";
        Close();
        return false;
    }
    uint32_t segOff[5];
    for (int i = 0; i < 5; ++i)
        segOff[i] = U32(hdr + 12 + i * 8);

    uint8_t bankData[96] = {};
    m_xwb.seekg(segOff[0]);
    m_xwb.read(reinterpret_cast<char*>(bankData), sizeof bankData);
    m_numEntries   = U32(bankData + 4);
    m_metaElemSize = U32(bankData + 72);
    if (m_metaElemSize < 16)
        m_metaElemSize = 24;
    m_metaBase = segOff[1];
    m_waveBase = segOff[4];
    m_open = static_cast<bool>(m_xwb);
    if (!m_open)
    {
        error = "SOUND.xwb: truncated header";
        Close();
    }
    return m_open;
}

void XactBank::Close()
{
    m_open = false;
    m_cues.clear();
    if (m_xwb.is_open())
        m_xwb.close();
    m_xwb.clear();
}

bool XactBank::Decode(const std::string& cue, Wave& out, std::string& error)
{
    auto it = m_cues.find(cue);
    if (!m_open || it == m_cues.end())
    {
        error = "cue " + cue + " not found";
        return false;
    }
    const uint32_t index = it->second;
    if (index >= m_numEntries)
    {
        error = "wave index out of range";
        return false;
    }

    std::vector<uint8_t> meta(m_metaElemSize);
    m_xwb.clear();
    m_xwb.seekg(std::streamoff(m_metaBase) + std::streamoff(index) * m_metaElemSize);
    m_xwb.read(reinterpret_cast<char*>(meta.data()), meta.size());
    if (!m_xwb)
    {
        error = "SOUND.xwb: cannot read entry";
        return false;
    }
    const uint32_t fmt     = U32(meta.data() + 4);
    const uint32_t playOff = U32(meta.data() + 8);
    const uint32_t playLen = U32(meta.data() + 12);
    const int tag      = fmt & 0x3;
    const int channels = (fmt >> 2) & 0x7;
    const int rate     = (fmt >> 5) & 0x3FFFF;
    const int blkRaw   = (fmt >> 23) & 0xFF;
    const int bits16   = (fmt >> 31) & 0x1;

    std::vector<uint8_t> data(playLen);
    m_xwb.seekg(std::streamoff(m_waveBase) + playOff);
    m_xwb.read(reinterpret_cast<char*>(data.data()), data.size());
    if (!m_xwb)
    {
        error = "SOUND.xwb: cannot read wave data";
        return false;
    }

    out = Wave{};
    out.channels = channels;
    out.sampleRate = rate;
    if (channels <= 0 || rate <= 0)
    {
        error = "invalid wave format";
        return false;
    }
    if (tag == 2)  // MS-ADPCM
    {
        if (!DecodeMsAdpcm(data.data(), data.size(), channels, (blkRaw + 22) * channels, out.samples))
        {
            error = "MS-ADPCM decode failed";
            return false;
        }
        return true;
    }
    if (tag == 0)  // PCM
    {
        if (bits16)
        {
            out.samples.resize(data.size() / 2);
            for (size_t i = 0; i < out.samples.size(); ++i)
                out.samples[i] = int16_t(data[i * 2] | data[i * 2 + 1] << 8);
        }
        else
        {
            out.samples.resize(data.size());
            for (size_t i = 0; i < data.size(); ++i)
                out.samples[i] = int16_t((int(data[i]) - 128) << 8);
        }
        return true;
    }
    error = "unsupported wave format (XMA/WMA)";
    return false;
}

// ---------------------------------------------------------------------------
// MS-ADPCM
// ---------------------------------------------------------------------------

static const int kAdaptation[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };
static const int kCoef[7][2] = { { 256, 0 }, { 512, -256 }, { 0, 0 }, { 192, 64 }, { 240, 0 }, { 460, -208 }, { 392, -232 } };

namespace
{
    // 64-bit like Python's ints: delta is not bounded by the format.
    struct AdpcmState { int64_t coef1, coef2, delta, s1, s2; };

    int16_t DecodeNibble(int nibble, AdpcmState& st)
    {
        const int64_t signedNibble = (nibble & 0x08) ? nibble - 16 : nibble;
        // Python's // floors; >> on a negative int is floor division too.
        const int64_t prediction = (st.s1 * st.coef1 + st.s2 * st.coef2) >> 8;
        const int64_t sample = std::clamp<int64_t>(prediction + signedNibble * st.delta, -32768, 32767);
        st.s2 = st.s1;
        st.s1 = sample;
        st.delta = std::max<int64_t>(16, (kAdaptation[nibble] * st.delta) >> 8);
        return int16_t(sample);
    }
}

bool DecodeMsAdpcm(const uint8_t* data, size_t size, int channels, int blockAlign, std::vector<int16_t>& out)
{
    if (channels < 1 || channels > 2 || blockAlign <= 7 * channels)
        return false;
    const int samplesPerBlock = ((blockAlign - 7 * channels) * 2) / channels + 2;
    out.clear();
    out.reserve(size / blockAlign * samplesPerBlock * channels + samplesPerBlock * channels);

    for (size_t off = 0; off < size; off += blockAlign)
    {
        const size_t len = std::min<size_t>(blockAlign, size - off);
        const uint8_t* b = data + off;
        if (len < size_t(7 * channels))
            break;

        AdpcmState st[2] = {};
        size_t p = 0;
        for (int c = 0; c < channels; ++c)
        {
            const int pred = b[p++];
            st[c].coef1 = kCoef[pred < 7 ? pred : 0][0];
            st[c].coef2 = kCoef[pred < 7 ? pred : 0][1];
        }
        auto s16 = [&](size_t at) { return int(int16_t(b[at] | b[at + 1] << 8)); };
        for (int c = 0; c < channels; ++c, p += 2) st[c].delta = std::max(16, std::abs(s16(p)));  // int -> int64
        for (int c = 0; c < channels; ++c, p += 2) st[c].s1 = s16(p);
        for (int c = 0; c < channels; ++c, p += 2) st[c].s2 = s16(p);

        // The two header samples come first: sample2, then sample1.
        for (int c = 0; c < channels; ++c) out.push_back(int16_t(st[c].s2));
        for (int c = 0; c < channels; ++c) out.push_back(int16_t(st[c].s1));

        int produced = 2;
        if (channels == 1)
        {
            for (; p < len && produced < samplesPerBlock; ++p)
            {
                out.push_back(DecodeNibble(b[p] >> 4, st[0]));
                if (++produced < samplesPerBlock)
                {
                    out.push_back(DecodeNibble(b[p] & 0x0F, st[0]));
                    ++produced;
                }
            }
        }
        else
        {
            for (; p < len && produced < samplesPerBlock; ++p, ++produced)
            {
                out.push_back(DecodeNibble(b[p] >> 4, st[0]));
                out.push_back(DecodeNibble(b[p] & 0x0F, st[1]));
            }
        }
    }
    return !out.empty();
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

AudioPlayer::~AudioPlayer()
{
    Stop();
}

bool AudioPlayer::Play(const Wave& wave, float volume, std::string& error)
{
    Stop();
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        error = SDL_GetError();
        return false;
    }
    SDL_AudioSpec spec{ SDL_AUDIO_S16LE, wave.channels, wave.sampleRate };
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!m_stream)
    {
        error = SDL_GetError();
        return false;
    }
    SDL_SetAudioStreamGain(m_stream, volume);
    SDL_PutAudioStreamData(m_stream, wave.samples.data(), int(wave.samples.size() * sizeof(int16_t)));
    SDL_FlushAudioStream(m_stream);
    SDL_ResumeAudioStreamDevice(m_stream);
    m_length = wave.Seconds();
    m_bytesPerSecond = wave.channels * wave.sampleRate * int(sizeof(int16_t));
    return true;
}

void AudioPlayer::Stop()
{
    if (m_stream)
    {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    m_length = 0;
    m_tag.clear();
}

bool AudioPlayer::IsPlaying() const
{
    return m_stream && SDL_GetAudioStreamQueued(m_stream) > 0;
}

double AudioPlayer::Position() const
{
    if (!m_stream || !m_bytesPerSecond)
        return 0.0;
    const double remaining = double(SDL_GetAudioStreamQueued(m_stream)) / m_bytesPerSecond;
    return std::max(0.0, m_length - remaining);
}

void AudioPlayer::SetVolume(float volume)
{
    if (m_stream)
        SDL_SetAudioStreamGain(m_stream, volume);
}
