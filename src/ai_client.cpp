#include "ai_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "secrets.h"

namespace ai_client {

static constexpr const char* CHAT_URL       = "https://api.ai.sakura.ad.jp/v1/chat/completions";
static constexpr const char* TRANSCRIBE_URL = "https://api.ai.sakura.ad.jp/v1/audio/transcriptions";
static constexpr const char* SPEECH_URL     = "https://api.ai.sakura.ad.jp/v1/audio/speech";

static constexpr const char* CHAT_MODEL       = "gpt-oss-120b";
static constexpr const char* TRANSCRIBE_MODEL = "whisper-large-v3-turbo";
static constexpr const char* SPEECH_MODEL     = "zundamon";
static constexpr const char* SPEECH_VOICE     = "normal";

static constexpr int MAX_TOKENS = 1024;
static constexpr uint16_t HTTP_TIMEOUT_MS = 30000;

static constexpr const char* SYSTEM_PROMPT =
    "あなたは机の上にいる小さなロボット「スタックチャン」です。"
    "親しみやすい口調で、日本語で1〜2文の短い返事をしてください。"
    "返事は音声で読み上げるので、記号や絵文字、箇条書きは使わないでください。";

static constexpr const char* MULTIPART_BOUNDARY = "----stackchan-boundary";

// HTTPClient::writeToStream() の受け口。受け取ったデータを PSRAM に貯める。
// writeToStream() はチャンク転送にも対応しているので、自前で解析しなくて済む。
class PsramBufferStream : public Stream {
   public:
    ~PsramBufferStream() { free(data_); }

    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* buf, size_t len) override {
        if (size_ + len > capacity_) {
            size_t new_capacity = capacity_ == 0 ? 64 * 1024 : capacity_ * 2;
            while (new_capacity < size_ + len) {
                new_capacity *= 2;
            }
            uint8_t* p = static_cast<uint8_t*>(ps_realloc(data_, new_capacity));
            if (p == nullptr) {
                return 0;
            }
            data_ = p;
            capacity_ = new_capacity;
        }
        memcpy(data_ + size_, buf, len);
        size_ += len;
        return len;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

    size_t size() const { return size_; }
    // 中身の所有権を呼び出し側に渡す。
    uint8_t* release() {
        uint8_t* p = data_;
        data_ = nullptr;
        size_ = capacity_ = 0;
        return p;
    }

   private:
    uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
};

static bool begin_request(WiFiClientSecure& client, HTTPClient& http, const char* url) {
    // TODO: 証明書を検証していない。ルート証明書の設定は後で対応する。
    client.setInsecure();
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(client, url)) {
        return false;
    }
    http.addHeader("Authorization", String("Bearer ") + SAKURA_AI_TOKEN);
    return true;
}

bool chat(const String& user_text, String& reply) {
    JsonDocument req;
    req["model"] = CHAT_MODEL;
    req["max_tokens"] = MAX_TOKENS;
    req["stream"] = false;
    JsonArray messages = req["messages"].to<JsonArray>();
    JsonObject sys = messages.add<JsonObject>();
    sys["role"] = "system";
    sys["content"] = SYSTEM_PROMPT;
    JsonObject user = messages.add<JsonObject>();
    user["role"] = "user";
    user["content"] = user_text;

    String body;
    serializeJson(req, body);

    WiFiClientSecure client;
    HTTPClient http;
    if (!begin_request(client, http, CHAT_URL)) {
        reply = "接続を開始できませんでした";
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    const int status = http.POST(body);
    const String res_body = http.getString();
    http.end();

    if (status != HTTP_CODE_OK) {
        reply = "HTTP " + String(status) + ": " + res_body;
        return false;
    }

    JsonDocument res;
    const DeserializationError err = deserializeJson(res, res_body);
    if (err) {
        reply = String("JSON 解析エラー: ") + err.c_str();
        return false;
    }

    const char* content = res["choices"][0]["message"]["content"];
    if (content == nullptr) {
        reply = "返事が空でした: " + res_body;
        return false;
    }
    reply = content;
    reply.trim();
    return true;
}

static void write_le16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = v >> 8;
}

static void write_le32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        p[i] = (v >> (8 * i)) & 0xFF;
    }
}

// 16bit モノラル PCM 用の 44 バイトの WAV ヘッダーを書く。
static void write_wav_header(uint8_t* p, size_t samples, uint32_t sample_rate) {
    const uint32_t data_size = samples * sizeof(int16_t);
    memcpy(p, "RIFF", 4);
    write_le32(p + 4, 36 + data_size);
    memcpy(p + 8, "WAVEfmt ", 8);
    write_le32(p + 16, 16);               // fmt チャンクのサイズ
    write_le16(p + 20, 1);                // PCM
    write_le16(p + 22, 1);                // モノラル
    write_le32(p + 24, sample_rate);
    write_le32(p + 28, sample_rate * 2);  // バイトレート
    write_le16(p + 32, 2);                // ブロックサイズ
    write_le16(p + 34, 16);               // ビット数
    memcpy(p + 36, "data", 4);
    write_le32(p + 40, data_size);
}

bool transcribe(const int16_t* pcm, size_t samples, uint32_t sample_rate, String& text) {
    const String head = String("--") + MULTIPART_BOUNDARY + "\r\n" +
                        "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
                        TRANSCRIBE_MODEL + "\r\n" +
                        "--" + MULTIPART_BOUNDARY + "\r\n" +
                        "Content-Disposition: form-data; name=\"language\"\r\n\r\n" +
                        "ja\r\n" +
                        "--" + MULTIPART_BOUNDARY + "\r\n" +
                        "Content-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\n" +
                        "Content-Type: audio/wav\r\n\r\n";
    const String tail = String("\r\n--") + MULTIPART_BOUNDARY + "--\r\n";

    static constexpr size_t WAV_HEADER_SIZE = 44;
    const size_t pcm_bytes = samples * sizeof(int16_t);
    const size_t body_size = head.length() + WAV_HEADER_SIZE + pcm_bytes + tail.length();
    uint8_t* body = static_cast<uint8_t*>(ps_malloc(body_size));
    if (body == nullptr) {
        text = "送信用のメモリを確保できませんでした";
        return false;
    }
    uint8_t* p = body;
    memcpy(p, head.c_str(), head.length());
    p += head.length();
    write_wav_header(p, samples, sample_rate);
    p += WAV_HEADER_SIZE;
    memcpy(p, pcm, pcm_bytes);
    p += pcm_bytes;
    memcpy(p, tail.c_str(), tail.length());

    WiFiClientSecure client;
    HTTPClient http;
    if (!begin_request(client, http, TRANSCRIBE_URL)) {
        free(body);
        text = "接続を開始できませんでした";
        return false;
    }
    http.addHeader("Content-Type", String("multipart/form-data; boundary=") + MULTIPART_BOUNDARY);
    http.addHeader("Accept", "application/json");

    const int status = http.POST(body, body_size);
    free(body);
    const String res_body = http.getString();
    http.end();

    if (status != HTTP_CODE_OK) {
        text = "HTTP " + String(status) + ": " + res_body;
        return false;
    }

    JsonDocument res;
    const DeserializationError err = deserializeJson(res, res_body);
    if (err) {
        text = String("JSON 解析エラー: ") + err.c_str();
        return false;
    }
    const char* result = res["text"];
    if (result == nullptr) {
        text = "文字起こしの結果がありませんでした: " + res_body;
        return false;
    }
    text = result;
    text.trim();
    return true;
}

bool speak(const String& text, uint8_t*& wav, size_t& wav_size, String& error) {
    JsonDocument req;
    req["model"] = SPEECH_MODEL;
    req["voice"] = SPEECH_VOICE;
    req["input"] = text;
    req["response_format"] = "wav";

    String body;
    serializeJson(req, body);

    WiFiClientSecure client;
    HTTPClient http;
    if (!begin_request(client, http, SPEECH_URL)) {
        error = "接続を開始できませんでした";
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "audio/wav");

    const int status = http.POST(body);
    if (status != HTTP_CODE_OK) {
        error = "HTTP " + String(status) + ": " + http.getString();
        http.end();
        return false;
    }

    PsramBufferStream stream;
    const int written = http.writeToStream(&stream);
    http.end();
    if (written <= 0 || stream.size() == 0) {
        error = "音声データを受け取れませんでした (" + String(written) + ")";
        return false;
    }
    wav_size = stream.size();
    wav = stream.release();
    return true;
}

}  // namespace ai_client
