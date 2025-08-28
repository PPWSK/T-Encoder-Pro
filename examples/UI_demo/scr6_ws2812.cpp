#include "screen.h"
#include "moon_encoder_ble.h"
#include <lvgl.h>
#include <time.h>     // for getLocalTime on ESP32

// ---------- JSON helpers ----------
static bool jsonFindInt(const String& s, const char* key, int& out){
    String k = "\""; k += key; k += "\":";
    int i = s.indexOf(k);
    if (i < 0) return false;
    i += k.length();
    while (i < (int)s.length() && isspace((int)s[i])) ++i;
    bool neg = false; if (s[i]=='-'){ neg=true; ++i; }
    int val=0, start=i;
    while (i < (int)s.length() && isdigit((int)s[i])){ val = val*10 + (s[i]-'0'); ++i; }
    if (i==start) return false;
    out = neg ? -val : val; return true;
}

static bool jsonFindFloat(const String& s, const char* key, double& out){
    String k = "\""; k += key; k += "\":";
    int i = s.indexOf(k);
    if (i < 0) return false;
    i += k.length();
    while (i < (int)s.length() && isspace((int)s[i])) ++i;
    int j = i;
    while (j < (int)s.length() && (s[j]=='+'||s[j]=='-'||s[j]=='.'|| isdigit((int)s[j]))) ++j;
    if (j==i) return false;
    out = strtod(s.substring(i, j).c_str(), nullptr);
    return true;
}

// ---------- Screen state ----------
static lv_obj_t*  s_arc    = nullptr;
static lv_timer_t* s_timer = nullptr;
static int s_lastSent = -1;

// Return seconds since local midnight using SNTP/RTC if available.
// Returns -1 if no time available.
static int secondsSinceMidnight() {
    struct tm now;
    if (getLocalTime(&now, 50 /*ms*/)) {
        return now.tm_hour*3600 + now.tm_min*60 + now.tm_sec;
    }
    return -1;
}

static void set_arc_value(int v){
    if (!s_arc) return;
    if (v < 0) v = 0; if (v > 100) v = 100;
    lv_arc_set_value(s_arc, v);
}

static void arc_changed(lv_event_t*){
    int v = lv_arc_get_value(s_arc);

    // Quantize to 0.1 steps and clamp
    int tenth = (v + 5) / 10;          // round to nearest 10
    if (tenth < 0) tenth = 0; if (tenth > 10) tenth = 10;
    int quantArc = tenth * 10;         // 0..100 in 10-step
    double f = tenth / 10.0;           // 0.0 .. 1.0 (one decimal)

    if (quantArc == s_lastSent) return;
    s_lastSent = quantArc;

    // Keep the UI aligned to the sent granularity
    if (quantArc != v) set_arc_value(quantArc);

    // Include update_image and send floats with 1 decimal precision
    char buf[120];
    // %.1f guarantees 0.0 style, not "0"
    snprintf(buf, sizeof(buf),
             "{\"day_brightness\":%.1f,\"night_brightness\":%.1f,\"update_image\":true}",
             f, f);

    Serial.printf("[BRI] send day=night=%.1f (arc=%d)\n", f, quantArc);
    sendMoonCommand(String(buf));
}

// Decide whether it's day or night now, given seconds-of-day schedule.
// Handles wrap-around (e.g., day at 09:00 = 32400s, night at 21:00 = 75600s).
static bool isDayNow(int day_time_sec, int night_time_sec, int now_sec){
    if (day_time_sec < 0 || night_time_sec < 0 || now_sec < 0) {
        return true; // default (if time missing)
    }
    if (day_time_sec < night_time_sec) {
        // day: [day..night)
        return (now_sec >= day_time_sec && now_sec < night_time_sec);
    } else if (day_time_sec > night_time_sec) {
        // day wraps across midnight: [day..24h) U [0..night)
        return !(now_sec >= night_time_sec && now_sec < day_time_sec);
    } else {
        // same moment (degenerate) -> arbitrarily pick "day"
        return true;
    }
}

static void tick_cb(lv_timer_t*){
    String js;
    if (!blePopLastJson(js)) return;

    // Expect floats 0..1 and schedule seconds
    double day_b = -1.0, night_b = -1.0;
    int day_t = -1, night_t = -1;
    jsonFindFloat(js, "day_brightness",   day_b);
    jsonFindFloat(js, "night_brightness", night_b);
    jsonFindInt  (js, "day_time",         day_t);    // seconds
    jsonFindInt  (js, "night_time",       night_t);  // seconds

    int now_sec = secondsSinceMidnight();
    bool useDay = isDayNow(day_t, night_t, now_sec);

    double chosen = useDay ? day_b : night_b;

    // Diagnostics: show exactly what we parsed and decided
    Serial.printf("[BRI] get_info: day=%.3f night=%.3f day_t=%d night_t=%d now=%d use=%s\n",
                  day_b, night_b, day_t, night_t, now_sec, useDay ? "day" : "night");

    // If we have no valid brightness from the chosen mode, fall back gracefully
    if (chosen < 0.0) {
        if (day_b >= 0.0) { chosen = day_b; useDay = true; }
        else if (night_b >= 0.0) { chosen = night_b; useDay = false; }
        else { return; } // nothing to show yet
    }

    // Convert 0..1 -> 0..100 for arc (rounded to nearest 10 so UI matches send granularity)
    int arcVal = (int)lround(chosen * 100.0);
    int tenth = (arcVal + 5)/10; arcVal = tenth*10;
    if (arcVal < 0) arcVal = 0; if (arcVal > 100) arcVal = 100;

    Serial.printf("[BRI] show %s brightness=%.3f -> arc=%d\n",
                  useDay ? "day" : "night", chosen, arcVal);

    set_arc_value(arcVal);
}

static void focus_arc(){
    lv_group_t* g = lv_group_get_default();
    if (g) lv_group_focus_obj(s_arc);
}

static void create_ws2812(lv_obj_t* parent){
    back_btn_hidden(false);

    s_arc = lv_arc_create(parent);
    lv_obj_set_size(s_arc, 180, 180);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_bg_angles(s_arc, 135, 45);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(s_arc, arc_changed, LV_EVENT_VALUE_CHANGED, NULL);

    focus_arc();

    if (isConnected()){
        Serial.println("[BRI] requesting get_info");
        sendMoonCommand("{\"get_info\":true}");
    }

    if (s_timer) lv_timer_del(s_timer);
    s_timer = lv_timer_create(tick_cb, 300, NULL);
}

static void entry_ws2812(void){ focus_arc(); }
static void exit_ws2812(void){}
static void destroy_ws2812(void){
    if (s_timer){ lv_timer_del(s_timer); s_timer = nullptr; }
    back_btn_hidden(true);
}

scr_lifecycle_t scr_ws2812 = {
    .create  = create_ws2812,
    .entry   = entry_ws2812,
    .exit    = exit_ws2812,
    .destroy = destroy_ws2812,
};
