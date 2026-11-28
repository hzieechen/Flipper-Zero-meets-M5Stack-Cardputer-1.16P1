#pragma once

#include <gui/view.h>
#include <stdint.h>

typedef enum {
    EnvMonitorLinkIdle,
    EnvMonitorLinkScanning,
    EnvMonitorLinkConnected,
    EnvMonitorLinkError,
} EnvMonitorLinkState;

typedef struct {
    EnvMonitorLinkState link;
    int16_t temp_x100;
    uint8_t humidity;
    uint16_t pressure_x10;
    int32_t altitude_cm;
    uint32_t update_tick;
} EnvMonitorViewModel;

/** Allocate the read-out view (owns its locking view model). */
View* env_monitor_view_alloc(void);

/** Atomically update the model; triggers a redraw. */
void env_monitor_view_set_data(
    View* view,
    EnvMonitorLinkState link,
    int16_t temp_x100,
    uint8_t humidity,
    uint16_t pressure_x10,
    int32_t altitude_cm);

/** Hook the OK-key action (open calibration / retry). */
void env_monitor_view_set_ok_callback(View* view, void (*callback)(void*), void* context);

/** Free the view together with its input context. */
void env_monitor_view_free(View* view);
