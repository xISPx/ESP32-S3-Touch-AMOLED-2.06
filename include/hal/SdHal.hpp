// ============================================================================
//  SdHal.hpp — TF card (SD_MMC 1-bit, pins 2/1/3 as in the vendor demo).
//  Mounted lazily by the music app; lists WAV files from / and /music.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>
#include <Arduino.h>  // String
#include <FS.h>       // fs::File

namespace hal {

struct WavInfo {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t dataOffset = 0;  // byte offset of the data chunk
    uint32_t dataSize = 0;    // payload bytes
    uint32_t totalSamples = 0;  // mono frames (dataSize / frameBytes)
};

class SdHal {
public:
    SdHal() = default;
    ~SdHal() = default;
    SdHal(const SdHal&) = delete;
    SdHal& operator=(const SdHal&) = delete;

    static SdHal& instance();

    // Mount the card (no-op when already mounted).  Returns false on failure.
    bool begin();

    bool mounted() const { return mounted_; }

    // Collect "*.wav" names from / and /music (short paths, no dirs).
    std::vector<String> listTracks();

    // Parse a PCM WAV header; leaves the file positioned for data reads.
    bool parseWav(File& f, WavInfo& out);

private:
    bool mounted_ = false;
};

}  // namespace hal
