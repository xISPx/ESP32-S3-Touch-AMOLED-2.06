// ============================================================================
//  SdHal.cpp — TF card via SD_MMC 1-bit (vendor 07_LVGL_SD_Test wiring).
// ============================================================================
#include "hal/SdHal.hpp"
#include "config/config.hpp"
#include "core/Logger.hpp"
#include <SD_MMC.h>
#include <cstring>

namespace hal {

namespace {
    constexpr const char* kTag = "Sd";

    bool iendsWithWav(const String& name) {
        const String lower = name;
        const char* p = lower.c_str();
        const size_t n = strlen(p);
        return n > 4 && strcasecmp(p + n - 4, ".wav") == 0;
    }
}

SdHal& SdHal::instance() {
    static SdHal s;
    return s;
}

bool SdHal::begin() {
    if (mounted_) return true;
    SD_MMC.setPins(cfg::kSdMmcClk, cfg::kSdMmcCmd, cfg::kSdMmcDat);
    if (!SD_MMC.begin("/sdcard", true)) {  // 1-bit mode, vendor wiring
        LOGW(kTag, "card mount failed");
        return false;
    }
    const uint8_t type = SD_MMC.cardType();
    if (type == CARD_NONE) {
        LOGW(kTag, "no card attached");
        SD_MMC.end();
        return false;
    }
    mounted_ = true;
    LOGI(kTag, "mounted, card type %u, %llu MB used / %llu MB",
         type,
         static_cast<unsigned long long>(SD_MMC.usedBytes() / (1024ULL * 1024)),
         static_cast<unsigned long long>(SD_MMC.totalBytes() / (1024ULL * 1024)));
    return true;
}

std::vector<String> SdHal::listTracks() {
    std::vector<String> out;
    if (!begin()) return out;

    const char* dirs[] = {"/", "/music"};
    for (const char* dir : dirs) {
        File root = SD_MMC.open(dir);
        if (!root || !root.isDirectory()) continue;
        File f;
        while ((f = root.openNextFile())) {
            if (!f.isDirectory() && iendsWithWav(f.name())) {
                // File::name() has no directory prefix — rebuild the full path.
                String path = String(dir) + String(dir[1] == '\0' ? "" : "/");
                out.push_back(path + f.name());
            }
            f.close();
        }
        root.close();
    }
    return out;
}

bool SdHal::parseWav(File& f, WavInfo& out) {
    out = WavInfo{};
    if (!f) return false;

    uint8_t hdr[12];
    if (f.read(hdr, 12) != 12) return false;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        LOGW(kTag, "not a RIFF/WAVE file");
        return false;
    }

    bool haveFmt = false;
    while (true) {
        uint8_t ch[8];
        if (f.read(ch, 8) != 8) break;
        const uint32_t size = static_cast<uint32_t>(ch[4]) |
                              (static_cast<uint32_t>(ch[5]) << 8) |
                              (static_cast<uint32_t>(ch[6]) << 16) |
                              (static_cast<uint32_t>(ch[7]) << 24);

        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (size < 16 || f.read(fmt, 16) != 16) return false;
            const uint16_t format = static_cast<uint16_t>(fmt[0]) |
                                    (static_cast<uint16_t>(fmt[1]) << 8);
            out.channels = static_cast<uint16_t>(fmt[2]) |
                           (static_cast<uint16_t>(fmt[3]) << 8);
            out.sampleRate = static_cast<uint32_t>(fmt[4]) |
                             (static_cast<uint32_t>(fmt[5]) << 8) |
                             (static_cast<uint32_t>(fmt[6]) << 16) |
                             (static_cast<uint32_t>(fmt[7]) << 24);
            out.bits = static_cast<uint16_t>(fmt[14]) |
                       (static_cast<uint16_t>(fmt[15]) << 8);
            if (format != 1) {  // PCM only
                LOGW(kTag, "unsupported WAV format %u", format);
                return false;
            }
            if (size > 16) f.seek(f.position() + size - 16);
            haveFmt = true;
        } else if (memcmp(ch, "data", 4) == 0) {
            out.dataOffset = f.position();
            out.dataSize = size;
            break;
        } else {
            f.seek(f.position() + size + (size & 1));  // chunks are word-aligned
        }
    }

    if (!haveFmt || out.dataSize == 0) return false;
    if (out.bits != 16 || out.channels < 1 || out.channels > 2) {
        LOGW(kTag, "unsupported WAV: ch=%u bits=%u", out.channels, out.bits);
        return false;
    }
    if (out.sampleRate < 8000 || out.sampleRate > 48000) {
        LOGW(kTag, "unsupported rate %u", out.sampleRate);
        return false;
    }
    const uint32_t frameBytes = 2 * out.channels;  // 16-bit
    out.totalSamples = out.dataSize / frameBytes;
    f.seek(out.dataOffset);
    return true;
}

}  // namespace hal
