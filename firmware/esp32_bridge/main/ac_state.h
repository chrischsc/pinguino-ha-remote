#pragma once
#include <stdbool.h>
#include <stdint.h>

// Open-loop model of the De'Longhi PAC EL112 CST air-conditioner state.
//
// The emulator is a one-way HID remote: it can only SEND button presses, never read the AC
// back. So we model the AC's logical state from the presses we dispatch (see ac_state_apply),
// applying the manual's mode-dependent rules. This is necessarily open-loop: presses from the
// physical CST remote, or the AC's true power-on state, are invisible and cause drift — hence
// ac_state_set() ("set current state" sync) to re-anchor the model.
//
// Sources: docs/references/Pinguino_PACEL112CST_manual.pdf (button map, mode availability).

typedef enum { AC_MODE_COOL = 0, AC_MODE_DRY = 1, AC_MODE_FAN = 2 } ac_mode_t;
typedef enum { AC_FAN_MIN  = 0, AC_FAN_MED = 1, AC_FAN_MAX = 2, AC_FAN_AUTO = 3 } ac_fan_t;

// Setpoint range — Status: OBSERVED (confirmed on the device: 18–32 °C inclusive).
#define AC_TEMP_MIN   18
#define AC_TEMP_MAX   32
#define AC_TEMP_DEF   24

typedef struct {
    bool      on;        // true = running, false = standby (power resumes the last mode)
    ac_mode_t mode;      // cool / dry / fan
    uint8_t   temp_c;    // setpoint, meaningful in COOL only
    ac_fan_t  fan;       // fan speed
    bool      silent;    // COOL only
    bool      eco;       // COOL only (ECO REAL FEEL)
    bool      swing;     // any mode (flap oscillation)
    uint8_t   timer_h;   // 0 = off; hours. NOTE: not fully modeled in v1 (see ac_state.c)
} ac_state_t;

void               ac_state_init(void);              // load from NVS (or defaults)
const ac_state_t  *ac_state_get(void);               // current modeled state (snapshot via lock)
void               ac_state_get_copy(ac_state_t *out);

// Update the model from a button we just dispatched (called inside uart_link_press). Applies
// the manual's mode-aware semantics; ignores presses that the AC would ignore in the current
// mode (e.g. up/down outside COOL). Returns true if the modeled state changed.
bool               ac_state_apply(const char *btn);

// "Set current state" sync: overwrite the model to match reality (after manual-remote use),
// WITHOUT sending any presses. Persists to NVS. Clamps/normalises invalid combos.
void               ac_state_set(const ac_state_t *s);

// String helpers for the API / Home Assistant (HA climate vocabulary).
const char        *ac_mode_ha(const ac_state_t *s);  // "off"/"cool"/"dry"/"fan_only"
const char        *ac_fan_str(ac_fan_t f);           // "min"/"medium"/"max"/"auto"
bool               ac_fan_from_str(const char *s, ac_fan_t *out);
bool               ac_mode_from_ha(const char *s, ac_mode_t *out, bool *want_on);

// Register a callback fired whenever the model changes (used to push HA state). May be called
// from any task; keep it light (it runs under no lock).
typedef void (*ac_state_cb_t)(const ac_state_t *s);
void               ac_state_on_change(ac_state_cb_t cb);
