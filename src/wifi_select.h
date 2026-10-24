#pragma once

namespace wifi_select {

// secrets.h に登録した接続先から1つ選んで Wi-Fi につなぐ。
// 登録が複数あれば起動時に一覧を出して選ばせ、つながらなければ一覧に戻る。
// 登録が1件だけのときは一覧を出さず、つながらなければ false を返す。
bool connect();

}  // namespace wifi_select
