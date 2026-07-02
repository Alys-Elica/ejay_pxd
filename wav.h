#ifndef WAV_H
#define WAV_H

#include <string>

#include "pxd.h"

bool writeWavMono16(const std::string_view& path, const WavAudioData& audio);

#endif // WAV_H
