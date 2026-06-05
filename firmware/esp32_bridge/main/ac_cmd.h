#pragma once
#include <stdbool.h>

// Translates a desired Home-Assistant target into the sequence of button presses needed to
// reach it from the current modeled state, paced so the AC registers each press. Runs on a
// dedicated worker task so MQTT/HTTP callers never block. Each issued press goes through the
// normal dispatch (relayed to the nRF AND folded into ac_state).
void ac_cmd_init(void);

void ac_cmd_set_mode_ha(const char *ha_mode);          // "off"/"cool"/"dry"/"fan_only"
void ac_cmd_set_temp(int target_c);                    // cooling setpoint
void ac_cmd_set_fan(const char *fan_str);              // "min"/"medium"/"max"/"auto"
void ac_cmd_set_switch(const char *which, bool on);    // "swing"/"eco"/"silent"

// Queue one raw remote-button press through the same paced worker, so rapid taps on the web
// remote are spaced to the AC's touch-button debounce instead of being fired back-to-back
// (which the AC drops, drifting the model). Returns false if the queue is full. "btn" is a
// button name accepted by uart_link (power/mode/up/down/fan/flap/eco/silent/timer).
bool ac_cmd_press(const char *btn);
