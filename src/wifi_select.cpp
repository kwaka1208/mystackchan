#include "wifi_select.h"

#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>

#include "secrets.h"

namespace wifi_select {

struct Network {
    const char* name;  // 一覧と接続中の画面に出す名前
    const char* ssid;
    const char* password;
};

#ifdef WIFI_NETWORKS
static const Network NETWORKS[] = WIFI_NETWORKS;
#else
static const Network NETWORKS[] = {{WIFI_SSID, WIFI_SSID, WIFI_PASSWORD}};
#endif
static constexpr size_t NETWORK_COUNT = sizeof(NETWORKS) / sizeof(NETWORKS[0]);

static constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;

// 一覧の見た目。画面に収まる5行のうち、最後の1行はオフラインに使う。
static constexpr int    HEADER_HEIGHT    = 40;
static constexpr int    ROW_HEIGHT       = 40;
static constexpr int    ROW_MARGIN       = 8;
static constexpr size_t MAX_NETWORK_ROWS = 4;

// 電波の強さを示すバー。RSSI が閾値以上なら1本ずつ増える。
static constexpr int BAR_WIDTH  = 4;
static constexpr int BAR_GAP    = 2;
static constexpr int BAR_STEP   = 5;  // 1本ごとの高さの増分
static constexpr int RSSI_BAR_2 = -75;
static constexpr int RSSI_BAR_3 = -60;

static constexpr int RSSI_NOT_FOUND = INT32_MIN;

static constexpr const char* PREF_NAMESPACE = "wifi";
static constexpr const char* PREF_LAST_SSID = "last";

static size_t load_last_index() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, true);
    const String last = prefs.getString(PREF_LAST_SSID, "");
    prefs.end();
    for (size_t i = 0; i < NETWORK_COUNT; i++) {
        if (last == NETWORKS[i].ssid) {
            return i;
        }
    }
    return 0;
}

static void save_last_index(size_t index) {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString(PREF_LAST_SSID, NETWORKS[index].ssid);
    prefs.end();
}

static size_t row_count() {
    return min(NETWORK_COUNT, MAX_NETWORK_ROWS);
}

static void draw_header(const String& text) {
    auto& d = M5.Display;
    d.fillRect(0, 0, d.width(), HEADER_HEIGHT, TFT_BLACK);
    d.setTextColor(TFT_WHITE);
    d.setTextDatum(middle_left);
    d.drawString(text, ROW_MARGIN, HEADER_HEIGHT / 2);
}

// 一覧に出す接続先を探し、見つかったものの RSSI を rssi に入れる。
// 見つからなかったものは RSSI_NOT_FOUND になる。
static void scan_networks(int rssi[]) {
    auto& d = M5.Display;
    d.fillScreen(TFT_BLACK);
    d.setFont(&fonts::efontJA_16);
    draw_header("スキャン中…");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    const int found = WiFi.scanNetworks();
    for (size_t i = 0; i < row_count(); i++) {
        rssi[i] = RSSI_NOT_FOUND;
        for (int j = 0; j < found; j++) {
            if (WiFi.SSID(j) == NETWORKS[i].ssid) {
                rssi[i] = max(rssi[i], (int)WiFi.RSSI(j));
            }
        }
    }
    WiFi.scanDelete();
}

static int signal_bars(int rssi) {
    if (rssi >= RSSI_BAR_3) {
        return 3;
    }
    if (rssi >= RSSI_BAR_2) {
        return 2;
    }
    return 1;
}

// 行の右端 right、中央 center_y に電波の強さのバーを描く。
static void draw_signal(int right, int center_y, int rssi) {
    auto& d = M5.Display;
    const int bars   = signal_bars(rssi);
    const int bottom = center_y + BAR_STEP * 3 / 2;
    for (int b = 0; b < 3; b++) {
        const int x = right - (3 - b) * (BAR_WIDTH + BAR_GAP);
        const int h = BAR_STEP * (b + 1);
        const uint16_t color = b < bars ? TFT_WHITE : TFT_DARKGREY;
        d.fillRect(x, bottom - h, BAR_WIDTH, h, color);
    }
}

// 近くで見つかった接続先は電波の強さを、見つからなかったものは灰色で「圏外」と出す。
// highlighted（前回の接続先）は見つかっていれば緑で目立たせる。最後にオフラインの行を出す。
static void draw_list(size_t highlighted, const int rssi[]) {
    auto& d = M5.Display;
    d.fillScreen(TFT_BLACK);
    d.setFont(&fonts::efontJA_16);
    draw_header("Wi-Fiを選んでね");
    for (size_t i = 0; i < row_count(); i++) {
        const int  y      = HEADER_HEIGHT + i * ROW_HEIGHT;
        const int  center = y + ROW_HEIGHT / 2;
        const int  right  = d.width() - ROW_MARGIN * 2;
        const bool found  = rssi[i] != RSSI_NOT_FOUND;
        const uint16_t color = !found ? TFT_DARKGREY : i == highlighted ? TFT_DARKGREEN : TFT_NAVY;
        d.fillRoundRect(ROW_MARGIN, y + 2, d.width() - ROW_MARGIN * 2, ROW_HEIGHT - 4, 6, color);
        d.setTextColor(found ? TFT_WHITE : TFT_LIGHTGREY);
        d.setTextDatum(middle_left);
        d.drawString(NETWORKS[i].name, ROW_MARGIN * 2, center);
        if (found) {
            draw_signal(right, center, rssi[i]);
        } else {
            d.setTextDatum(middle_right);
            d.drawString("圏外", right, center);
        }
    }

    const int y = HEADER_HEIGHT + row_count() * ROW_HEIGHT;
    d.fillRoundRect(ROW_MARGIN, y + 2, d.width() - ROW_MARGIN * 2, ROW_HEIGHT - 4, 6, TFT_MAROON);
    d.setTextColor(TFT_WHITE);
    d.setTextDatum(middle_left);
    d.drawString("オフライン", ROW_MARGIN * 2, y + ROW_HEIGHT / 2);
}

// タッチで選ばれるまで待ち、その行を返す。オフラインの行は row_count() になる。
// 圏外の接続先もスキャンで取りこぼすことがあるので選べるようにしておく。
static size_t select_network(size_t highlighted) {
    int rssi[MAX_NETWORK_ROWS];
    scan_networks(rssi);
    draw_list(highlighted, rssi);
    for (;;) {
        M5.update();
        const auto touch = M5.Touch.getDetail();
        if (touch.wasPressed() && touch.y >= HEADER_HEIGHT) {
            const size_t row = (touch.y - HEADER_HEIGHT) / ROW_HEIGHT;
            if (row <= row_count()) {
                return row;
            }
        }
        delay(10);
    }
}

static bool try_connect(const Network& network) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(network.ssid, network.password);

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > CONNECT_TIMEOUT_MS) {
            return false;
        }
        delay(250);
    }
    return true;
}

Result connect() {
    size_t index = load_last_index();
    for (;;) {
        index = select_network(index);
        if (index == row_count()) {
            WiFi.mode(WIFI_OFF);
            Serial.println("Wi-Fi offline");
            return Result::Offline;
        }
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setFont(&fonts::efontJA_16);
        draw_header("接続中: " + String(NETWORKS[index].name));
        Serial.printf("Wi-Fi connecting: %s\n", NETWORKS[index].ssid);

        if (try_connect(NETWORKS[index])) {
            save_last_index(index);
            return Result::Connected;
        }
        Serial.printf("Wi-Fi connection failed: %s\n", NETWORKS[index].ssid);
    }
}

}  // namespace wifi_select
