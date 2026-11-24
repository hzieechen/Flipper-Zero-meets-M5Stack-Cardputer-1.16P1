#include "furi_hal_nfc_i.h"
#include "furi_hal_nfc_tech_i.h"

#include <driver/gptimer.h>
#include <esp_log.h>

#define TAG "FuriHalNfcTimer"

/* 1 MHz tick => 1 us resolution. NFC timing needs ~1 us granularity for FDT
 * (frame delay time) and a few ms for FWT (frame wait time). */
#define FURI_HAL_NFC_TIMER_RES_HZ 1000000

typedef enum {
    FuriHalNfcTimerFwt,
    FuriHalNfcTimerBlockTx,
    FuriHalNfcTimerCount,
} FuriHalNfcTimer;

typedef struct {
    gptimer_handle_t handle;
    FuriHalNfcEventInternalType event;
    /* started: the peripheral is in the "started" state. An ESP-IDF one-shot
     * timer stays started after its alarm fires, so start/stop must be tracked
     * separately from "counting", otherwise gptimer_stop() logs an error on
     * every re-arm (thousands per second during polling). */
    volatile bool started;
    volatile bool running;
} FuriHalNfcTimerConfig;

static FuriHalNfcTimerConfig furi_hal_nfc_timers[FuriHalNfcTimerCount] = {
    [FuriHalNfcTimerFwt] =
        {
            .handle = NULL,
            .event = FuriHalNfcEventInternalTypeTimerFwtExpired,
            .started = false,
            .running = false,
        },
    [FuriHalNfcTimerBlockTx] =
        {
            .handle = NULL,
            .event = FuriHalNfcEventInternalTypeTimerBlockTxExpired,
            .started = false,
            .running = false,
        },
};

static bool furi_hal_nfc_timer_on_alarm(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t* edata,
    void* context) {
    UNUSED(timer);
    UNUSED(edata);

    FuriHalNfcTimerConfig* config = context;
    config->running = false;
    furi_hal_nfc_event_set(config->event);

    return false;
}

static void furi_hal_nfc_timer_init(FuriHalNfcTimer timer) {
    FuriHalNfcTimerConfig* config = &furi_hal_nfc_timers[timer];

    const gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = FURI_HAL_NFC_TIMER_RES_HZ,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &config->handle));

    const gptimer_event_callbacks_t callbacks = {
        .on_alarm = furi_hal_nfc_timer_on_alarm,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(config->handle, &callbacks, config));
    ESP_ERROR_CHECK(gptimer_enable(config->handle));
}

static void furi_hal_nfc_timer_deinit(FuriHalNfcTimer timer) {
    FuriHalNfcTimerConfig* config = &furi_hal_nfc_timers[timer];

    if(!config->handle) return;

    if(config->started) {
        gptimer_stop(config->handle);
        config->started = false;
    }
    gptimer_disable(config->handle);
    gptimer_del_timer(config->handle);
    config->handle = NULL;
    config->running = false;
}

static int32_t furi_hal_nfc_timer_get_compensation(FuriHalNfcTimer timer) {
    const FuriHalNfcTechBase* current_tech = furi_hal_nfc_tech[furi_hal_nfc.tech];

    if(furi_hal_nfc.mode == FuriHalNfcModePoller) {
        const FuriHalNfcPollerCompensation* comp = &current_tech->poller.compensation;
        if(timer == FuriHalNfcTimerFwt)
            return comp->fwt;
        else if(timer == FuriHalNfcTimerBlockTx)
            return comp->fdt;

    } else if(furi_hal_nfc.mode == FuriHalNfcModeListener) {
        const FuriHalNfcListenerCompensation* comp = &current_tech->listener.compensation;
        if(timer == FuriHalNfcTimerBlockTx) return comp->fdt;
    }

    return 0;
}

static inline bool furi_hal_nfc_timer_is_running(FuriHalNfcTimer timer) {
    return furi_hal_nfc_timers[timer].running;
}

static void furi_hal_nfc_timer_start_us(FuriHalNfcTimer timer, uint32_t time_us) {
    FuriHalNfcTimerConfig* config = &furi_hal_nfc_timers[timer];
    furi_check(config->handle);

    if(time_us == 0) time_us = 1;

    /* Re-arm: stop only if the peripheral is actually started, then reload. */
    if(config->started) {
        esp_err_t stop_err = gptimer_stop(config->handle);
        if(stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "gptimer_stop failed: %s", esp_err_to_name(stop_err));
        }
        config->started = false;
    }

    gptimer_set_raw_count(config->handle, 0);

    const gptimer_alarm_config_t alarm_config = {
        .alarm_count = time_us,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false,
    };
    esp_err_t err = gptimer_set_alarm_action(config->handle, &alarm_config);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "gptimer_set_alarm_action failed: %s", esp_err_to_name(err));
    }

    config->running = true;
    err = gptimer_start(config->handle);
    if(err == ESP_OK) {
        config->started = true;
    } else {
        config->running = false;
        ESP_LOGW(TAG, "gptimer_start failed: %s", esp_err_to_name(err));
    }
}

static void furi_hal_nfc_timer_start_fc(FuriHalNfcTimer timer, uint32_t time_fc) {
    const int32_t comp_fc = furi_hal_nfc_timer_get_compensation(timer);
    /* Negative compensation would start the timer too early; the original
     * Flipper semantics only skip when the delay is already consumed. */
    if(comp_fc >= (int32_t)time_fc) return;

    const uint64_t time_us =
        ((uint64_t)((int32_t)time_fc - comp_fc) * FURI_HAL_NFC_TIMER_RES_HZ) /
        FURI_HAL_NFC_CARRIER_HZ;

    furi_hal_nfc_timer_start_us(timer, (uint32_t)time_us);
}

static void furi_hal_nfc_timer_stop(FuriHalNfcTimer timer) {
    FuriHalNfcTimerConfig* config = &furi_hal_nfc_timers[timer];

    if(!config->handle) return;

    if(config->started) {
        esp_err_t err = gptimer_stop(config->handle);
        if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "gptimer_stop failed: %s", esp_err_to_name(err));
        }
        config->started = false;
        gptimer_set_raw_count(config->handle, 0);
    }
    config->running = false;
}

void furi_hal_nfc_timers_init(void) {
    for(size_t i = 0; i < FuriHalNfcTimerCount; i++) {
        furi_hal_nfc_timer_init(i);
    }
}

void furi_hal_nfc_timers_deinit(void) {
    for(size_t i = 0; i < FuriHalNfcTimerCount; i++) {
        furi_hal_nfc_timer_deinit(i);
    }
}

void furi_hal_nfc_timer_fwt_start(uint32_t time_fc) {
    furi_hal_nfc_timer_start_fc(FuriHalNfcTimerFwt, time_fc);
}

void furi_hal_nfc_timer_fwt_stop(void) {
    furi_hal_nfc_timer_stop(FuriHalNfcTimerFwt);
}

void furi_hal_nfc_timer_block_tx_start(uint32_t time_fc) {
    furi_hal_nfc_timer_start_fc(FuriHalNfcTimerBlockTx, time_fc);
}

void furi_hal_nfc_timer_block_tx_start_us(uint32_t time_us) {
    furi_hal_nfc_timer_start_us(FuriHalNfcTimerBlockTx, time_us);
}

void furi_hal_nfc_timer_block_tx_stop(void) {
    furi_hal_nfc_timer_stop(FuriHalNfcTimerBlockTx);
}

bool furi_hal_nfc_timer_block_tx_is_running(void) {
    return furi_hal_nfc_timer_is_running(FuriHalNfcTimerBlockTx);
}
