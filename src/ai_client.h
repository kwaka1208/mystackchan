#pragma once

#include <Arduino.h>

// さくらのAI Engine (OpenAI 互換 API) のクライアント。
namespace ai_client {

// ユーザーの発言を Chat Completions API に送り、返事を reply に入れる。
// 失敗したときは false を返し、reply にエラー内容を入れる。
bool chat(const String& user_text, String& reply);

// 16bit モノラル PCM を文字起こしして text に入れる。
// 失敗したときは false を返し、text にエラー内容を入れる。
bool transcribe(const int16_t* pcm, size_t samples, uint32_t sample_rate, String& text);

// text を音声合成し、WAV データを PSRAM に確保して wav / wav_size に入れる。
// wav は呼び出し側で free() すること。失敗したときは false を返し、error に内容を入れる。
bool speak(const String& text, uint8_t*& wav, size_t& wav_size, String& error);

}  // namespace ai_client
