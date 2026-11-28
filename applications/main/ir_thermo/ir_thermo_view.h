#pragma once

#include <gui/view.h>

typedef enum {
    IrThermoStateIdle,     /* Not measuring, waiting for OK */
    IrThermoStateMeasuring, /* Sampling live */
    IrThermoStateStopped,  /* Stopped, showing last value */
    IrThermoStateError,    /* Sensor not present / read error */
} IrThermoState;

typedef struct {
    IrThermoState state;
    int16_t temp_x100; /* centidegrees C */
    uint16_t emissivity_x100; /* e.g. 98 = 0.98 */
    bool need_restart; /* emissivity changed, prompt a reboot */
} IrThermoViewModel;

/** Allocate the temperature view. */
View* ir_thermo_view_alloc(void);

/** Free the temperature view. */
void ir_thermo_view_free(View* view);

/** Hook the OK-key action (start / stop measuring). */
void ir_thermo_view_set_ok_callback(View* view, void (*callback)(void*), void* context);

/** Hook the left/right key action (adjust emissivity). dir: -1 = left, +1 = right. */
void ir_thermo_view_set_adj_callback(View* view, void (*callback)(int dir, void*), void* context);

/** Atomically update the read-out. */
void ir_thermo_view_set_data(
    View* view,
    IrThermoState state,
    int16_t temp_x100,
    uint16_t emissivity_x100);

/** Set the "emissivity changed, reboot needed" flag. */
void ir_thermo_view_set_restart_hint(View* view, bool hint);
