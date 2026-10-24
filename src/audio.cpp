#include "audio.h"

#include <M5Unified.h>

namespace audio {

static constexpr uint32_t REC_MAX_SECONDS = 8;
static constexpr size_t   REC_MAX_SAMPLES = REC_SAMPLE_RATE * REC_MAX_SECONDS;
static constexpr size_t   REC_CHUNK_SAMPLES = REC_SAMPLE_RATE / 10;  // 100ms ごとに録音する

static constexpr uint8_t SPEAKER_VOLUME = 180;

// 口パクの音量計算に使う区間と、口が全開になる音量。
static constexpr size_t   LEVEL_WINDOW_SAMPLES = 512;
static constexpr float    LEVEL_FULL_SCALE     = 6000.0f;
static constexpr uint32_t LEVEL_INTERVAL_MS    = 30;

static int16_t* rec_buffer = nullptr;
static size_t   rec_samples = 0;

bool begin() {
    rec_buffer = static_cast<int16_t*>(ps_malloc(REC_MAX_SAMPLES * sizeof(int16_t)));
    M5.Mic.end();
    M5.Speaker.begin();
    M5.Speaker.setVolume(SPEAKER_VOLUME);
    return rec_buffer != nullptr;
}

void start_recording() {
    M5.Speaker.end();
    M5.Mic.begin();
    rec_samples = 0;
}

bool record_chunk() {
    if (rec_samples + REC_CHUNK_SAMPLES > REC_MAX_SAMPLES) {
        return false;
    }
    // 録音は2つまで予約でき、空きがなければ待つ。予約したぶんを録れたものとして数える。
    M5.Mic.record(rec_buffer + rec_samples, REC_CHUNK_SAMPLES, REC_SAMPLE_RATE);
    rec_samples += REC_CHUNK_SAMPLES;
    return true;
}

size_t stop_recording() {
    while (M5.Mic.isRecording()) {
        delay(1);
    }
    M5.Mic.end();
    M5.Speaker.begin();
    return rec_samples;
}

const int16_t* recorded() {
    return rec_buffer;
}

static uint16_t read_le16(const uint8_t* p) {
    return p[0] | (p[1] << 8);
}

static uint32_t read_le32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// 口パクの音量計算のため、playWav() ではなく WAV を自前で読んで PCM を取り出す。
bool play_wav(const uint8_t* wav, size_t size, void (*on_level)(float level)) {
    if (size < 12 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
        return false;
    }

    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    const int16_t* pcm = nullptr;
    size_t pcm_samples = 0;

    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8_t* chunk = wav + pos;
        uint32_t chunk_size = read_le32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            channels = read_le16(chunk + 10);
            sample_rate = read_le32(chunk + 12);
            bits = read_le16(chunk + 22);
        } else if (memcmp(chunk, "data", 4) == 0) {
            // ストリーミングで作られた WAV はサイズが正しくないことがあるので、実データの長さに合わせる。
            if (chunk_size > size - pos - 8) {
                chunk_size = size - pos - 8;
            }
            pcm = reinterpret_cast<const int16_t*>(chunk + 8);
            pcm_samples = chunk_size / sizeof(int16_t);
            break;
        }
        pos += 8 + chunk_size + (chunk_size & 1);
    }

    if (pcm == nullptr || bits != 16 || channels != 1 || sample_rate == 0) {
        Serial.printf("unsupported wav: rate=%u ch=%u bits=%u\n", sample_rate, channels, bits);
        return false;
    }

    if (!M5.Speaker.playRaw(pcm, pcm_samples, sample_rate)) {
        return false;
    }

    const uint32_t start_ms = millis();
    while (M5.Speaker.isPlaying()) {
        const size_t played = static_cast<uint64_t>(millis() - start_ms) * sample_rate / 1000;
        float sum = 0.0f;
        size_t n = 0;
        for (size_t i = played; i < played + LEVEL_WINDOW_SAMPLES && i < pcm_samples; i++) {
            sum += static_cast<float>(pcm[i]) * pcm[i];
            n++;
        }
        const float rms = n > 0 ? sqrtf(sum / n) : 0.0f;
        on_level(min(1.0f, rms / LEVEL_FULL_SCALE));
        delay(LEVEL_INTERVAL_MS);
    }
    on_level(0.0f);
    return true;
}

}  // namespace audio
