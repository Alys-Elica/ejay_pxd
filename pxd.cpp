#include "pxd.h"

#include <array>
#include <charconv>
#include <iostream>
#include <limits>

namespace {

int16_t clampToInt16(int value)
{
    if (value > std::numeric_limits<int16_t>::max()) {
        return std::numeric_limits<int16_t>::max();
    }
    if (value < std::numeric_limits<int16_t>::min()) {
        return std::numeric_limits<int16_t>::min();
    }
    return static_cast<int16_t>(value);
}

std::array<int16_t, 256> buildDeltaTable()
{
    std::array<int16_t, 256> deltaTable { };
    double step = 2.2;
    double multiplier = 1.0565;

    deltaTable[128] = 0;
    for (int i = 1; i < 128; ++i) {
        const int16_t previous = deltaTable[127 + i];
        const int increment = i < 6 ? 1 : static_cast<int>(step - 0.52);
        deltaTable[128 + i] = static_cast<int16_t>(previous + increment);
        step *= multiplier;
        multiplier -= 0.00022;
    }

    deltaTable[0] = 0;
    for (int i = 1; i < 128; ++i) {
        deltaTable[128 - i] = static_cast<int16_t>(-deltaTable[128 + i]);
    }

    for (int16_t& value : deltaTable) {
        value = static_cast<int16_t>(value << 1);
    }

    return deltaTable;
}

std::string unquote(std::string_view value)
{
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return std::string(value.substr(1, value.size() - 2));
    }
    return std::string(value);
}

bool nextLine(std::string_view data, size_t& offset, std::string_view& line)
{
    if (offset >= data.size()) {
        return false;
    }

    const size_t begin = offset;
    const size_t end = data.find('\n', begin);
    if (end == std::string_view::npos) {
        offset = data.size();
        line = data.substr(begin);
    } else {
        offset = end + 1;
        line = data.substr(begin, end - begin);
    }

    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return true;
}

}

bool parsePxd(const std::vector<uint8_t>& data, WavAudioData& audio)
{
    if (data.empty()) {
        return false;
    }

    audio.audioSamples.clear();
    audio.strings.clear();

    size_t pos = 0;

    /* Parse header */
    // Magic
    uint32_t magic
        = static_cast<uint32_t>(data[pos++])
        | (static_cast<uint32_t>(data[pos++]) << 8)
        | (static_cast<uint32_t>(data[pos++]) << 16)
        | (static_cast<uint32_t>(data[pos++]) << 24);
    if (magic != 0x44785074) { // tPxD
        std::cerr << "[pxd] Invalid header\n";
        return false;
    }

    // String infos
    uint8_t stringSize = data[pos++];
    if (pos + stringSize > data.size()) {
        std::cerr << "[pxd] Invalid string header\n";
        return false;
    }
    std::string stringData(reinterpret_cast<const char*>(data.data() + pos), stringSize);
    pos += stringSize;

    size_t stringOffset = 0;
    std::string_view stringLine;
    while (nextLine(stringData, stringOffset, stringLine)) {
        audio.strings.emplace_back(stringLine);
    }

    // Marker check
    while (data[pos++] != 0x54) {
        if (pos >= data.size()) {
            std::cerr << "[pxd] Failed to find 0x54 marker\n";
            return false;
        }
    }

    // Decoded sample count
    uint32_t decodedSampleCount
        = static_cast<uint32_t>(data[pos++])
        | (static_cast<uint32_t>(data[pos++]) << 8)
        | (static_cast<uint32_t>(data[pos++]) << 16);
    for (size_t i = 0; i < 3 && pos < data.size(); ++i) {
        ++pos;
    }

    /* Audio decoding */
    static const std::array<int16_t, 256> deltaTable = buildDeltaTable();

    constexpr size_t tableSlots { 256 };
    constexpr size_t entriesPerTable { 8 };
    constexpr size_t maxDefinedEntries { 11 };
    std::array<int16_t, tableSlots * entriesPerTable + maxDefinedEntries + 1> adaptiveTables { };
    for (size_t table = 0; table < tableSlots; ++table) {
        for (size_t i = 0; i < 5; ++i) {
            adaptiveTables[table * entriesPerTable + i] = deltaTable[128];
        }
        adaptiveTables[table * entriesPerTable + 5] = -1;
    }

    audio.audioSamples.reserve(decodedSampleCount > 0 ? decodedSampleCount : 4096);

    int predictor = 0;
    auto useTable = [&](uint8_t tableIndex) {
        const size_t base = static_cast<size_t>(tableIndex) * entriesPerTable;
        for (size_t i = base; i < adaptiveTables.size(); ++i) {
            const int16_t delta = adaptiveTables[i];
            if (delta == -1) {
                break;
            }
            if (decodedSampleCount <= 0 || audio.audioSamples.size() < decodedSampleCount) {
                predictor += delta;
                audio.audioSamples.push_back(clampToInt16(predictor));
            }
        }
    };

    while (pos < data.size()) {
        const uint8_t op = data[pos++];
        if (op < 0xf4) {
            useTable(op);
        } else if (op == 0xff) {
            if (pos >= data.size()) {
                break;
            }
            if (decodedSampleCount <= 0 || audio.audioSamples.size() < decodedSampleCount) {
                predictor += deltaTable[data[pos++]];
                audio.audioSamples.push_back(clampToInt16(predictor));
            }
        } else {
            const int count = static_cast<int>(op - 0xf3);
            if (pos >= data.size()) {
                break;
            }

            const uint8_t tableIndex = data[pos++];
            const size_t tableBase = static_cast<size_t>(tableIndex) * entriesPerTable;
            if (tableBase >= adaptiveTables.size()) {
                std::cerr << "[pxd] PXD adaptive table index out of range\n";
                return false;
            }

            for (int i = 0; i < count && pos < data.size(); ++i) {
                const size_t entryIndex = tableBase + static_cast<size_t>(i);
                if (entryIndex >= adaptiveTables.size()) {
                    break;
                }
                adaptiveTables[entryIndex] = deltaTable[data[pos++]];
            }
            const size_t sentinelIndex = tableBase + static_cast<size_t>(count);
            if (sentinelIndex < adaptiveTables.size()) {
                adaptiveTables[sentinelIndex] = -1;
            }
            useTable(tableIndex);
        }
    }

    return true;
}

bool parseInf(const std::string_view data, std::vector<InfSample>& samples)
{
    samples.clear();

    size_t offset = 0;
    std::string_view line;
    while (nextLine(data, offset, line) && line != "[SAMPLES]") { }

    if (line != "[SAMPLES]") {
        std::cerr << "[inf] Missing [SAMPLES] section\n";
        return false;
    }

    while (nextLine(data, offset, line)) {
        InfSample sample;
        std::from_chars(line.data(), line.data() + line.size(), sample.id);
        if (!nextLine(data, offset, line)) {
            break;
        }
        std::from_chars(line.data(), line.data() + line.size(), sample.type);
        if (!nextLine(data, offset, line)) {
            break;
        }
        sample.code = unquote(line);
        if (!nextLine(data, offset, line)) {
            break;
        }
        std::from_chars(line.data(), line.data() + line.size(), sample.offset);
        if (!nextLine(data, offset, line)) {
            break;
        }
        std::from_chars(line.data(), line.data() + line.size(), sample.size);
        if (!nextLine(data, offset, line)) {
            break;
        }
        sample.name = unquote(line);
        if (!nextLine(data, offset, line)) {
            break;
        }
        sample.variant = unquote(line);
        if (!nextLine(data, offset, line)) {
            break;
        }
        std::from_chars(line.data(), line.data() + line.size(), sample.one);
        if (!nextLine(data, offset, line)) {
            break;
        }
        std::from_chars(line.data(), line.data() + line.size(), sample.group);
        if (!nextLine(data, offset, line)) {
            break;
        }
        sample.unknownText = unquote(line);
        if (!nextLine(data, offset, line)) {
            break;
        }
        std::from_chars(line.data(), line.data() + line.size(), sample.zero);
        if (!nextLine(data, offset, line)) {
            break;
        }
        sample.tailText = unquote(line);
        samples.push_back(std::move(sample));
    }

    return true;
}
