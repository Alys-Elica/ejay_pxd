#include "wav.h"

#include <fstream>
#include <iostream>
#include <numeric>

bool writeWavMono16(const std::string_view& path, const WavAudioData& audio)
{
    if (audio.channels != 1 || audio.bitsPerSample != 16) {
        std::cerr << "[wav] Only 16-bit mono WAV output is supported\n";
        return false;
    }

    std::ofstream output(path.data(), std::ios::binary);
    if (!output) {
        std::cerr << "[wav] Could not open " << path << "\n";
        return false;
    }

    auto writeLe16 = [&](uint16_t value) {
        output.put(static_cast<char>(value & 0xff));
        output.put(static_cast<char>((value >> 8) & 0xff));
    };
    auto writeLe32 = [&](uint32_t value) {
        output.put(static_cast<char>(value & 0xff));
        output.put(static_cast<char>((value >> 8) & 0xff));
        output.put(static_cast<char>((value >> 16) & 0xff));
        output.put(static_cast<char>((value >> 24) & 0xff));
    };

    std::string pxdStrings = std::accumulate(audio.strings.begin(), audio.strings.end(), std::string(),
        [](std::string result, const std::string& value) {
            if (!result.empty()) {
                result.push_back('\n');
            }
            result += value;
            return result;
        });

    const uint32_t dataSize = static_cast<uint32_t>(audio.audioSamples.size() * sizeof(int16_t));
    const uint32_t commentSize = static_cast<uint32_t>(pxdStrings.size() + 1);
    const uint32_t infoListSize = audio.strings.empty() ? 0 : (4 + 8 + commentSize + (commentSize & 1));

    output.write("RIFF", 4);
    writeLe32(36 + dataSize + (infoListSize > 0 ? 8 + infoListSize : 0));
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    writeLe32(16);
    writeLe16(1);
    writeLe16(audio.channels);
    writeLe32(audio.sampleRate);
    writeLe32(audio.sampleRate * audio.channels * audio.bitsPerSample / 8);
    writeLe16(static_cast<uint16_t>(audio.channels * audio.bitsPerSample / 8));
    writeLe16(audio.bitsPerSample);
    if (!audio.strings.empty()) {
        output.write("LIST", 4);
        writeLe32(infoListSize);
        output.write("INFO", 4);

        output.write("ICMT", 4);
        writeLe32(static_cast<uint32_t>(pxdStrings.size() + 1));
        output.write(pxdStrings.data(), static_cast<std::streamsize>(pxdStrings.size()));
        output.put('\0');
        if ((pxdStrings.size() + 1) & 1) {
            output.put('\0');
        }
    }
    output.write("data", 4);
    writeLe32(dataSize);

    for (const int16_t sample : audio.audioSamples) {
        writeLe16(static_cast<uint16_t>(sample));
    }

    return true;
}
