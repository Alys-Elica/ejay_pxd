#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>

#include "pxd.h"
#include "wav.h"

namespace {

std::string sanitizePathPart(std::string value)
{
    for (char& ch : value) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '-' && ch != '_' && ch != ' ' && ch != '.') {
            ch = '_';
        }
    }

    while (!value.empty() && value.back() == '_') {
        value.pop_back();
    }
    return value.empty() ? "sample" : value;
}

void exportPxd(const std::filesystem::path& inputPath)
{
    // Read file
    std::ifstream file(inputPath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open " << inputPath << "\n";
        return;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);

    // Decode file
    WavAudioData audio;
    if (!parsePxd(data, audio)) {
        std::cerr << "Failed to parse PXD\n";
        return;
    }

    // Save audio
    std::filesystem::path outputPath = inputPath;
    outputPath.replace_extension(".wav");

    writeWavMono16(outputPath.string(), audio);

    std::cout << inputPath << " -> " << outputPath << "\n";
}

void exportInf(const std::filesystem::path& infPath)
{
    // Read INF file
    std::ifstream fileInf(infPath);
    if (!fileInf.is_open()) {
        std::cerr << "Failed to open " << infPath << "\n";
        return;
    }

    fileInf.seekg(0, std::ios::end);
    size_t fileInfSize = fileInf.tellg();
    fileInf.seekg(0, std::ios::beg);

    std::string dataInf(fileInfSize, 0);
    fileInf.read(dataInf.data(), fileInfSize);

    // Parse INF file
    std::vector<InfSample> infSamples;
    if (!parseInf(dataInf, infSamples)) {
        std::cerr << "Failed to parse INF\n";
        return;
    }

    // Create sub-directory
    std::filesystem::path outputDirectory = std::filesystem::path(infPath.stem().string() + "_out");
    if (std::filesystem::exists(outputDirectory) && !std::filesystem::is_directory(outputDirectory)) {
        std::cerr << "Output path exists and is not a directory: " << outputDirectory.string();
        return;
    }
    std::filesystem::create_directories(outputDirectory);

    // Export PXD samples
    std::filesystem::path pxdPath = infPath;
    pxdPath.replace_extension();

    std::cout << infPath << " + " << pxdPath << " -> " << outputDirectory
              << "/ (" << infSamples.size() << " samples)\n";

    std::ifstream filePxd(pxdPath, std::ios::binary);
    if (!filePxd.is_open()) {
        std::cerr << "Failed to open " << pxdPath << "\n";
        return;
    }

    for (const InfSample& sample : infSamples) {
        // Read PXD data
        std::vector<uint8_t> dataPxd(sample.size);
        filePxd.seekg(sample.offset);
        filePxd.read(reinterpret_cast<char*>(dataPxd.data()), sample.size);

        // Decode PXD
        WavAudioData audio;
        if (!parsePxd(dataPxd, audio)) {
            std::cerr << "Failed to parse PXD\n";
            return;
        }

        // Save audio
        std::filesystem::path pathGroup = outputDirectory / std::to_string(sample.group);
        if (!std::filesystem::exists(pathGroup)) {
            std::filesystem::create_directories(pathGroup);
        }

        std::ostringstream name;
        if (!sample.code.empty()) {
            name << sanitizePathPart(sample.code);
        }
        name << '_' << sample.id;
        if (!sample.name.empty()) {
            name << '_' << sanitizePathPart(sample.name);
        }
        if (!sample.variant.empty()) {
            name << '_' << sanitizePathPart(sample.variant);
        }
        name << ".wav";

        const std::filesystem::path outputPath = pathGroup / name.str();
        writeWavMono16(outputPath.string(), audio);
    }

    std::cout << "Exported " << infSamples.size() << " samples to " << outputDirectory << "/\n";
}

}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <file>.[pxd|inf] [<file>.[pxd|inf]]\n";
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path inputPath(argv[i]);

        std::string extension = inputPath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extension == ".pxd") {
            exportPxd(inputPath);
        } else if (extension == ".inf") {
            exportInf(inputPath);
        } else {
            std::cerr << "Invalid file: " << inputPath << "\n";
        }
    }

    return 0;
}
