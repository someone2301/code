// Offline renderer: runs kv::Tune over a mono WAV (16/24-bit PCM) and writes a 24-bit mono WAV, latency removed.
// usage: render in.wav out.wav quality speedMs humanizePct key scale [range]
//   quality 0 Tracking, 1 High quality; key 0 = C .. 11 = B; scale index as in Params (0 Chromatic, 1 Major,
//   2 Natural Minor, ...); range 0 High, 1 Middle, 2 Low, 3 Deep.
// build: g++ -O2 -std=c++17 -I../../Source render.cpp -o render
#include "dsp/Tune.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

static bool readWav (const char* path, std::vector<float>& x, int& sr)
{
    FILE* f = std::fopen (path, "rb"); if (! f) return false;
    char id[4]; uint32_t sz;
    std::fread (id, 1, 4, f); std::fread (&sz, 4, 1, f); std::fread (id, 1, 4, f);
    int ch = 1, bits = 16;
    while (std::fread (id, 1, 4, f) == 4 && std::fread (&sz, 4, 1, f) == 1)
    {
        if (! std::memcmp (id, "fmt ", 4))
        {
            std::vector<uint8_t> b (sz); std::fread (b.data(), 1, sz, f);
            ch = b[2] | (b[3] << 8); sr = (int) (b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t) b[7] << 24)); bits = b[14] | (b[15] << 8);
        }
        else if (! std::memcmp (id, "data", 4))
        {
            const int bps = bits / 8; const size_t frames = sz / (size_t) (bps * ch);
            std::vector<uint8_t> b (sz); std::fread (b.data(), 1, sz, f);
            x.resize (frames);
            for (size_t i = 0; i < frames; ++i)
            {
                const uint8_t* p = b.data() + i * (size_t) (bps * ch);
                int32_t v = bits == 24 ? (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) >> 8
                                       : (int32_t) (int16_t) (p[0] | (p[1] << 8));
                x[i] = (float) v / (bits == 24 ? 8388608.0f : 32768.0f);
            }
            break;
        }
        else std::fseek (f, (long) (sz + (sz & 1)), SEEK_CUR);
    }
    std::fclose (f);
    return ! x.empty();
}

static void writeWav (const char* path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path, "wb");
    const uint32_t n = (uint32_t) x.size(), bytes = n * 3;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); }; auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (1); w16 (1); w32 ((uint32_t) sr);
    w32 ((uint32_t) sr * 3); w16 (3); w16 (24); std::fwrite ("data", 1, 4, f); w32 (bytes);
    for (float v : x)
    {
        const int32_t q = (int32_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 8388607.0f);
        const uint8_t b[3] = { (uint8_t) (q & 255), (uint8_t) ((q >> 8) & 255), (uint8_t) ((q >> 16) & 255) };
        std::fwrite (b, 1, 3, f);
    }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    if (argc < 8) { std::fprintf (stderr, "usage: render in out quality speedMs humanizePct key scale [range]\n"); return 1; }
    std::vector<float> x; int sr = 44100;
    if (! readWav (argv[1], x, sr)) { std::fprintf (stderr, "cannot read %s\n", argv[1]); return 1; }
    kv::TuneSettings s;
    s.quality = std::atoi (argv[3]); s.speedMs = (float) std::atof (argv[4]); s.humanize = (float) std::atof (argv[5]) / 100.0f;
    s.range = argc > 8 ? std::atoi (argv[8]) : 1;
    kv::scaleNotes (std::atoi (argv[6]), std::atoi (argv[7]), s.notes);
    kv::Tune t; t.prepare (sr);
    const int lat = kv::Tune::latencyFor (sr, s.quality, s.range);
    std::vector<float> l (x.size() + (size_t) lat, 0.0f);
    std::copy (x.begin(), x.end(), l.begin());
    std::vector<float> r = l;
    for (size_t i = 0; i < l.size(); i += 256)
        t.process (l.data() + i, r.data() + i, (int) std::min<size_t> (256, l.size() - i), s);
    std::vector<float> out (l.begin() + lat, l.end());
    writeWav (argv[2], out, sr);
    return 0;
}
