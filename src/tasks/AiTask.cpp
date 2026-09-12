// ============================================================================
//  AiTask.cpp — official XiaoZhi client flow + OpenAI-compatible fallback.
// ============================================================================
#include "tasks/AiTask.hpp"
#include "config/config.hpp"
#include "core/Event.hpp"
#include "core/Logger.hpp"
#include "core/Settings.hpp"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <sys/time.h>
#include <esp_system.h>
#include <opus.h>
#include <stdarg.h>
#include <stdio.h>

namespace tasks {

namespace {
    constexpr const char* kTag = "Ai";

    constexpr uint32_t kWsConnectTimeoutMs = 10000;
    constexpr uint32_t kHelloTimeoutMs     = 10000;  // official default
    constexpr uint32_t kReplyTimeoutMs     = 40000;
    constexpr uint32_t kTtsIdleTimeoutMs   = 20000;  // waiting for tts start
    constexpr uint32_t kVoiceMaxMs         = 120000;
    constexpr size_t   kHistoryMaxEntries  = 8;      // 4 user/assistant turns

    constexpr const char* kOtaUrl = "https://api.tenclass.net/xiaozhi/ota/";
    constexpr const char* kFirmwareVersion = cfg::kFwVersion;
    constexpr const char* kBoardType = "esp32-s3-touch-amoled-2.06";
    constexpr const char* kBoardName = "Waveshare ESP32-S3-Touch-AMOLED-2.06";

    constexpr const char* kSystemPrompt =
        "Ты — ИИ-ассистент на смарт-часах. Отвечай дружелюбно и кратко "
        "(1-3 предложения), по-русски, если пользователь не попросил иначе. "
        "Без markdown-разметки.";

    // wss://host[:port]/path → components; returns false on garbage.
    bool parseWsUrl(const char* url, bool& tls, String& host, uint16_t& port,
                    String& path) {
        const char* p = strstr(url, "://");
        if (!p) return false;
        tls = strncmp(url, "wss", 3) == 0;
        p += 3;
        const char* slash = strchr(p, '/');
        const char* colon = strchr(p, ':');
        if (slash && colon && colon > slash) colon = nullptr;

        String hostPort = slash ? String(p).substring(0, slash - p) : String(p);
        host = colon ? String(p).substring(0, colon - p) : hostPort;
        if (colon) {
            port = atoi(colon + 1);
        } else {
            port = tls ? 443 : 80;
        }
        path = slash ? String(slash) : "/";
        return host.length() > 0 && port > 0;
    }

    // Cheap 2/3 decimation for 24k → 16k opus encoding (speech quality).
    size_t resample24to16(const int16_t* in, size_t inLen, int16_t* out) {
        size_t o = 0;
        for (size_t i = 0; i + 2 < inLen; i += 3) {
            out[o++] = (in[i] + in[i + 1]) / 2;
            out[o++] = (in[i + 1] + in[i + 2]) / 2;
        }
        return o;
    }

    // 3/2 interpolation for 16k → 24k playback when the server goes 16k.
    size_t resample16to24(const int16_t* in, size_t inLen, int16_t* out) {
        size_t o = 0;
        for (size_t i = 0; i + 1 < inLen; ++i) {
            out[o++] = in[i];
            out[o++] = (in[i] + in[i + 1]) / 2;
        }
        out[o++] = in[inLen - 1];
        return o;
    }
}

// ----------------------------------------------------------------------------
//  WebSocket pump
// ----------------------------------------------------------------------------

void AiTask::wsEvent(WStype_t type, uint8_t* payload, size_t len) {
    switch (type) {
        case WStype_CONNECTED:
            LOGI(kTag, "xiaozhi ws connected");
            wsConnected_ = true;
            helloDone_ = false;
            // Official hello handshake (docs/websocket.md §1.3)
            ws_.sendTXT(
                "{\"type\":\"hello\",\"version\":1,\"features\":{\"mcp\":false},"
                "\"transport\":\"websocket\",\"audio_params\":{\"format\":\"opus\","
                "\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60}}");
            break;

        case WStype_DISCONNECTED:
            LOGI(kTag, "xiaozhi ws disconnected");
            wsConnected_ = false;
            helloDone_ = false;
            speaking_ = false;
            break;

        case WStype_ERROR:
            wsFailed_ = true;
            if (errorBuf_.isEmpty()) errorBuf_ = "Ошибка соединения с сервером";
            break;

        case WStype_BIN:
            // Binary frames are opus TTS audio (protocol §4.2.9), one packet
            // per message.  Queue them only inside a voice session — in text
            // mode the audio is discarded (text already carries the answer).
            if (speaking_ && voiceSession_ && len > 0) {
                ttsFrames_.insert(ttsFrames_.end(), payload, payload + len);
                ttsLens_.push_back(static_cast<uint16_t>(len));
            }
            break;

        case WStype_TEXT: {
            if (len == 0) break;
            std::string buf(reinterpret_cast<char*>(payload), len);
            JsonDocument doc;
            if (deserializeJson(doc, buf.data(), buf.size())) break;

            const char* typeStr = doc["type"] | "";
            if (strcmp(typeStr, "hello") == 0) {
                sessionId_ = doc["session_id"] | "";
                helloDone_ = true;
                // Server dictates the decode rate for TTS (usually 24000).
                const int sr = doc["audio_params"]["sample_rate"] | 16000;
                if (sr >= 8000 && sr <= 48000 && sr != decSampleRate_) {
                    decSampleRate_ = sr;
                    if (dec_) {
                        // libopus fixes the rate at creation — recreate.
                        opus_decoder_destroy(dec_);
                        int err = 0;
                        dec_ = opus_decoder_create(decSampleRate_, 1, &err);
                    }
                }
                LOGI(kTag, "server hello, session=%s rate=%d",
                     sessionId_.c_str(), decSampleRate_);
            } else if (strcmp(typeStr, "tts") == 0) {
                const char* state = doc["state"] | "";
                if (strcmp(state, "start") == 0) {
                    speaking_ = true;
                    publishKind(static_cast<uint8_t>(core::AiMsgKind::Speaking),
                                "1");
                } else if (strcmp(state, "sentence_start") == 0 ||
                           strcmp(state, "sentence") == 0) {
                    const char* text = doc["text"] | "";
                    if (text[0]) publishKind(
                        static_cast<uint8_t>(core::AiMsgKind::Reply), "%s", text);
                } else if (strcmp(state, "stop") == 0) {
                    replyDone_ = true;
                    publishKind(static_cast<uint8_t>(core::AiMsgKind::Speaking),
                                "0");
                }
            } else if (strcmp(typeStr, "stt") == 0) {
                const char* text = doc["text"] | "";
                if (text[0]) publishKind(
                    static_cast<uint8_t>(core::AiMsgKind::Stt), "%s", text);
            } else if (strcmp(typeStr, "llm") == 0) {
                // Official emotion event → the chat header face.
                const char* emo = doc["emotion"] | "";
                if (emo[0]) publishKind(
                    static_cast<uint8_t>(core::AiMsgKind::Emotion), "%s", emo);
            } else if (strcmp(typeStr, "alert") == 0) {
                const char* msg = doc["message"] | "Внимание";
                publishKind(static_cast<uint8_t>(core::AiMsgKind::Error), "%s", msg);
            } else if (strcmp(typeStr, "error") == 0) {
                wsFailed_ = true;
                errorBuf_ = doc["message"] | "Ошибка сервера XiaoZhi";
            }
            break;
        }

        default:
            break;  // ping/pong/fragments handled by the library
    }
}

// ----------------------------------------------------------------------------
//  Official provisioning: POST device info to the OTA endpoint
// ----------------------------------------------------------------------------

bool AiTask::provisionOta() {
    if (WiFi.status() != WL_CONNECTED) {
        publishText(true, "Нет Wi-Fi. Подключитесь в Настройках.");
        return false;
    }

    const String mac = WiFi.macAddress();
    // Body mirrors Board::GetSystemInfoJson() of the official client.
    JsonDocument body;
    body["version"] = 2;
    body["flash_size"] = ESP.getFlashChipSize();
    body["psram_size"] = ESP.getPsramSize();
    body["minimum_free_heap_size"] = ESP.getMinFreeHeap();
    body["mac_address"] = mac;
    body["uuid"] = clientId_;
    body["chip_model_name"] = "esp32s3";
    body["application"]["name"] = "xiaozhi-watch";
    body["application"]["version"] = kFirmwareVersion;
    body["application"]["compile_time"] = __DATE__ " " __TIME__;
    body["board"]["type"] = kBoardType;
    body["board"]["name"] = kBoardName;
    body["language"] = "ru-RU";
    String payload;
    serializeJson(body, payload);

    // Official OTA headers (ota.cc SetupHttp): Activation-Version is what
    // makes the server treat us as an activatable DIY device.
    const char* extraHeaders[] = {"Activation-Version: 1", nullptr};

    int code = -1;
    String resp;
    String lastErr;
    // HTTPS first; plain HTTP fallback (ESP32 TLS to this server can be
    // reset by the CDN — the OTA payload is not sensitive).
    for (int attempt = 0; attempt < 2 && code <= 0; ++attempt) {
        const bool tls = (attempt == 0);
        const String url = String(tls ? "https://" : "http://") +
                           "api.tenclass.net/xiaozhi/ota/";
        IPAddress ip;
        if (WiFi.hostByName("api.tenclass.net", ip) != 1) {
            lastErr = "DNS: имя не разрешилось";
            continue;
        }
        LOGI(kTag, "OTA try %s -> %s", tls ? "https" : "http",
             ip.toString().c_str());

        HTTPClient http;
        if (tls) {
            WiFiClientSecure client;
            client.setInsecure();
            client.setHandshakeTimeout(20);
            if (!http.begin(client, url)) {
                lastErr = "begin() failed";
                continue;
            }
        } else {
            WiFiClient plain;
            if (!http.begin(plain, url)) {
                lastErr = "begin() failed";
                continue;
            }
        }
        http.setConnectTimeout(12000);
        http.setTimeout(30000);
        for (int h = 0; extraHeaders[h]; ++h) {
            String kv = extraHeaders[h];          // "Name: value"
            const int colon = kv.indexOf(':');
            http.addHeader(kv.substring(0, colon), kv.substring(colon + 2));
        }
        http.addHeader("Device-Id", mac);
        http.addHeader("Client-Id", clientId_);
        http.addHeader("User-Agent", "xiaozhi-watch/1.0.0 (esp32s3)");
        http.addHeader("Accept-Language", "ru-RU");
        http.addHeader("Content-Type", "application/json");

        code = http.POST(payload);
        if (code > 0) {
            resp = http.getString();
        } else {
            lastErr = http.errorToString(code);
            LOGW(kTag, "OTA %s failed: %d %s (min heap %u)",
                 tls ? "https" : "http", code, lastErr.c_str(),
                 static_cast<unsigned>(ESP.getMinFreeHeap()));
        }
        http.end();
    }

    if (code != HTTP_CODE_OK) {
        publishText(true, "OTA-сервер: %s (HTTP %d)",
                    lastErr.length() ? lastErr.c_str() : "нет ответа", code);
        return false;
    }

    JsonDocument d;
    if (deserializeJson(d, resp.c_str())) {
        publishText(true, "Некорректный ответ OTA-сервера");
        return false;
    }

    // server_time: seed the clock when nothing better is available (UTC).
    const long long ts = d["server_time"]["timestamp"] | 0LL;
    if (ts > 1577836800000LL && time(nullptr) < 1577836800) {
        struct timeval tv = {(time_t)(ts / 1000), (suseconds_t)((ts % 1000) * 1000)};
        settimeofday(&tv, nullptr);
    }

    // websocket{url, token} — present once the device is bound/known.
    bool hasWs = false;
    const char* wsUrl = d["websocket"]["url"] | "";
    const char* wsToken = d["websocket"]["token"] | "";
    if (wsUrl[0] && wsToken[0]) {
        auto& s = core::Settings::instance().d();
        strlcpy(s.aiWsUrl, wsUrl, sizeof(s.aiWsUrl));
        strlcpy(s.aiToken, wsToken, sizeof(s.aiToken));
        // NVS write on a PSRAM-stack task is forbidden — ask UiTask to save.
        core::EventBus::instance().publish(core::Event::makeSettingsChanged(
            core::kSettingsAi | core::kSettingsPersist));
        hasWs = true;
        LOGI(kTag, "OTA: websocket config stored (%s)", wsUrl);
    }

    // activation{code, message} — device not yet bound to a user account.
    const char* actCode = d["activation"]["code"] | "";
    if (actCode[0]) {
        publishKind(static_cast<uint8_t>(core::AiMsgKind::Info),
                    "Устройство не привязано.\nКод активации: %s\n"
                    "Введите его на xiaozhi.me — Консоль — устройства, "
                    "затем удерживайте кнопку микрофона снова.",
                    actCode);
        return false;
    }

    if (!hasWs) {
        publishText(true, "OTA-сервер не вернул настройки websocket");
        return false;
    }
    return true;
}

bool AiTask::ensureOpus() {
    if (enc_ && dec_) return true;
    encBuf_ = static_cast<uint8_t*>(malloc(400));  // 60 ms voip << 400 B
    decPcm_ = static_cast<int16_t*>(malloc(2 * 2880));
    int err = 0;
    enc_ = opus_encoder_create(cfg::kOpusSampleRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err) return false;
    opus_encoder_ctl(enc_, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(enc_, OPUS_SET_COMPLEXITY(1));
    opus_encoder_ctl(enc_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    dec_ = opus_decoder_create(decSampleRate_, 1, &err);
    if (err) return false;
    LOGI(kTag, "opus ready (enc 16k/24kbps, dec %d Hz)", decSampleRate_);
    return true;
}

bool AiTask::ensureXiaozhiConnected() {
    if (!ensureOpus()) {
        publishText(true, "Не хватило памяти для аудиокодека");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        publishText(true, "Нет Wi-Fi. Подключитесь в Настройках.");
        return false;
    }

    if (wsConnected_) return true;

    const auto& s = core::Settings::instance().d();
    bool tls = true;
    String host, path;
    uint16_t port = 443;
    if (!parseWsUrl(s.aiWsUrl, tls, host, port, path)) {
        publishText(true, "Неверный URL сервера XiaoZhi");
        return false;
    }
    // Official headers (websocket_protocol.cc): the Authorization header is
    // sent ONLY when a token exists — an empty "Bearer " makes the server
    // reject the handshake.
    const String mac = WiFi.macAddress();
    extraHeaders_ = "";
    if (s.aiToken[0] != '\0') {
        extraHeaders_ += String("Authorization: Bearer ") + s.aiToken + "\r\n";
    }
    extraHeaders_ += "Protocol-Version: 1\r\nDevice-Id: " + mac +
                     "\r\nClient-Id: " + clientId_ + "\r\n";

    wsConnected_ = false;
    helloDone_ = false;
    wsFailed_ = false;
    errorBuf_ = "";
    ttsFrames_.clear();
    ttsLens_.clear();
    publishKind(static_cast<uint8_t>(core::AiMsgKind::Status),
                "Подключение…");
    if (tls) ws_.beginSSL(host.c_str(), port, path.c_str());
    else ws_.begin(host.c_str(), port, path.c_str());
    ws_.setExtraHeaders(extraHeaders_.c_str());

    const uint32_t t0 = millis();
    while (!wsConnected_ && !wsFailed_ && millis() - t0 < kWsConnectTimeoutMs) {
        ws_.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!wsConnected_) {
        LOGW(kTag, "ws connect failed: int heap %u KB, largest %u KB",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
        publishText(true,
            "Нет соединения с сервером (%s) — mem %u/%u КБ",
            host.c_str(),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
        return false;
    }

    // Wait for the server hello (10 s official timeout).
    const uint32_t t1 = millis();
    while (!helloDone_ && !wsFailed_ && millis() - t1 < kHelloTimeoutMs) {
        ws_.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!helloDone_) {
        publishText(true, "Сервер XiaoZhi не ответил (hello)");
        return false;
    }
    publishKind(static_cast<uint8_t>(core::AiMsgKind::Status), "Подключено");
    return true;
}

void AiTask::sendListenState(const char* state, const char* mode, const char* text) {
    JsonDocument q;
    q["session_id"] = sessionId_;
    q["type"] = "listen";
    q["state"] = state;
    if (mode) q["mode"] = mode;
    if (text) q["text"] = text;
    String msg;
    serializeJson(q, msg);
    ws_.sendTXT(msg);
}

// ---- Voice: push-to-talk (official manual-mode listening) ------------------

void AiTask::stopPlayback() {
    ttsFrames_.clear();
    ttsLens_.clear();
    speaking_ = false;
    hal_.audio.paEnable(false);  // release the speaker amplifier
}

void AiTask::pumpTtsPlayback() {
    static int16_t out24k[cfg::kOpusFrameSamples * 3 / 2 + 4];  // 60 ms @24k
    while (!ttsLens_.empty()) {
        const uint16_t len = ttsLens_.front();
        ttsLens_.erase(ttsLens_.begin());
        const int decoded = opus_decode(
            dec_, ttsFrames_.data(), static_cast<opus_int32>(len),
            decPcm_, 2880, 0);
        ttsFrames_.erase(ttsFrames_.begin(), ttsFrames_.begin() + len);
        if (decoded <= 0) continue;

        size_t outSamples;
        if (static_cast<size_t>(decSampleRate_) == cfg::kI2sSampleRate) {
            outSamples = static_cast<size_t>(decoded);  // 24k: play as-is
        } else {
            outSamples = resample16to24(decPcm_, static_cast<size_t>(decoded),
                                        out24k);
        }
        if (outSamples) hal_.audio.playPcm(out24k, outSamples);
        ws_.loop();  // keep the socket serviced between blocking writes
        return;      // one frame per pump iteration; keep polling the mailbox
    }
}

void AiTask::startVoice() {
    if (recording_ || speaking_) return;
    const auto& s = core::Settings::instance().d();
    if (s.aiToken[0] == '\0') {
        // Unprovisioned device: run the official OTA flow; it either stores
        // the websocket config (then we continue connecting) or prints the
        // activation code and stops here.
        if (!provisionOta()) return;
    }
    if (!ensureXiaozhiConnected()) return;
    if (!hal_.audio.healthy()) {
        publishText(true, "Аудио недоступно — используйте текст");
        return;
    }

    // The music player may have re-clocked I2S for a WAV file — pin 24 kHz.
    hal_.audio.setRate(cfg::kI2sSampleRate);

    hal_.audio.drainMic();
    sendListenState("start", "manual", nullptr);
    recording_ = true;
    voiceSession_ = true;
    LOGI(kTag, "voice capture start");

    // Capture at the official 24 kHz duplex rate; opus wants 16 kHz.
    static int16_t pcm24[cfg::kPttFrameSamples];
    static int16_t pcm16[cfg::kOpusFrameSamples];
    const uint32_t t0 = millis();
    while (recording_ && !wsFailed_ && millis() - t0 < kVoiceMaxMs) {
        const size_t got = hal_.audio.readMic(pcm24, cfg::kPttFrameSamples);
        if (got == cfg::kPttFrameSamples) {
            const size_t n16 = resample24to16(pcm24, got, pcm16);
            const int encLen = opus_encode(enc_, pcm16,
                                           cfg::kOpusFrameSamples,
                                           encBuf_, 400);
            if (encLen > 0 && n16 == cfg::kOpusFrameSamples) {
                ws_.sendBIN(encBuf_, static_cast<uint16_t>(encLen));
            }
        }
        ws_.loop();
        core::Event e;
        while (waitFor(e, 0)) {
            if (e.type == core::EventType::AiText &&
                e.ai.kind == static_cast<uint8_t>(core::AiMsgKind::VoiceStop)) {
                recording_ = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    sendListenState("stop", nullptr, nullptr);
    recording_ = false;
    LOGI(kTag, "voice capture stop");

    // Wait for the answer: tts start → binary frames → tts stop.
    const uint32_t t1 = millis();
    bool gotAnswer = false;
    while (!wsFailed_ && millis() - t1 < kTtsIdleTimeoutMs + kReplyTimeoutMs) {
        ws_.loop();
        if (speaking_ || !ttsLens_.empty()) {
            gotAnswer = true;
            pumpTtsPlayback();
            if (replyDone_ && ttsLens_.empty()) break;  // tts stop received
        } else if (replyDone_) {
            gotAnswer = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    stopPlayback();
    speaking_ = false;
    voiceSession_ = false;
    if (!gotAnswer && !wsFailed_) {
        publishText(true, "Речь не распознана — попробуйте ещё раз");
    }
    replyDone_ = false;
    LOGI(kTag, "voice session done");
}

// ---- Text backends ----------------------------------------------------------

bool AiTask::queryXiaozhi(const char* text) {
    const auto& s = core::Settings::instance().d();
    if (s.aiToken[0] == '\0') {
        if (!provisionOta()) return false;
    }
    if (!ensureXiaozhiConnected()) return false;

    // The music player may have re-clocked I2S for a WAV file — pin 24 kHz.
    hal_.audio.setRate(cfg::kI2sSampleRate);

    // Official text input = listen/detect (protocol §4.1.4)
    replyDone_ = false;
    speaking_ = false;
    sendListenState("detect", nullptr, text);

    LOGI(kTag, "query sent (%u bytes)", static_cast<unsigned>(strlen(text)));

    const uint32_t t0 = millis();
    while (!wsFailed_ && millis() - t0 < kReplyTimeoutMs) {
        ws_.loop();
        if (replyDone_) break;
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    if (wsFailed_) {
        publishText(true, "%s", errorBuf_.c_str());
        return false;
    }
    if (!replyDone_) {
        publishText(true, "Таймаут ответа XiaoZhi");
        return false;
    }
    return true;  // reply text was published via tts/sentence_start
}

bool AiTask::queryOpenAi(const char* text) {
    const auto& s = core::Settings::instance().d();
    if (s.aiApiKey[0] == '\0') {
        publishText(true, "Не настроен API-ключ.\nНастройки → ИИ-ассистент.");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        publishText(true, "Нет Wi-Fi. Подключитесь в Настройках.");
        return false;
    }

    JsonDocument body;
    body["model"] = s.aiModel;
    body["max_tokens"] = 400;
    body["temperature"] = 0.6;
    JsonArray msgs = body["messages"].to<JsonArray>();
    {
        JsonObject sys = msgs.add<JsonObject>();
        sys["role"] = "system";
        sys["content"] = kSystemPrompt;
        for (const auto& h : history_) {
            const size_t sep = h.find('|');
            if (sep == std::string::npos) continue;
            JsonObject m = msgs.add<JsonObject>();
            m["role"] = (h.substr(0, 2) == "u|") ? "user" : "assistant";
            m["content"] = h.substr(sep + 1);
        }
        JsonObject user = msgs.add<JsonObject>();
        user["role"] = "user";
        user["content"] = text;
    }
    String payload;
    serializeJson(body, payload);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, s.aiApiUrl)) {
        publishText(true, "Неверный API URL");
        return false;
    }
    http.setConnectTimeout(12000);
    http.setTimeout(60000);
    http.addHeader("Authorization", String("Bearer ") + s.aiApiKey);
    http.addHeader("Content-Type", "application/json");

    const int code = http.POST(payload);
    if (code != HTTP_CODE_OK) {
        String err = "HTTP " + String(code);
        const String resp = http.getString();
        if (resp.length()) {
            JsonDocument d;
            if (!deserializeJson(d, resp.c_str())) {
                const char* m = d["error"]["message"] | "";
                if (m[0]) err += ": " + String(m);
            }
        }
        http.end();
        publishText(true, "%s", err.c_str());
        return false;
    }

    const String resp = http.getString();
    http.end();

    JsonDocument d;
    if (deserializeJson(d, resp.c_str())) {
        publishText(true, "Некорректный ответ сервера");
        return false;
    }
    const char* content = d["choices"][0]["message"]["content"] | "";
    if (content[0] == '\0') {
        publishText(true, "Пустой ответ модели");
        return false;
    }

    publishText(false, "%s", content);

    history_.push_back(std::string("u|") + text);
    history_.push_back(std::string("a|") + content);
    while (history_.size() > kHistoryMaxEntries) {
        history_.erase(history_.begin(), history_.begin() + 2);
    }
    return true;
}

void AiTask::handleQuery(const char* text) {
    const auto& s = core::Settings::instance().d();
    LOGI(kTag, "query: \"%s\" (backend=%u)", text, s.aiBackend);
    if (s.aiBackend == 1) queryOpenAi(text);
    else queryXiaozhi(text);
}

void AiTask::publishText(bool isError, const char* fmt, ...) {
    char buf[core::kAiTextMax];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    core::EventBus::instance().publish(core::Event::makeAiText(
        isError ? core::AiMsgKind::Error : core::AiMsgKind::Reply, buf));
    if (isError) LOGW(kTag, "error reply: %s", buf);
}

void AiTask::publishKind(uint8_t kind, const char* fmt, ...) {
    char buf[core::kAiTextMax];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    core::EventBus::instance().publish(core::Event::makeAiText(
        static_cast<core::AiMsgKind>(kind), buf));
}

void AiTask::initClientId() {

    Preferences prefs;
    prefs.begin("watch", true);
    String stored = prefs.getString("client_id", "");
    prefs.end();
    if (stored.length() >= 32) {
        strlcpy(clientId_, stored.c_str(), sizeof(clientId_));
        return;
    }
    // Stable per-device UUID (v4-shaped) generated once.
    uint32_t r[4] = {esp_random(), esp_random(), esp_random(), esp_random()};
    snprintf(clientId_, sizeof(clientId_),
             "%08lx-%04lx-4%03lx-a%03lx-%08lx%04lx",
             static_cast<unsigned long>(r[0]),
             static_cast<unsigned long>((r[1] >> 16) & 0xFFFF),
             static_cast<unsigned long>(r[1] & 0x0FFF),
             static_cast<unsigned long>((r[2] >> 16) & 0x0FFF),
             static_cast<unsigned long>(r[2] & 0xFFFFFFFF),
             static_cast<unsigned long>(r[3] & 0xFFFF));
    Preferences w;
    if (w.begin("watch", false)) {
        w.putString("client_id", clientId_);
        w.end();
    }
}

void AiTask::run() {
    // ensureClientId() already ran on the main task (NVS + flash are
    // forbidden on this PSRAM stack).
    core::EventBus::instance().subscribe(mailbox());
    ws_.onEvent([this](WStype_t t, uint8_t* p, size_t l) { wsEvent(t, p, l); });

    LOGI(kTag, "running (client-id %s)", clientId_);

    core::Event e;
    while (true) {
        if (waitFor(e, 50)) {
            if (e.type != core::EventType::AiText) continue;
            const auto kind = static_cast<core::AiMsgKind>(e.ai.kind);
            switch (kind) {
                case core::AiMsgKind::Query:
                    handleQuery(e.ai.text);
                    break;
                case core::AiMsgKind::VoiceStart:
                    startVoice();
                    break;
                case core::AiMsgKind::VoiceStop:
                    recording_ = false;
                    break;
                default:
                    break;  // own replies and other traffic ignored
            }
        } else {
            // Idle keepalive: server pings keep the xiaozhi session warm.
            if (wsConnected_) ws_.loop();
        }
    }
}

}  // namespace tasks
