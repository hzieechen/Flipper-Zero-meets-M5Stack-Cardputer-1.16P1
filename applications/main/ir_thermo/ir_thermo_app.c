/**
 * @file ir_thermo_app.c
 * @brief Unit NCIR2 infrared thermometer menu.
 *
 * Interaction (per request):
 *   - Enter app: idle state, "press OK to start".
 *   - OK: start continuous sampling (500 ms), live temperature read-out.
 *   - OK again: stop sampling, keep the last measured value on screen.
 *   - Back: exit, delete I2C driver so the Grove pads can be reused.
 *
 * No SceneManager needed: a single view + tick callback covers the whole
 * flow. The tick runs on the GUI thread, so no cross-thread locking is
 * required beyond the view model's own Locking model.
 */
#include "ir_thermo_view.h"

#include <ncir2/ncir2.h>

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <input/input.h>

#define TAG "IrThermo"
#define SAMPLE_PERIOD_MS 500u
#define BREATH_PERIOD_MS 2000u
#define BREATH_STEP_MS 20u

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    View* view;
    volatile bool measuring;
    int16_t last_temp_x100;
    uint16_t emissivity_x100; /* e.g. 98 = 0.98 */
    FuriThread* breath_thread;
    volatile bool breath_running;
} IrThermoApp;

static bool ir_thermo_back_event_callback(void* context) {
    UNUSED(context);
    /* Returning false lets the dispatcher stop and the app exit. */
    return false;
}

static void ir_thermo_ok_callback(void* context) {
    IrThermoApp* app = context;

    /* "Retry" on the error screen: re-probe before starting. */
    if(!ncir2_is_present()) {
        ir_thermo_view_set_data(app->view, IrThermoStateError, app->last_temp_x100, app->emissivity_x100);
        return;
    }

    app->measuring = !app->measuring;
    if(app->measuring) {
        ir_thermo_view_set_data(app->view, IrThermoStateMeasuring, app->last_temp_x100, app->emissivity_x100);
    } else {
        ir_thermo_view_set_data(app->view, IrThermoStateStopped, app->last_temp_x100, app->emissivity_x100);
    }
}

/* Left/right adjusts emissivity by 0.01 (range 0.10..1.00), writes to the
 * sensor and saves to flash each step. */
static void ir_thermo_adj_callback(int dir, void* context) {
    IrThermoApp* app = context;

    int16_t v = (int16_t)app->emissivity_x100 + (dir * 1);
    if(v < 10) v = 10;
    if(v > 100) v = 100;
    app->emissivity_x100 = (uint16_t)v;

    ncir2_set_emissivity((float)app->emissivity_x100 / 100.0f);
    ncir2_save_config();

    /* Refresh the readout without disturbing the current measurement state. */
    IrThermoState st;
    with_view_model(
        app->view,
        IrThermoViewModel * m,
        { st = m->state; },
        false);
    ir_thermo_view_set_data(app->view, st, app->last_temp_x100, app->emissivity_x100);
    ir_thermo_view_set_restart_hint(app->view, true);
    FURI_LOG_I(TAG, "emissivity set to 0.%02u", app->emissivity_x100);
}

static void ir_thermo_on_tick(void* context) {
    IrThermoApp* app = context;
    if(!app->measuring) return;

    int16_t temp_x100 = 0;
    if(ncir2_read_temp(&temp_x100)) {
        app->last_temp_x100 = temp_x100;
        ir_thermo_view_set_data(app->view, IrThermoStateMeasuring, temp_x100, app->emissivity_x100);
    }
    /* A failed read keeps the last good value on screen; next tick retries. */
    /* A failed read keeps the last good value on screen; next tick retries. */
}

/* Breathing blue LED: brightness follows a sine-ish ramp up and down. Runs on
 * its own thread so the I2C writes do not stall the GUI / keyboard loop. */
static int32_t ir_thermo_breath_worker(void* context) {
    IrThermoApp* app = context;
    const uint32_t steps = BREATH_PERIOD_MS / BREATH_STEP_MS;
    uint32_t phase = 0;

    while(app->breath_running) {
        /* Triangle wave: 0 -> max -> 0 -> max -> ... */
        uint32_t t = phase % steps;
        uint32_t half = steps / 2;
        uint8_t b;
        if(t < half) {
            b = (uint8_t)((t * 255u) / half);
        } else {
            b = (uint8_t)(((steps - t) * 255u) / half);
        }
        ncir2_set_led(0, 0, b); /* pure blue */
        phase++;
        furi_delay_ms(BREATH_STEP_MS);
    }

    ncir2_set_led(0, 0, 0); /* off on exit */
    return 0;
}

static void ir_thermo_breath_start(IrThermoApp* app) {
    if(app->breath_thread) return;
    app->breath_running = true;
    app->breath_thread = furi_thread_alloc_ex("Ncir2Breath", 2 * 1024, ir_thermo_breath_worker, app);
    furi_thread_start(app->breath_thread);
}

static void ir_thermo_breath_stop(IrThermoApp* app) {
    if(!app->breath_thread) return;
    app->breath_running = false;
    furi_thread_join(app->breath_thread);
    furi_thread_free(app->breath_thread);
    app->breath_thread = NULL;
}

static IrThermoApp* ir_thermo_app_alloc(void) {
    IrThermoApp* app = malloc(sizeof(IrThermoApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->measuring = false;
    app->last_temp_x100 = 0;
    app->emissivity_x100 = 98;
    app->breath_thread = NULL;
    app->breath_running = false;

    app->view = ir_thermo_view_alloc();
    ir_thermo_view_set_ok_callback(app->view, ir_thermo_ok_callback, app);
    ir_thermo_view_set_adj_callback(app->view, ir_thermo_adj_callback, app);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, ir_thermo_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, ir_thermo_on_tick, SAMPLE_PERIOD_MS);
    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_add_view(app->view_dispatcher, 0, app->view);

    return app;
}

static void ir_thermo_app_free(IrThermoApp* app) {
    ir_thermo_breath_stop(app);
    view_dispatcher_remove_view(app->view_dispatcher, 0);
    view_dispatcher_free(app->view_dispatcher);
    ir_thermo_view_free(app->view);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t ir_thermo_app(void* p) {
    UNUSED(p);

    if(!ncir2_init()) {
        FURI_LOG_E(TAG, "Failed to init I2C for NCIR2");
        return 0;
    }

    IrThermoApp* app = ir_thermo_app_alloc();

    if(ncir2_is_present()) {
        /* Show the sensor's current emissivity (defaults to 0.98 if unreadable). */
        uint16_t raw = 0;
        if(ncir2_read_emissivity(&raw)) {
            uint16_t x100 = (uint16_t)((raw * 100u + 32767u) / 65535u);
            if(x100 >= 10 && x100 <= 100) app->emissivity_x100 = x100;
        }
        ir_thermo_view_set_data(app->view, IrThermoStateIdle, 0, app->emissivity_x100);
        ir_thermo_breath_start(app); /* breathing blue status LED */
    } else {
        FURI_LOG_W(TAG, "NCIR2 not detected at 0x5A");
        ir_thermo_view_set_data(app->view, IrThermoStateError, 0, app->emissivity_x100);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, 0);
    view_dispatcher_run(app->view_dispatcher);

    ir_thermo_app_free(app);
    ncir2_deinit();
    return 0;
}
