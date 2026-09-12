// ============================================================================
//  AiTask.hpp — AI assistant worker implementing the OFFICIAL XiaoZhi
//  websocket protocol (78/xiaozhi-esp32, docs/websocket.md) plus an
//  OpenAI-compatible HTTPS fallback.  The board has an ES8311 codec with
//  mic + speaker, so push-to-talk voice works exactly like the official
//  client: listen start/stop + raw opus 16 kHz/60 ms binary frames (v1),
//  TTS opus playback at the server's advertised sample rate.
//
//  Provisioning follows the official OTA flow: with no stored token the task
//  POSTs device info to https://api.tenclass.net/xiaozhi/ota/, stores the
//  returned websocket url/token and displays the activation code for binding
//  at xiaozhi.me.
// ============================================================================
#pragma once

#include "core/TaskBase.hpp"
#include "hal/Hal.hpp"
#include <WebSocketsClient.h>
#include <vector>
#include <string>

struct OpusEncoder;
struct OpusDecoder;

namespace tasks {

class AiTask final : public core::TaskBase {
public:
    explicit AiTask(hal::Hal& hal) : hal_(hal) {}

    // NVS access must happen on an internal-stack task (main) BEFORE start:
    // PSRAM-stack tasks cannot touch the flash.
    void initClientId();
    const char* clientId() const { return clientId_; }

private:
    void run() override;

    void handleQuery(const char* text);
    bool queryXiaozhi(const char* text);
    bool queryOpenAi(const char* text);

    // ---- official provisioning (api.tenclass.net/xiaozhi/ota/) --------------
    bool provisionOta();

    // ---- voice (push-to-talk, official listen/opus flow) ---------------------
    void startVoice();
    bool ensureXiaozhiConnected();
    void sendListenState(const char* state, const char* mode, const char* text);
    void pumpTtsPlayback();
    bool ensureOpus();
    void stopPlayback();

    void publishText(bool isError, const char* fmt, ...);
    void publishKind(uint8_t kind, const char* fmt, ...);
    void ensureClientId();

    // WebSocketsClient event pump
    void wsEvent(WStype_t type, uint8_t* payload, size_t len);

    hal::Hal& hal_;

    // ---- XiaoZhi session state ----------------------------------------------
    WebSocketsClient ws_;
    bool wsConnected_ = false;
    bool helloDone_ = false;
    bool replyDone_ = false;
    bool wsFailed_ = false;
    String sessionId_;
    String replyBuf_;
    String errorBuf_;
    String extraHeaders_;   // kept alive: the library stores the pointer

    // ---- voice state ---------------------------------------------------------
    bool recording_ = false;     // PTT capture in progress
    bool voiceSession_ = false;  // true between listen-start and tts end
    bool speaking_ = false;      // server is streaming TTS
    int decSampleRate_ = 24000;  // server hello audio_params (official rate)
    std::vector<uint8_t> ttsFrames_;   // pending encoded TTS audio
    std::vector<uint16_t> ttsLens_;    // one entry per opus frame
    OpusEncoder* enc_ = nullptr;
    OpusDecoder* dec_ = nullptr;
    uint8_t* encBuf_ = nullptr;
    int16_t* decPcm_ = nullptr;

    // ---- OpenAI rolling history (user/assistant pairs) -----------------------
    std::vector<std::string> history_;

    char clientId_[40] = "";
};

}  // namespace tasks
