# stackchan

M5Stack **StackChan**(CoreS3 ベース)で動く AI エージェントです。
AI の処理には [さくらのAI Engine](https://www.sakura.ad.jp/aipf/ai-engine/) を使います。

## 開発の段階

1. 雛形(アバター表示と Wi-Fi 接続)
2. テキスト対話(Chat Completions API)
3. 音声対話(音声認識 → 対話 → 音声合成) ← いまここ
4. エージェント化(tool calling で首振りや表情を操作)

## ハードウェア

- **M5Stack StackChan**(ホスト: M5Stack CoreS3 / ESP32-S3)

## 準備

`include/secrets.h.example` を `include/secrets.h` にコピーし、Wi-Fi の SSID・パスワードと、さくらのAI Engine のアカウントトークンを書き込みます。
`secrets.h` は `.gitignore` で管理対象外にしています。

Wi-Fi の接続先は `WIFI_NETWORKS` に「表示名、SSID、パスワード」の組で複数書けます。一覧には表示名が出ます。複数あると起動時に一覧が出るので、タッチで選びます。5秒触らなければ、前回つないだ接続先につなぎます。つながらなかったときは一覧に戻ります。以前の書き方（`WIFI_SSID`、`WIFI_PASSWORD`）も、1件の登録として使えます。

## 使い方(音声対話)

画面を押している間だけ録音します(最長 8 秒)。指を離すと文字起こし → 返事 → 読み上げの順に進み、話している間は口が動きます。
声は「ずんだもん」で、`src/ai_client.cpp` の `SPEECH_MODEL` / `SPEECH_VOICE` で変えられます。

## 使い方(テキスト対話)

`make monitor` でシリアルモニターを開き、話しかけたい文を入力して Enter を押します。
返事がシリアルモニターとアバターの吹き出しに表示されます。
モデルやシステムプロンプトは `src/ai_client.cpp` の定数で変えられます。

## ビルドと書き込み

[PlatformIO CLI](https://platformio.org/install/cli) が必要です。

```bash
make build    # ビルド
make upload   # 書き込み
make monitor  # シリアルモニター
```
