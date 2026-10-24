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
static constexpr uint32_t SELECT_TIMEOUT_MS  = 5000;  // 触らなければ前回の接続先を選ぶ

// 一覧の見た目。画面に収まる5件まで出す。
static constexpr int    HEADER_HEIGHT = 40;
static constexpr int    ROW_HEIGHT    = 40;
static constexpr int    ROW_MARGIN    = 8;
static constexpr size_t MAX_ROWS      = 5;

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
    return min(NETWORK_COUNT, MAX_ROWS);
}

static void draw_header(const String& text) {
    auto& d = M5.Display;
    d.fillRect(0, 0, d.width(), HEADER_HEIGHT, TFT_BLACK);
    d.setTextColor(TFT_WHITE);
    d.setTextDatum(middle_left);
    d.drawString(text, ROW_MARGIN, HEADER_HEIGHT / 2);
}

static void draw_list(size_t highlighted) {
    auto& d = M5.Display;
    d.fillScreen(TFT_BLACK);
    d.setFont(&fonts::efontJA_16);
    for (size_t i = 0; i < row_count(); i++) {
        const int y = HEADER_HEIGHT + i * ROW_HEIGHT;
        const uint16_t color = i == highlighted ? TFT_DARKGREEN : TFT_NAVY;
        d.fillRoundRect(ROW_MARGIN, y + 2, d.width() - ROW_MARGIN * 2, ROW_HEIGHT - 4, 6, color);
        d.setTextColor(TFT_WHITE);
        d.setTextDatum(middle_left);
        d.drawString(NETWORKS[i].name, ROW_MARGIN * 2, y + ROW_HEIGHT / 2);
    }
}

// タッチで選ばれた行を返す。時間切れなら default_index を返す。
static size_t select_network(size_t default_index) {
    draw_list(default_index);
    const uint32_t start = millis();
    int shown_seconds = -1;
    for (;;) {
        const uint32_t elapsed = millis() - start;
        if (elapsed >= SELECT_TIMEOUT_MS) {
            return default_index;
        }
        const int seconds = (SELECT_TIMEOUT_MS - elapsed + 999) / 1000;
        if (seconds != shown_seconds) {
            shown_seconds = seconds;
            draw_header("Wi-Fiを選んでね (" + String(seconds) + ")");
        }

        M5.update();
        const auto touch = M5.Touch.getDetail();
        if (touch.wasPressed() && touch.y >= HEADER_HEIGHT) {
            const size_t row = (touch.y - HEADER_HEIGHT) / ROW_HEIGHT;
            if (row < row_count()) {
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

bool connect() {
    size_t index = load_last_index();
    for (;;) {
        if (NETWORK_COUNT > 1) {
            index = select_network(index);
        }
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setFont(&fonts::efontJA_16);
        draw_header("接続中: " + String(NETWORKS[index].name));
        Serial.printf("Wi-Fi connecting: %s\n", NETWORKS[index].ssid);

        if (try_connect(NETWORKS[index])) {
            save_last_index(index);
            return true;
        }
        Serial.printf("Wi-Fi connection failed: %s\n", NETWORKS[index].ssid);
        if (NETWORK_COUNT <= 1) {
            return false;
        }
    }
}

}  // namespace wifi_select
