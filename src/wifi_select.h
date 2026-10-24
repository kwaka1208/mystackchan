#pragma once

namespace wifi_select {

enum class Result {
    Connected,  // 選んだ接続先につながった
    Offline,    // オフラインが選ばれた。Wi-Fi は止めてある
};

// secrets.h に登録した接続先とオフラインの一覧を出し、選ばれるまで待つ。
// 接続先が選ばれたらつなぎ、つながらなければ一覧に戻る。
Result connect();

}  // namespace wifi_select
