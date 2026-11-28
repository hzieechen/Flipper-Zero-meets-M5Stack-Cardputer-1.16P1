/**
 * @file env_monitor_app.c
 * @brief 环境监测 app: Chain ENV read-out + sea-level calibration.
 *
 * Layout follows applications/examples/example_view_dispatcher and the
 * infrared app's SceneManager pattern. The 2s poll thread and the calibration
 * worker both go through env_chain's internal mutex, so they cannot collide
 * on the shared UART.
 */
#include "env_monitor_view.h"

#include <env_chain/env_chain.h>

#include <furi.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/popup.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <input/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "EnvMonitor"
#define POLL_PERIOD_MS 2000u
#define INPUT_BUF_SIZE 24u

typedef enum {
    ViewIdReadout,
    ViewIdCalibMenu,
    ViewIdInput,
    ViewIdPopup,
    ViewIdCount,
} AppViewId;

typedef enum {
    SceneReadout,
    SceneCalibMenu,
    SceneInput,
    SceneResult,
    SceneCount,
} AppScene;

typedef enum {
    EventDataUpdated = 1,
    EventInputDone,
    EventPopupClosed,
    EventCalibDone,
} AppEvent;

typedef enum {
    SubmenuIndexByPressure,
    SubmenuIndexByAltitude,
} CalibSubmenuIndex;

typedef enum {
    CalibModeNone,
    CalibModeByPressure,
    CalibModeByAltitude,
} CalibMode;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    View* readout_view;
    Submenu* calib_menu;
    TextInput* text_input;
    Popup* popup;

    FuriThread* poll_thread;
    volatile bool poll_running;
    volatile bool device_connected;
    char input_buf[INPUT_BUF_SIZE];

    CalibMode calib_mode;
    bool calib_ok;
    FuriThread* calib_thread;
} EnvMonitorApp;

static bool env_monitor_custom_event_callback(void* context, uint32_t event);
static bool env_monitor_back_event_callback(void* context);
static void env_monitor_on_tick(void* context);

/* ------------------------------------------------------------------ */
/* Scene handlers (forward declarations)                                */
/* ------------------------------------------------------------------ */
static void scene_readout_on_enter(void* context);
static bool scene_readout_on_event(void* context, SceneManagerEvent event);
static void scene_readout_on_exit(void* context);
static void scene_calib_menu_on_enter(void* context);
static bool scene_calib_menu_on_event(void* context, SceneManagerEvent event);
static void scene_calib_menu_on_exit(void* context);
static void scene_input_on_enter(void* context);
static bool scene_input_on_event(void* context, SceneManagerEvent event);
static void scene_input_on_exit(void* context);
static void scene_result_on_enter(void* context);
static bool scene_result_on_event(void* context, SceneManagerEvent event);
static void scene_result_on_exit(void* context);

static void (*const s_on_enter_handlers[])(void*) = {
    scene_readout_on_enter,
    scene_calib_menu_on_enter,
    scene_input_on_enter,
    scene_result_on_enter,
};
static bool (*const s_on_event_handlers[])(void*, SceneManagerEvent) = {
    scene_readout_on_event,
    scene_calib_menu_on_event,
    scene_input_on_event,
    scene_result_on_event,
};
static void (*const s_on_exit_handlers[])(void*) = {
    scene_readout_on_exit,
    scene_calib_menu_on_exit,
    scene_input_on_exit,
    scene_result_on_exit,
};
static const SceneManagerHandlers s_scene_handlers = {
    .on_enter_handlers = s_on_enter_handlers,
    .on_event_handlers = s_on_event_handlers,
    .on_exit_handlers = s_on_exit_handlers,
    .scene_num = SceneCount,
};

/* ------------------------------------------------------------------ */
/* Poll thread                                                          */
/* ------------------------------------------------------------------ */
static void env_monitor_apply_reading(EnvMonitorApp* app, EnvChainStatus status, EnvChainData* data) {
    if(status == EnvChainStatusOk) {
        env_monitor_view_set_data(
            app->readout_view,
            EnvMonitorLinkConnected,
            data->temp_aht_x100,
            data->humidity,
            data->pressure_x10,
            data->altitude_cm);
    } else {
        env_monitor_view_set_data(
            app->readout_view, EnvMonitorLinkError, 0, 0, 0, 0);
    }
}

static int32_t env_monitor_poll_worker(void* context) {
    EnvMonitorApp* app = context;
    FURI_LOG_I(TAG, "poll worker start");

    while(app->poll_running) {
        EnvChainData data;
        EnvChainStatus status = env_chain_read(&data);
        app->device_connected = (status == EnvChainStatusOk);
        /* view model is Locking: safe to update straight from the worker. */
        env_monitor_apply_reading(app, status, &data);
        furi_delay_ms(POLL_PERIOD_MS);
    }
    FURI_LOG_I(TAG, "poll worker stop");
    return 0;
}

static void env_monitor_poll_start(EnvMonitorApp* app) {
    if(app->poll_thread) return;
    app->poll_running = true;
    app->poll_thread = furi_thread_alloc_ex(
        "EnvPoll", 4 * 1024, env_monitor_poll_worker, app);
    furi_thread_start(app->poll_thread);
}

static void env_monitor_poll_stop(EnvMonitorApp* app) {
    if(!app->poll_thread) return;
    app->poll_running = false;
    furi_thread_join(app->poll_thread);
    furi_thread_free(app->poll_thread);
    app->poll_thread = NULL;
}

/* ------------------------------------------------------------------ */
/* Calibration worker                                                   */
/* ------------------------------------------------------------------ */
static int32_t env_monitor_calib_worker(void* context) {
    EnvMonitorApp* app = context;
    EnvChainStatus status = EnvChainStatusParameterError;

    if(app->calib_mode == CalibModeByPressure) {
        double hpa = strtod(app->input_buf, NULL);
        uint16_t x10 = (uint16_t)(hpa * 10.0 + 0.5);
        if(hpa < 0) x10 = 0;
        status = env_chain_set_sealevel(x10, true);
    } else if(app->calib_mode == CalibModeByAltitude) {
        int32_t metres = (int32_t)strtol(app->input_buf, NULL, 10);
        status = env_chain_calibrate_altitude(metres * 100, true);
    }

    app->calib_ok = (status == EnvChainStatusOk);
    FURI_LOG_I(TAG, "calibration done: status=%d ok=%d", (int)status, app->calib_ok);
    view_dispatcher_send_custom_event(app->view_dispatcher, (uint32_t)EventCalibDone);
    return 0;
}

/* ------------------------------------------------------------------ */
/* GUI callbacks                                                        */
/* ------------------------------------------------------------------ */
static void env_monitor_readout_ok(void* context) {
    EnvMonitorApp* app = context;
    /* The view's model is locked-free for reads here; check connection state
     * through the shared link flag maintained by the poll worker. */
    if(app->device_connected) {
        scene_manager_next_scene(app->scene_manager, SceneCalibMenu);
    } else {
        env_monitor_poll_stop(app);
        env_monitor_poll_start(app); /* retry enumeration */
    }
}

static void env_monitor_calib_menu_callback(void* context, uint32_t index) {
    EnvMonitorApp* app = context;
    app->calib_mode =
        (index == SubmenuIndexByPressure) ? CalibModeByPressure : CalibModeByAltitude;
    scene_manager_next_scene(app->scene_manager, SceneInput);
}

static void env_monitor_text_input_callback(void* context) {
    EnvMonitorApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, (uint32_t)EventInputDone);
}

static void env_monitor_popup_callback(void* context) {
    EnvMonitorApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, (uint32_t)EventPopupClosed);
}

static bool env_monitor_validator(const char* text, FuriString* error, void* context) {
    EnvMonitorApp* app = context;
    if(!text || !strlen(text)) {
        furi_string_set_str(error, "请输入数值");
        return false;
    }
    if(strlen(text) >= INPUT_BUF_SIZE - 4) {
        furi_string_set_str(error, "数值过长");
        return false;
    }
    if(app->calib_mode == CalibModeByPressure) {
        const double hpa = strtod(text, NULL);
        if(hpa < 300.0 || hpa > 1100.0) {
            furi_string_set_str(error, "范围 300.0~1100.0 hPa");
            return false;
        }
    } else {
        const long metres = strtol(text, NULL, 10);
        if(metres < -500 || metres > 9000) {
            furi_string_set_str(error, "范围 -500~9000 m");
            return false;
        }
    }
    return true;
}

static bool env_monitor_custom_event_callback(void* context, uint32_t event) {
    EnvMonitorApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool env_monitor_back_event_callback(void* context) {
    EnvMonitorApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void env_monitor_on_tick(void* context) {
    EnvMonitorApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* ------------------------------------------------------------------ */
/* Scene: readout                                                       */
/* ------------------------------------------------------------------ */
static void scene_readout_on_enter(void* context) {
    EnvMonitorApp* app = context;
    env_monitor_view_set_data(app->readout_view, EnvMonitorLinkScanning, 0, 0, 0, 0);
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdReadout);
    env_monitor_poll_start(app);
}

static bool scene_readout_on_event(void* context, SceneManagerEvent event) {
    EnvMonitorApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == EventDataUpdated) {
        return true; /* view already updated by the worker */
    }
    if(event.type == SceneManagerEventTypeBack) {
        env_monitor_poll_stop(app);
        view_dispatcher_stop(app->view_dispatcher);
        return true;
    }
    return false;
}

static void scene_readout_on_exit(void* context) {
    EnvMonitorApp* app = context;
    env_monitor_poll_stop(app);
}

/* ------------------------------------------------------------------ */
/* Scene: calibration submenu                                           */
/* ------------------------------------------------------------------ */
static void scene_calib_menu_on_enter(void* context) {
    EnvMonitorApp* app = context;
    submenu_reset(app->calib_menu);
    submenu_set_header(app->calib_menu, "海拔校准");
    submenu_add_item(
        app->calib_menu,
        "按海平面气压校准",
        SubmenuIndexByPressure,
        env_monitor_calib_menu_callback,
        app);
    submenu_add_item(
        app->calib_menu,
        "按当前海拔校准",
        SubmenuIndexByAltitude,
        env_monitor_calib_menu_callback,
        app);
    submenu_set_selected_item(app->calib_menu, 0);
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdCalibMenu);
}

static bool scene_calib_menu_on_event(void* context, SceneManagerEvent event) {
    EnvMonitorApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SceneReadout);
        return true;
    }
    return false;
}

static void scene_calib_menu_on_exit(void* context) {
    UNUSED(context);
}

/* ------------------------------------------------------------------ */
/* Scene: numeric input                                                 */
/* ------------------------------------------------------------------ */
static void scene_input_on_enter(void* context) {
    EnvMonitorApp* app = context;
    app->input_buf[0] = '\0';

    if(app->calib_mode == CalibModeByPressure) {
        text_input_set_header_text(app->text_input, "海平面气压 300.0~1100.0 hPa");
    } else {
        text_input_set_header_text(app->text_input, "当前海拔 -500~9000 m");
    }
    text_input_set_result_callback(
        app->text_input,
        env_monitor_text_input_callback,
        app,
        app->input_buf,
        INPUT_BUF_SIZE,
        false);
    text_input_set_validator(app->text_input, env_monitor_validator, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdInput);
}

static bool scene_input_on_event(void* context, SceneManagerEvent event) {
    EnvMonitorApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == EventInputDone) {
        scene_manager_next_scene(app->scene_manager, SceneResult);
        return true;
    }
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SceneCalibMenu);
        return true;
    }
    return false;
}

static void scene_input_on_exit(void* context) {
    EnvMonitorApp* app = context;
    text_input_reset(app->text_input);
}

/* ------------------------------------------------------------------ */
/* Scene: calibration progress / result                                 */
/* ------------------------------------------------------------------ */
static void env_monitor_popup_show_result(EnvMonitorApp* app) {
    popup_reset(app->popup);
    if(app->calib_ok) {
        popup_set_header(app->popup, "校准成功", 64, 8, AlignCenter, AlignTop);
        popup_set_text(app->popup, "已写入并保存\n断电后仍有效", 64, 30, AlignCenter, AlignTop);
    } else {
        popup_set_header(app->popup, "校准失败", 64, 8, AlignCenter, AlignTop);
        popup_set_text(app->popup, "请检查连接\n或数值范围", 64, 30, AlignCenter, AlignTop);
    }
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, env_monitor_popup_callback);
    popup_set_timeout(app->popup, 2000);
    popup_enable_timeout(app->popup);
}

static void scene_result_on_enter(void* context) {
    EnvMonitorApp* app = context;

    /* Stop the poller so it cannot race the (up to 6.5 s) calibration. */
    env_monitor_poll_stop(app);

    popup_reset(app->popup);
    popup_set_header(app->popup, "正在校准", 64, 12, AlignCenter, AlignTop);
    popup_set_text(app->popup, "请稍候，约需几秒...", 64, 36, AlignCenter, AlignTop);
    popup_set_context(app->popup, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdPopup);

    app->calib_ok = false;
    app->calib_thread = furi_thread_alloc_ex(
        "EnvCalib", 4 * 1024, env_monitor_calib_worker, app);
    furi_thread_start(app->calib_thread);
}

static bool scene_result_on_event(void* context, SceneManagerEvent event) {
    EnvMonitorApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == EventCalibDone) {
        if(app->calib_thread) {
            furi_thread_join(app->calib_thread);
            furi_thread_free(app->calib_thread);
            app->calib_thread = NULL;
        }
        env_monitor_popup_show_result(app);
        return true;
    }
    if(event.type == SceneManagerEventTypeCustom && event.event == EventPopupClosed) {
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SceneReadout);
        return true;
    }
    if(event.type == SceneManagerEventTypeBack) {
        return true; /* ignore Back while calibrating */
    }
    return false;
}

static void scene_result_on_exit(void* context) {
    EnvMonitorApp* app = context;
    if(app->calib_thread) {
        furi_thread_join(app->calib_thread);
        furi_thread_free(app->calib_thread);
        app->calib_thread = NULL;
    }
    popup_reset(app->popup);
}

/* ------------------------------------------------------------------ */
/* Alloc / free / entry                                                 */
/* ------------------------------------------------------------------ */
static EnvMonitorApp* env_monitor_app_alloc(void) {
    EnvMonitorApp* app = malloc(sizeof(EnvMonitorApp));
    memset(app, 0, sizeof(*app));

    app->gui = furi_record_open(RECORD_GUI);
    app->scene_manager = scene_manager_alloc(&s_scene_handlers, app);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, env_monitor_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, env_monitor_back_event_callback);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, env_monitor_on_tick, 100);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->readout_view = env_monitor_view_alloc();
    env_monitor_view_set_ok_callback(app->readout_view, env_monitor_readout_ok, app);
    view_dispatcher_add_view(app->view_dispatcher, ViewIdReadout, app->readout_view);

    app->calib_menu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ViewIdCalibMenu, submenu_get_view(app->calib_menu));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ViewIdInput, text_input_get_view(app->text_input));

    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ViewIdPopup, popup_get_view(app->popup));

    app->calib_mode = CalibModeNone;
    return app;
}

static void env_monitor_app_free(EnvMonitorApp* app) {
    env_monitor_poll_stop(app);

    view_dispatcher_remove_view(app->view_dispatcher, ViewIdReadout);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdCalibMenu);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdInput);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdPopup);
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    env_monitor_view_free(app->readout_view);
    submenu_free(app->calib_menu);
    text_input_free(app->text_input);
    popup_free(app->popup);

    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t env_monitor_app(void* p) {
    UNUSED(p);

    if(!env_chain_init()) {
        FURI_LOG_W(TAG, "no Chain ENV found on Cap Grove port");
    }

    EnvMonitorApp* app = env_monitor_app_alloc();
    scene_manager_next_scene(app->scene_manager, SceneReadout);
    view_dispatcher_run(app->view_dispatcher);
    env_monitor_app_free(app);

    env_chain_deinit();
    return 0;
}
