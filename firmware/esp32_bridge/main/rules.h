#pragma once
#include <stdbool.h>
#include <stdint.h>

// Presence automation. A rule fires ONCE when its condition (presence or absence) has held
// continuously for `duration_s`, driving the AC toward a target *state* (off/cool/dry/fan_only)
// via the idempotent command worker — so it never blindly re-presses a button (which, for an
// open-loop toggle like power, would undo itself). No cooldown: the action is state-aware, and
// the rule re-arms only when its condition ends. The presence signal is debounced so brief
// LD2410 dropouts don't reset the sustained timer.
#define RULES_MAX        6
#define RULE_TARGET_LEN  12
#define RULE_MAX_SECS    (7u * 24 * 3600)     // duration clamp

enum { RULE_COND_PRESENCE = 0, RULE_COND_ABSENCE = 1 };

typedef struct {
    bool     enabled;
    uint8_t  cond;                     // RULE_COND_PRESENCE | RULE_COND_ABSENCE
    uint32_t duration_s;               // condition sustained (debounced) this long -> fire once
    char     target[RULE_TARGET_LEN];  // desired AC state: "off"/"cool"/"dry"/"fan_only"
} rule_t;

void rules_load(void);                 // load from NVS and start the evaluator task
int  rules_get(rule_t *out, int max);  // copy current rules; returns the count
bool rules_set(const rule_t *in, int count); // validate (target must be a valid mode), persist

// Live status (debounced presence).
bool     rules_presence(void);
uint32_t rules_presence_secs(void);    // seconds the debounced presence has held (0 if absent)
