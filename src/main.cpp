#include <M5Unified.h>
#include <WiFi.h>
#include <Avatar.h>

#include "ai_client.h"
#include "audio.h"
#include "wifi_select.h"

#include <vector>

using namespace m5avatar;

// これより短い録音は押し間違いとみなして無視する。
static constexpr size_t MIN_RECORD_SAMPLES = audio::REC_SAMPLE_RATE * 3 / 10;

// 吹き出しはライブラリ側で文字サイズ2倍固定のため、一度に出せる文字数が少ない。
// 収まらない文は電光掲示板のように1文字ずつ流す。
static constexpr size_t   SPEECH_WINDOW_CHARS = 8;    // 吹き出しに一度に出す文字数
static constexpr uint32_t SPEECH_SCROLL_MS    = 300;  // 1文字進める間隔
static constexpr const char* SPEECH_GAP       = "　　　";  // 末尾と先頭のつなぎ

// 通信を待つあいだの仕草。待ちのあいだ loop() は止まるので、別タスクで動かす。
static constexpr uint32_t BUSY_FRAME_MS = 50;    // 目線を更新する間隔（ライブラリのランダムな目線より優先させる）
static constexpr uint32_t BUSY_DOT_MS   = 400;   // 「...」を1つ進める間隔
static constexpr float    BUSY_GAZE_UP  = -0.6f; // 考え中は少し上を見る
static constexpr float    BUSY_SWAY_MS  = 2000.0f;  // 目線が左右を1往復する時間

static Avatar avatar;
static String serial_line;

static std::vector<String> speech_chars;  // 流す文を UTF-8 の1文字ずつに分けたもの
static size_t   speech_pos     = 0;
static uint32_t speech_last_ms = 0;

static SemaphoreHandle_t busy_mutex;
static const char* busy_label    = nullptr;  // nullptr のときは待ちの仕草をしない
static uint32_t    busy_start_ms = 0;

static std::vector<String> split_utf8(const String& text) {
    std::vector<String> chars;
    size_t i = 0;
    while (i < text.length()) {
        const uint8_t c = text[i];
        size_t len = 1;
        if (c >= 0xF0) {
            len = 4;
        } else if (c >= 0xE0) {
            len = 3;
        } else if (c >= 0xC0) {
            len = 2;
        }
        chars.push_back(text.substring(i, i + len));
        i += len;
    }
    return chars;
}

static void show_speech_window() {
    String window;
    for (size_t i = 0; i < SPEECH_WINDOW_CHARS; i++) {
        window += speech_chars[(speech_pos + i) % speech_chars.size()];
    }
    avatar.setSpeechText(window.c_str());
}

static void set_speech(const String& text) {
    std::vector<String> chars = split_utf8(text);
    if (chars.size() <= SPEECH_WINDOW_CHARS) {
        speech_chars.clear();
        avatar.setSpeechText(text.c_str());
        return;
    }
    speech_chars = split_utf8(text + SPEECH_GAP);
    speech_pos = 0;
    speech_last_ms = millis();
    show_speech_window();
}

static void update_speech() {
    if (speech_chars.empty() || millis() - speech_last_ms < SPEECH_SCROLL_MS) {
        return;
    }
    speech_last_ms = millis();
    speech_pos = (speech_pos + 1) % speech_chars.size();
    show_speech_window();
}

static void busy_task(void*) {
    for (;;) {
        xSemaphoreTake(busy_mutex, portMAX_DELAY);
        if (busy_label != nullptr) {
            const uint32_t elapsed = millis() - busy_start_ms;
            const float sway = sinf(elapsed * 2.0f * PI / BUSY_SWAY_MS);
            avatar.setRightGaze(BUSY_GAZE_UP, sway);
            avatar.setLeftGaze(BUSY_GAZE_UP, sway);

            static const char* const DOTS[] = {".", "..", "..."};
            const String text = String(busy_label) + DOTS[(elapsed / BUSY_DOT_MS) % 3];
            avatar.setSpeechText(text.c_str());
        }
        xSemaphoreGive(busy_mutex);
        vTaskDelay(pdMS_TO_TICKS(BUSY_FRAME_MS));
    }
}

// 通信を待つあいだ、目線を動かして吹き出しの「...」を進める。
static void start_busy(const char* label) {
    speech_chars.clear();
    xSemaphoreTake(busy_mutex, portMAX_DELAY);
    busy_label = label;
    busy_start_ms = millis();
    xSemaphoreGive(busy_mutex);
}

// 戻った時点でタスクはもう吹き出しを書き換えない。
static void stop_busy() {
    xSemaphoreTake(busy_mutex, portMAX_DELAY);
    busy_label = nullptr;
    xSemaphoreGive(busy_mutex);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    // 一覧は画面に直接描くので、アバターを出す前に接続する。
    const bool connected = wifi_select::connect();

    avatar.init();
    avatar.setSpeechFont(&fonts::efontJA_12);
    busy_mutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(busy_task, "busy", 4096, nullptr, 1, nullptr, APP_CPU_NUM);

    if (!audio::begin()) {
        Serial.println("failed to allocate recording buffer");
    }

    if (connected) {
        Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
        avatar.setExpression(Expression::Happy);
        set_speech("つながったよ");
    } else {
        Serial.println("Wi-Fi connection failed");
        avatar.setExpression(Expression::Sad);
        set_speech("Wi-Fiにつながらない");
    }
}

static void on_voice_level(float level) {
    avatar.setMouthOpenRatio(level);
}

// 返事は声で伝えるので吹き出しには出さない。声を出せなかったときだけ文字で見せる。
static void talk(const String& text) {
    Serial.printf("> %s\n", text.c_str());
    avatar.setExpression(Expression::Doubt);
    start_busy("考え中");

    uint32_t t = millis();
    String reply;
    if (!ai_client::chat(text, reply)) {
        stop_busy();
        Serial.printf("! %s\n", reply.c_str());
        avatar.setExpression(Expression::Sad);
        set_speech("エラーになっちゃった");
        return;
    }
    Serial.printf("< %s\n", reply.c_str());
    Serial.printf("[time] chat %u ms\n", millis() - t);

    t = millis();
    uint8_t* wav = nullptr;
    size_t wav_size = 0;
    String error;
    const bool synthesized = ai_client::speak(reply, wav, wav_size, error);
    Serial.printf("[time] speech %u ms\n", millis() - t);
    stop_busy();

    avatar.setExpression(Expression::Happy);
    if (!synthesized) {
        Serial.printf("! %s\n", error.c_str());
        set_speech(reply);
        return;
    }
    set_speech("");
    if (!audio::play_wav(wav, wav_size, on_voice_level)) {
        Serial.println("! 音声を再生できませんでした");
        set_speech(reply);
    }
    free(wav);
}

// シリアルから1行読み取れたら AI に送る。
static void poll_serial() {
    while (Serial.available()) {
        const char c = Serial.read();
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            serial_line += c;
            continue;
        }
        serial_line.trim();
        if (serial_line.length() > 0 && WiFi.status() == WL_CONNECTED) {
            talk(serial_line);
        }
        serial_line = "";
    }
}

// 画面を押している間だけ録音し、離したら文字起こしして AI に送る。
static void poll_touch() {
    if (!M5.Touch.getDetail().wasPressed() || WiFi.status() != WL_CONNECTED) {
        return;
    }

    avatar.setExpression(Expression::Neutral);
    set_speech("聞いてるよ");
    audio::start_recording();
    while (M5.Touch.getDetail().isPressed() && audio::record_chunk()) {
        M5.update();
    }
    const size_t samples = audio::stop_recording();
    Serial.printf("recorded %u samples\n", samples);

    if (samples < MIN_RECORD_SAMPLES) {
        set_speech("");
        return;
    }

    avatar.setExpression(Expression::Doubt);
    start_busy("聞き取り中");
    const uint32_t t = millis();
    String text;
    const bool transcribed = ai_client::transcribe(audio::recorded(), samples, audio::REC_SAMPLE_RATE, text);
    Serial.printf("[time] transcribe %u ms\n", millis() - t);
    stop_busy();
    if (!transcribed) {
        Serial.printf("! %s\n", text.c_str());
        avatar.setExpression(Expression::Sad);
        set_speech("聞き取れなかった");
        return;
    }
    if (text.length() == 0) {
        avatar.setExpression(Expression::Neutral);
        set_speech("もう一回言って");
        return;
    }
    talk(text);
}

void loop() {
    M5.update();
    poll_touch();
    poll_serial();
    update_speech();
    delay(10);
}
