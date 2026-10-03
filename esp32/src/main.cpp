#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <algorithm>
#include <exception>
#include <math.h>
#include <time.h>

#include "esp32_http_client.h"
#include "healthtracker/google_health_client.hpp"
#include "healthtracker/oauth_client.hpp"
#include "healthtracker/step_summary.hpp"
#include "healthtracker/time_utils.hpp"
#include "secrets.h"

namespace {

// ESP32 DEVKIT V1 & Adafruit 2.2" 240x320 ILI9340/ILI9341 TFT
constexpr int TFT_SCK_PIN = 18;
constexpr int TFT_MOSI_PIN = 22;
constexpr int TFT_CS_PIN = 5;
constexpr int TFT_DC_PIN = 2;
constexpr int TFT_RST_PIN = -1;

constexpr uint16_t BG_COLOR = ILI9341_BLACK;
constexpr uint16_t TEXT_COLOR = ILI9341_WHITE;
constexpr uint16_t ERROR_COLOR = ILI9341_RED;
constexpr uint16_t TRACK_COLOR = ILI9341_DARKGREY;
constexpr uint16_t ZONE_RING_COLOR = ILI9341_CYAN;
constexpr uint16_t STEPS_BAR_COLOR = ILI9341_GREEN;

// The Google Health API has no way to fetch user goals, so these are
// entered manually for the display.
constexpr long long STEPS_GOAL = 5000;
constexpr long long ZONE_MINUTES_GOAL = 45;

constexpr unsigned long POLL_INTERVAL_MS = 10UL * 60UL * 1000UL;  // 10 minutes
constexpr unsigned long RETRY_INTERVAL_MS = 60UL * 1000UL;  // 1 minute

constexpr int HEADER_HEIGHT = 26;

// Self-heal safety net for a live-observed overnight lockup (see loop()):
constexpr unsigned long MAX_UPTIME_MS = 12UL * 60UL * 60UL * 1000UL;  // 12 hours
constexpr int MAX_POLLS_BEFORE_RESTART = 200;  // trusts only a plain increment
constexpr int MAX_CONSECUTIVE_POLL_FAILURES_BEFORE_RESTART = 5;

constexpr uint32_t WATCHDOG_TIMEOUT_SEC = 90;

// bounds how long it will wait for a WiFi outage to resolve.
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 5UL * 60UL * 1000UL;  // 5 minutes
// A stuck WiFi connect can outlast even ESP.restart(); cycle radio
// on/off to actually reset instead of relying on WiFi.begin()
constexpr unsigned long WIFI_FLUSH_INTERVAL_MS = 60UL * 1000UL;  // 1 minute
constexpr const char* NTP_SERVER = "pool.ntp.org";

Adafruit_ILI9341 tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
Esp32HttpClient http_client;
healthtracker::OAuthClient oauth(http_client, GOOGLE_CLIENT_ID, GOOGLE_CLIENT_SECRET,
                                  GOOGLE_REFRESH_TOKEN);
healthtracker::GoogleHealthClient health_client(http_client, oauth);

unsigned long last_poll_ms = 0;
unsigned long next_clock_update_ms = 0;  // millis() deadline for the next header redraw
int poll_count_this_boot = 0;
bool last_poll_ok = true;
int consecutive_poll_failures = 0;

// A same-day floor under whatever a poll fetches, guarding against a
// transient hiccup returning a lower value than an earlier poll already established
long long steps_high_water_mark = 0;
long long zone_minutes_high_water_mark = 0;
int high_water_mark_local_day = -1;

bool fetch_error_active = false;

// Simple status screen: a couple of lines of size-2 text, top-left aligned.
void StatusMessage(const String& line0, const String& line1 = "",
                    uint16_t color = TEXT_COLOR) {
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(color);
    tft.setTextSize(2);
    tft.setCursor(0, 0);
    tft.println(line0);
    if (line1.length()) {
        tft.setCursor(0, 20);
        tft.println(line1);
    }
}

// Donut-style progress ring, drawn as a filled track with a colored arc
// (manual radial sweep, since Adafruit_GFX has no arc-fill primitive).
void DrawProgressRing(int cx, int cy, int outer_r, int thickness, float pct,
                       uint16_t fill_color) {
    pct = constrain(pct, 0.0f, 1.0f);
    const int inner_r = outer_r - thickness;

    tft.fillCircle(cx, cy, outer_r, TRACK_COLOR);
    tft.fillCircle(cx, cy, inner_r, BG_COLOR);

    const float sweep_degrees = 360.0f * pct;
    for (float deg = 0; deg <= sweep_degrees; deg += 1.0f) {
        const float rad = (deg - 90.0f) * (PI / 180.0f);
        const float c = cosf(rad);
        const float s = sinf(rad);
        tft.drawLine(cx + inner_r * c, cy + inner_r * s, cx + outer_r * c,
                     cy + outer_r * s, fill_color);
    }
}

// Horizontal bar: an outlined track fully drawn, filled proportionally to
// `pct` from the left.
void DrawProgressBar(int x, int y, int w, int h, float pct, uint16_t fill_color) {
    pct = constrain(pct, 0.0f, 1.0f);
    tft.drawRect(x, y, w, h, TRACK_COLOR);
    tft.fillRect(x + 2, y + 2, w - 4, h - 4, BG_COLOR);
    const int fill_w = static_cast<int>((w - 4) * pct);
    if (fill_w > 0) {
        tft.fillRect(x + 2, y + 2, fill_w, h - 4, fill_color);
    }
}

void ConnectWiFi() {
    StatusMessage("Connecting WiFi...");
    WiFi.mode(WIFI_STA);
    // Disconnect first: repeated WiFi.begin() without it is a known ESP32
    // Arduino core leak, which adds up over many reconnects unattended.
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wl_status_t last_status = WL_IDLE_STATUS;
    const unsigned long connect_start_ms = millis();
    unsigned long last_flush_ms = connect_start_ms;
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - connect_start_ms >= WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("WiFi connect timed out after 5 minutes; restarting");
            StatusMessage("Failed to connect to WiFi.", "Restarting...", ERROR_COLOR);
            delay(2000);
            ESP.restart();
        }
        if (millis() - last_flush_ms >= WIFI_FLUSH_INTERVAL_MS) {
            Serial.println("\nStill not connected; flushing WiFi radio and retrying");
            WiFi.disconnect(/*wifioff=*/true);
            delay(100);
            esp_task_wdt_reset();
            WiFi.mode(WIFI_STA);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            last_flush_ms = millis();
            last_status = WL_IDLE_STATUS;
        }
        delay(500);
        esp_task_wdt_reset();  // this can run long on a bad connection
        Serial.print('.');
        const wl_status_t status = WiFi.status();
        if (status != last_status) {
            Serial.printf("\nWiFi status changed: %d\n", status);
            last_status = status;
        }
    }
    Serial.println();
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());

    //Modem auto sleep messes up the fetch timing, easier just to disable it
    // NOTE: If trying to save battery power you should modify this so that the program works as follows:
    // Fetch, then put everything to sleep and just display what was fetched
    // Wake up WiFi modem a bit before you're scheduled to fetch, then repeat previous step
    WiFi.setSleep(false);
}

// GMT_OFFSET_SEC/DAYLIGHT_OFFSET_SEC come from secrets.h, since the ESP32
// has no on-device IANA timezone database.
void SyncTime() {
    StatusMessage("Syncing time...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    Serial.print("Waiting for NTP time sync");
    time_t now = time(nullptr);
    while (now < 8 * 3600 * 2) {  // still near the epoch => not synced yet
        delay(250);
        Serial.print('.');
        now = time(nullptr);
    }
    Serial.println();
}

struct DailyTotalFetch {
    // No value if this poll didn't establish a new total
    std::optional<long long> value;
    // False only for a genuine fetch failure (network/HTTP error, or a
    // top-level response shape FetchDailyTotal doesn't recognize)
    bool ok = true;
};

// Fetches the day's server-side aggregated total for `data_type_path` via
// GoogleHealthClient::dailyRollup, with one retry on failure. `quiet`
// suppresses all logging for this call
DailyTotalFetch FetchDailyTotal(const char* data_type_path, healthtracker::CivilDate today,
                                 std::optional<long long> (*extract)(const nlohmann::json&),
                                 bool quiet) {
    constexpr int MAX_ATTEMPTS = 2;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        try {
            const auto response = health_client.DailyRollup(data_type_path, today);
            const std::optional<long long> value = extract(response);
            return {value, value.has_value()};
        } catch (const healthtracker::MissingDataPointError& e) {
            if (!quiet) {
                Serial.printf("%s: ", data_type_path);
                Serial.println(e.what());
            }
            return {std::nullopt, true};
        } catch (const std::exception& e) {
            if (!quiet) {
                Serial.printf("%s fetch attempt %d/%d failed: ", data_type_path, attempt,
                               MAX_ATTEMPTS);
                Serial.println(e.what());
            }
            if (attempt < MAX_ATTEMPTS) delay(2000);
        }
    }
    return {std::nullopt, false};
}

// Draws the zone-minutes ring plus its "N / Goal" label.
void DrawZoneMinutes(long long total) {
    tft.setTextSize(2);
    tft.setCursor(5, HEADER_HEIGHT + 15);
    tft.print("Zone Minutes:");
    tft.setCursor(5, HEADER_HEIGHT + 40);
    tft.print(String(static_cast<long>(total)) + " / " +
              String(static_cast<long>(ZONE_MINUTES_GOAL)));

    constexpr int RING_CX = 260;
    const int ring_cy = HEADER_HEIGHT + 45;
    constexpr int RING_OUTER_R = 38;
    constexpr int RING_THICKNESS = 10;
    const float pct =
        ZONE_MINUTES_GOAL > 0 ? static_cast<float>(total) / ZONE_MINUTES_GOAL : 0.0f;
    DrawProgressRing(RING_CX, ring_cy, RING_OUTER_R, RING_THICKNESS, pct, ZONE_RING_COLOR);

    const String label = String(static_cast<int>(pct * 100 + 0.5f)) + "%";
    int16_t bx, by;
    uint16_t bw, bh;
    tft.getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
    tft.setCursor(RING_CX - bw / 2, ring_cy - bh / 2);
    tft.print(label);
}

// Draws the "Steps Today: N / Goal" line plus its progress bar.
void DrawSteps(long long total) {
    tft.setTextSize(2);
    tft.setCursor(5, HEADER_HEIGHT + 120);
    tft.print("Steps Today: " + String(static_cast<long>(total)) + " / " +
               String(static_cast<long>(STEPS_GOAL)));

    constexpr int BAR_X = 5;
    const int bar_y = HEADER_HEIGHT + 150;
    constexpr int BAR_W = 250;
    constexpr int BAR_H = 24;
    const float pct = STEPS_GOAL > 0 ? static_cast<float>(total) / STEPS_GOAL : 0.0f;
    DrawProgressBar(BAR_X, bar_y, BAR_W, BAR_H, pct, STEPS_BAR_COLOR);

    tft.setCursor(BAR_X + BAR_W + 10, bar_y + 4);
    tft.print(String(static_cast<int>(pct * 100 + 0.5f)) + "%");
}

// Small red label displaying if there was a fetch error. Doesn't disturb rest of display
void DrawFetchErrorLabel(bool show) {
    constexpr int LABEL_X = 5;
    const int label_y = HEADER_HEIGHT + 185;
    constexpr int LABEL_W = 160;
    constexpr int LABEL_H = 20;
    tft.fillRect(LABEL_X, label_y, LABEL_W, LABEL_H, BG_COLOR);
    if (!show) return;
    tft.setTextColor(ERROR_COLOR);
    tft.setTextSize(2);
    tft.setCursor(LABEL_X, label_y);
    tft.print("Fetch error");
}

// Top header displays local time & DD/MM to the right
void DrawHeader() {
    const std::time_t now = time(nullptr);
    std::tm local_tm{};
    localtime_r(&now, &local_tm);

    int local_hr = local_tm.tm_hour;
    int display_hr;
    const char *ampm;

    if (local_hr == 0) {
        display_hr = 12;
        ampm = "AM";
    } else if (local_hr == 12) {
        display_hr = 12;
        ampm = "PM";
    } else if (local_hr > 12) {
        display_hr = local_hr - 12;
        ampm = "PM";
    } else {
        display_hr = local_hr;
        ampm = "AM";
    }

    const std::string display_min =
        (local_tm.tm_min < 10 ? "0" : "") + std::to_string(local_tm.tm_min);
    std::string time_buf = std::to_string(display_hr) + ":" + display_min + ampm;

    char date_buf[6];  // "DD/MM"
    std::strftime(date_buf, sizeof(date_buf), "%d/%m", &local_tm);

    tft.fillRect(0, 0, tft.width(), HEADER_HEIGHT, BG_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(2);

    tft.setCursor(5, 4);
    tft.print(time_buf.c_str());

    int16_t bx, by;
    uint16_t bw, bh;
    tft.getTextBounds(date_buf, 0, 0, &bx, &by, &bw, &bh);
    tft.setCursor(tft.width() - bw - 5, 4);
    tft.print(date_buf);

    tft.drawFastHLine(0, HEADER_HEIGHT - 4, tft.width(), TEXT_COLOR);
}

// Milliseconds from now until the next local wall-clock minute boundary
// (":00" seconds). Scheduling the header redraw against this — recomputed
// fresh off the real clock every time to avoid drift
unsigned long MillisUntilNextMinute() {
    const std::time_t now = time(nullptr);
    std::tm local_tm{};
    localtime_r(&now, &local_tm);
    return static_cast<unsigned long>(60 - local_tm.tm_sec) * 1000UL;
}

// Full redraw from the current high-water-mark value. Used on startup, on
// a fresh local day, and after any successful fetch. Implicitly clears the
// "Fetch error" label
void RenderDashboard() {
    tft.fillScreen(BG_COLOR);
    DrawHeader();
    tft.setTextColor(TEXT_COLOR);
    DrawZoneMinutes(zone_minutes_high_water_mark);
    DrawSteps(steps_high_water_mark);
}

// Zeroes the high-water marks and redraws the dashboard the first time this
// is called on a new local calendar day independent of poll timing
void ResetForNewLocalDay(healthtracker::CivilDate today) {
    const int today_key = today.year * 10000 + today.month * 100 + today.day;
    if (today_key == high_water_mark_local_day) return;
    high_water_mark_local_day = today_key;
    steps_high_water_mark = 0;
    zone_minutes_high_water_mark = 0;
    fetch_error_active = false;
    Serial.println("New local day: steps/zone minutes reset to 0");
    RenderDashboard();
}

// Returns false if neither steps nor zone minutes could be genuinely
// fetched this poll so the caller can retry sooner than the normal cadence.
bool PollAndDisplay() {
    // Both metrics still at 0 means nothing has synced for today yet
    const bool quiet = steps_high_water_mark == 0 && zone_minutes_high_water_mark == 0;

    if (!quiet) Serial.printf("Polling Google Health API... (free heap: %u)\n", ESP.getFreeHeap());
    const healthtracker::CivilDate today = healthtracker::LocalToday();

    const DailyTotalFetch steps_fetch =
        FetchDailyTotal("steps", today, healthtracker::ExtractDailyStepCount, quiet);
    if (steps_fetch.value) {
        steps_high_water_mark = std::max(steps_high_water_mark, *steps_fetch.value);
        Serial.printf("Steps: %lld (free heap: %u)\n", steps_high_water_mark, ESP.getFreeHeap());
    } else if (!quiet) {
        Serial.println("Steps: no new value this poll; keeping last known value");
    }

    delay(500);
    esp_task_wdt_reset();  // extra margin mid-poll on top of loop()'s own feed

    const DailyTotalFetch zone_fetch = FetchDailyTotal(
        "active-zone-minutes", today, healthtracker::ExtractDailyZoneMinutes, quiet);
    if (zone_fetch.value) {
        zone_minutes_high_water_mark = std::max(zone_minutes_high_water_mark, *zone_fetch.value);
        Serial.printf("Zone minutes: %lld (free heap: %u)\n", zone_minutes_high_water_mark,
                      ESP.getFreeHeap());
    } else if (!quiet) {
        Serial.println("Zone minutes: no new value this poll; keeping last known value");
    }

    const bool fetch_failed = !steps_fetch.ok && !zone_fetch.ok;
    if (fetch_failed && quiet) {
        // Nothing synced yet today and this fetch didn't change that —
        // silently keep waiting for the normal poll cadence.
        fetch_error_active = false;
        return true;
    }
    if (fetch_failed) {
        fetch_error_active = true;
        DrawFetchErrorLabel(true);
        return false;
    }

    fetch_error_active = false;
    RenderDashboard();
    return true;
}

}  // namespace

void setup() {
    Serial.begin(115200);
    esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, /*panic=*/true);
    esp_task_wdt_add(NULL);  // watch the current (loop) task

    // Settling delay: on a cold power-on the display's power rail may not
    // have stabilized yet when SPI init commands start, leaving it blank
    // for the session with no error reported (tft.begin() has no return
    // value to check).
    delay(200);
    SPI.begin(TFT_SCK_PIN, /*miso=*/-1, TFT_MOSI_PIN, TFT_CS_PIN);
    tft.begin();
    tft.setRotation(1);  // landscape, 320x240; use 3 to flip 180 degrees
    StatusMessage("Health Tracker", "Starting...");

    ConnectWiFi();
    SyncTime();

    ResetForNewLocalDay(healthtracker::LocalToday());
    DrawHeader();
    next_clock_update_ms = millis() + MillisUntilNextMinute();

    last_poll_ok = PollAndDisplay();
    last_poll_ms = millis();
}

void loop() {
    esp_task_wdt_reset();

    if (WiFi.status() != WL_CONNECTED) {
        ConnectWiFi();
    }

    ResetForNewLocalDay(healthtracker::LocalToday());

    if (static_cast<long>(millis() - next_clock_update_ms) >= 0) {
        DrawHeader();
        next_clock_update_ms = millis() + MillisUntilNextMinute();
    }

    const unsigned long interval = last_poll_ok ? POLL_INTERVAL_MS : RETRY_INTERVAL_MS;
    if (millis() - last_poll_ms >= interval) {
        last_poll_ok = PollAndDisplay();
        last_poll_ms = millis();
        ++poll_count_this_boot;
        consecutive_poll_failures = last_poll_ok ? 0 : consecutive_poll_failures + 1;

        // Three independent restart triggers for the lockup described above
        // MAX_UPTIME_MS: trusts millis(). MAX_POLLS_BEFORE_RESTART: trusts
        // only a plain increment, in case millis() itself is unreliable.
        // MAX_CONSECUTIVE_POLL_FAILURES_BEFORE_RESTART: a separate case,
        // for sitting on the "Update failed" screen poll after poll.
        if (millis() >= MAX_UPTIME_MS || poll_count_this_boot >= MAX_POLLS_BEFORE_RESTART ||
            consecutive_poll_failures >= MAX_CONSECUTIVE_POLL_FAILURES_BEFORE_RESTART) {
            Serial.println("Restarting (scheduled max uptime, poll count, or consecutive poll failures reached)");
            ESP.restart();
        }
    }

    delay(200);
}
