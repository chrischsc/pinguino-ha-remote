#include "ac_state.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"

#define NVS_NS  "acstate"
#define NVS_KEY "st"
#define NVS_VER 2          // bumped: timer_h -> timer_on/timer_halfh

// After pressing timer (or up/down while editing it), up/down adjust the timer for this long;
// afterwards they return to adjusting temperature, mirroring the device's brief edit mode.
#define TIMER_EDIT_US (5 * 1000000)

static const char *TAG = "acstate";

static ac_state_t s = {
    .on = false, .mode = AC_MODE_COOL, .temp_c = AC_TEMP_DEF, .fan = AC_FAN_AUTO,
    .silent = false, .eco = false, .swing = false,
    .timer_on = false, .timer_halfh = TIMER_DEF_HALFH,
};
static int64_t s_timer_edit_until_us;
static SemaphoreHandle_t s_lock;
static ac_state_cb_t     s_cb;
#define LOCK()   do { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); } while (0)
#define UNLOCK() do { if (s_lock) xSemaphoreGive(s_lock); } while (0)

// Enforce the manual's mode-dependent constraints on a state in place.
static void normalize(ac_state_t *st)
{
    if (st->temp_c < AC_TEMP_MIN) st->temp_c = AC_TEMP_MIN;
    if (st->temp_c > AC_TEMP_MAX) st->temp_c = AC_TEMP_MAX;
    if (st->timer_halfh < TIMER_MIN_HALFH) st->timer_halfh = TIMER_MIN_HALFH;
    if (st->timer_halfh > TIMER_MAX_HALFH) st->timer_halfh = TIMER_MAX_HALFH;
    if (st->mode != AC_MODE_COOL) { st->silent = false; st->eco = false; }   // COOL-only flags
    if (st->mode == AC_MODE_DRY)  st->fan = AC_FAN_AUTO;                      // dry forces auto
    if (st->mode == AC_MODE_FAN && st->fan == AC_FAN_AUTO) st->fan = AC_FAN_MAX; // no auto in fan
}

static void persist(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    uint8_t ver = NVS_VER;
    if (nvs_set_u8(h, "ver", ver) == ESP_OK)
        nvs_set_blob(h, NVS_KEY, &s, sizeof(s));
    nvs_commit(h);
    nvs_close(h);
}

static void notify(void)
{
    if (!s_cb) return;
    ac_state_t snap;
    LOCK(); snap = s; UNLOCK();
    s_cb(&snap);
}

void ac_state_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t ver = 0; size_t sz = sizeof(s); ac_state_t tmp;
        if (nvs_get_u8(h, "ver", &ver) == ESP_OK && ver == NVS_VER &&
            nvs_get_blob(h, NVS_KEY, &tmp, &sz) == ESP_OK && sz == sizeof(s)) {
            s = tmp;
            normalize(&s);
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "init: on=%d mode=%d temp=%d fan=%d", s.on, s.mode, s.temp_c, s.fan);
}

const ac_state_t *ac_state_get(void) { return &s; }
void ac_state_get_copy(ac_state_t *out) { LOCK(); *out = s; UNLOCK(); }

bool ac_state_apply(const char *btn)
{
    LOCK();
    ac_state_t before = s;
    int64_t now = esp_timer_get_time();
    bool timer_edit = now < s_timer_edit_until_us;   // up/down adjust the timer, not temp

    if (!strcmp(btn, "power")) {
        s.on = !s.on;
        s_timer_edit_until_us = 0;
    } else if (!strcmp(btn, "timer")) {
        // Toggle the timer; switching it on starts at 6.0 h and opens the edit window.
        if (s.timer_on) { s.timer_on = false; s_timer_edit_until_us = 0; }
        else { s.timer_on = true; s.timer_halfh = TIMER_DEF_HALFH; s_timer_edit_until_us = now + TIMER_EDIT_US; }
    } else if (timer_edit && (!strcmp(btn, "up") || !strcmp(btn, "down"))) {
        // 0.5 h steps from 0.5 to 9.5, then 1 h steps from 10 to 24 (half-hours: 20 == 10 h).
        int hh = s.timer_halfh + (!strcmp(btn, "up") ? (s.timer_halfh < 20 ? 1 : 2)
                                                      : (s.timer_halfh > 20 ? -2 : -1));
        if (hh < TIMER_MIN_HALFH) hh = TIMER_MIN_HALFH;
        if (hh > TIMER_MAX_HALFH) hh = TIMER_MAX_HALFH;
        s.timer_halfh = (uint8_t)hh;
        s_timer_edit_until_us = now + TIMER_EDIT_US;   // each tweak extends the edit window
    } else if (!s.on) {
        // In standby the AC ignores everything but power and timer. No model change.
    } else if (!strcmp(btn, "mode")) {
        // OBSERVED cycle: cool -> dry -> fan -> (wrap) cool. Confirmed on the device — going
        // fan -> dry takes two "mode" presses (fan -> cool -> dry), which only this order gives.
        s.mode = (ac_mode_t)((s.mode + 1) % 3);
    } else if (!strcmp(btn, "up") || !strcmp(btn, "down")) {
        if (s.mode == AC_MODE_COOL) {                    // setpoint is COOL-only
            int t = (int)s.temp_c + (btn[0] == 'u' ? 1 : -1);
            if (t >= AC_TEMP_MIN && t <= AC_TEMP_MAX) s.temp_c = (uint8_t)t;
        }
    } else if (!strcmp(btn, "fan")) {
        if (s.mode == AC_MODE_COOL)      s.fan = (ac_fan_t)((s.fan + 1) % 4);  // min/med/max/auto
        else if (s.mode == AC_MODE_FAN)  s.fan = (ac_fan_t)((s.fan + 1) % 3);  // min/med/max
        // dry: fan forced auto, ignore
    } else if (!strcmp(btn, "silent")) {
        if (s.mode == AC_MODE_COOL) s.silent = !s.silent;
    } else if (!strcmp(btn, "eco")) {
        if (s.mode == AC_MODE_COOL) s.eco = !s.eco;
    } else if (!strcmp(btn, "flap")) {
        s.swing = !s.swing;
    }
    // "timer" intentionally not modeled in v1 (it arms a delayed on/off whose hours are entered
    // via subsequent up/down — ambiguous to track open-loop). The press still relays to the AC.

    normalize(&s);
    bool changed = memcmp(&before, &s, sizeof(s)) != 0;
    UNLOCK();
    if (changed) { persist(); notify(); }
    return changed;
}

void ac_state_set(const ac_state_t *in)
{
    LOCK();
    s = *in;
    normalize(&s);
    UNLOCK();
    persist();
    notify();
}

const char *ac_mode_ha(const ac_state_t *st)
{
    if (!st->on) return "off";
    switch (st->mode) {
        case AC_MODE_COOL: return "cool";
        case AC_MODE_DRY:  return "dry";
        default:           return "fan_only";
    }
}

const char *ac_fan_str(ac_fan_t f)
{
    switch (f) {
        case AC_FAN_MIN: return "min";
        case AC_FAN_MED: return "medium";
        case AC_FAN_MAX: return "max";
        default:         return "auto";
    }
}

bool ac_fan_from_str(const char *str, ac_fan_t *out)
{
    if (!strcmp(str, "min"))    { *out = AC_FAN_MIN;  return true; }
    if (!strcmp(str, "medium")) { *out = AC_FAN_MED;  return true; }
    if (!strcmp(str, "max"))    { *out = AC_FAN_MAX;  return true; }
    if (!strcmp(str, "auto"))   { *out = AC_FAN_AUTO; return true; }
    return false;
}

bool ac_mode_from_ha(const char *str, ac_mode_t *out, bool *want_on)
{
    if (!strcmp(str, "off"))      { *want_on = false; *out = AC_MODE_COOL; return true; }
    if (!strcmp(str, "cool"))     { *want_on = true;  *out = AC_MODE_COOL; return true; }
    if (!strcmp(str, "dry"))      { *want_on = true;  *out = AC_MODE_DRY;  return true; }
    if (!strcmp(str, "fan_only")) { *want_on = true;  *out = AC_MODE_FAN;  return true; }
    return false;
}

void ac_state_on_change(ac_state_cb_t cb) { s_cb = cb; }
