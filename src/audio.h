#pragma once

#include <Arduino.h>

// マイク録音とスピーカー再生。
// CoreS3 はマイクとスピーカーを同時に使えないため、録音中だけマイクに切り替える。
namespace audio {

static constexpr uint32_t REC_SAMPLE_RATE = 16000;

// 録音バッファを確保する。setup() で一度だけ呼ぶ。
bool begin();

// 録音を始める。
void start_recording();
// 次の区切りぶんを録音する。バッファが一杯になったら false を返す。
bool record_chunk();
// 録音を止め、録れたサンプル数を返す。
size_t stop_recording();
// 録音したデータ (16bit モノラル PCM)。
const int16_t* recorded();

// WAV データを再生し、終わるまで待つ。
// 再生中は on_level に今の音量 (0.0〜1.0) を渡して呼び続ける。口パクに使う。
bool play_wav(const uint8_t* wav, size_t size, void (*on_level)(float level));

}  // namespace audio
