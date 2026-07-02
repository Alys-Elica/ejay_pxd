#ifndef PXD_H
#define PXD_H

#include <cstdint>
#include <string>
#include <vector>

struct WavAudioData {
    std::uint32_t sampleRate = 44100;
    std::uint16_t channels = 1;
    std::uint16_t bitsPerSample = 16;
    std::vector<std::int16_t> audioSamples;
    std::vector<std::string> strings;
};

struct InfSample {
    int id = 0;
    int type = 0;
    std::string code;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::string name;
    std::string variant;
    int one = 0;
    int group = 0;
    std::string unknownText;
    int zero = 0;
    std::string tailText;
};

bool parsePxd(const std::vector<uint8_t>& data, WavAudioData& audio);
bool parseInf(const std::string_view data, std::vector<InfSample>& samples);

#endif // PXD_H
