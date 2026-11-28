/**
 * @file furi_hal_mic.c
 * Microphone capture HAL.
 *
 * On the Cardputer-ADV the microphone is the ES8311 codec's ADC. Capture uses
 * a dedicated I2S_NUM_0 RX channel at 16 kHz (BCLK=41/WS=43/DIN=GPIO46),
 * mirroring M5Unified's known-good board setup: official Cardputer-Adv maps
 * Mic to I2S0 and Speaker to I2S1. The speaker clock is explicitly suspended
 * while capturing so the two masters never drive the shared BCLK/WS pins at
 * once. The ES8311 register sequence and software gain follow M5Unified's
 * Mic_Class defaults.
 *
 * Boards without a mic get no-op stubs so callers link everywhere.
 */

#include "furi_hal_mic.h"
#include "boards/board.h"
#include <furi.h>

#define TAG "FuriHalMic"

#if defined(BOARD_HAS_MIC) && BOARD_HAS_MIC && defined(BOARD_HAS_ES8311) && BOARD_HAS_ES8311

#include "furi_hal_speaker.h"
#include <driver/i2s_std.h>

/* M5Unified leaves Mic.config().sample_rate at its 16000 Hz default for
 * Cardputer-Adv and sets only over_sampling=1. Running the ES8311 mic clock
 * registers (0x01=0xBA, 0x02=0x18) at 44.1 kHz made the WAV play back sped up;
 * 16 kHz is the rate those registers were tuned for. */
#define MIC_SAMPLE_RATE 16000
#define MIC_READ_FRAMES 256 /* mono samples per i2s read chunk */

/* M5Unified's Mic_Class defaults to magnification=16 with over_sampling=1,
 * i.e. f_gain = 16 / 2 = 8. The user still found the 8x result a little quiet,
 * so this port uses 12x. Raise to 16 if needed, lower to 4 if loud sounds clip. */
#define MIC_SOFT_GAIN 12

static i2s_chan_handle_t mic_rx_handle = NULL;
static bool mic_rx_enabled = false;

uint32_t furi_hal_mic_start(void) {
    if(!mic_rx_handle) {
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        chan_cfg.dma_desc_num = 4;
        chan_cfg.dma_frame_num = MIC_READ_FRAMES;
        chan_cfg.auto_clear = false;

        esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &mic_rx_handle);
        if(err != ESP_OK || !mic_rx_handle) {
            FURI_LOG_E(TAG, "i2s new rx channel failed: %d", err);
            mic_rx_handle = NULL;
            return 0;
        }

        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
            .slot_cfg =
                I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = (gpio_num_t)BOARD_PIN_SPEAKER_BCLK,
                .ws = (gpio_num_t)BOARD_PIN_SPEAKER_WCLK,
                .dout = I2S_GPIO_UNUSED,
                .din = (gpio_num_t)BOARD_PIN_MIC_DATA,
            },
        };
        /* M5Unified's default input_channel is input_only_right. */
        std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;

        err = i2s_channel_init_std_mode(mic_rx_handle, &std_cfg);
        if(err != ESP_OK) {
            FURI_LOG_E(TAG, "i2s rx init failed: %d", err);
            i2s_del_channel(mic_rx_handle);
            mic_rx_handle = NULL;
            return 0;
        }
    }

    if(!mic_rx_enabled) {
        /* Speaker I2S1 shares BCLK/WS GPIOs with this I2S0 RX master. Park its
         * clocks first, otherwise two masters fight over the same pins and the
         * ES8311 sees a corrupted LRCK (symptom: all-zero samples). */
        furi_hal_speaker_suspend_clocks();
        esp_err_t err = i2s_channel_enable(mic_rx_handle);
        if(err != ESP_OK) {
            FURI_LOG_E(TAG, "i2s rx enable failed: %d", err);
            furi_hal_speaker_resume_clocks();
            return 0;
        }
        mic_rx_enabled = true;
    }

    /* ES8311 arms its capture path only when a 0x0D write happens while the
     * I2S clock is running. The RX channel is enabled above, so this is safe. */
    furi_hal_speaker_es8311_adc_init();

    FURI_LOG_I(TAG, "mic started @ %u Hz (gain x%d)", (unsigned)MIC_SAMPLE_RATE, MIC_SOFT_GAIN);
    return MIC_SAMPLE_RATE;
}

size_t furi_hal_mic_read(int16_t* buf, size_t samples, uint32_t timeout_ms) {
    if(!mic_rx_handle || !mic_rx_enabled || buf == NULL || samples == 0) return 0;

    static int16_t mono[MIC_READ_FRAMES];
    size_t got = 0;
    static uint32_t dbg_last_tick = 0;
    static int16_t dbg_raw_peak = 0;
    static int16_t dbg_out_peak = 0;

    while(got < samples) {
        size_t want = samples - got;
        if(want > MIC_READ_FRAMES) want = MIC_READ_FRAMES;

        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(
            mic_rx_handle, mono, want * sizeof(int16_t), &bytes_read, timeout_ms);
        if(err != ESP_OK || bytes_read == 0) break;

        size_t count = bytes_read / sizeof(int16_t);
        for(size_t i = 0; i < count; i++) {
            int16_t raw = mono[i];
            int16_t raw_abs = raw < 0 ? (int16_t)-raw : raw;
            if(raw_abs > dbg_raw_peak) dbg_raw_peak = raw_abs;

            int32_t v = (int32_t)raw * MIC_SOFT_GAIN;
            if(v > INT16_MAX) v = INT16_MAX;
            if(v < INT16_MIN) v = INT16_MIN;
            int16_t out = (int16_t)v;
            int16_t out_abs = out < 0 ? (int16_t)-out : out;
            if(out_abs > dbg_out_peak) dbg_out_peak = out_abs;
            buf[got + i] = out;
        }
        got += count;

        if(count < want) break; /* short read: no more buffered data */
    }

    uint32_t now = furi_get_tick();
    if(now - dbg_last_tick > 1000) {
        FURI_LOG_I(
            TAG,
            "I2S peak raw=%d out=%d (x%d)",
            (int)dbg_raw_peak,
            (int)dbg_out_peak,
            MIC_SOFT_GAIN);
        dbg_last_tick = now;
        dbg_raw_peak = 0;
        dbg_out_peak = 0;
    }

    return got;
}

void furi_hal_mic_stop(void) {
    if(mic_rx_handle && mic_rx_enabled) {
        i2s_channel_disable(mic_rx_handle);
        mic_rx_enabled = false;
    }
    /* The mic path changed ES8311 0x01 to 0xBA; put the DAC side back so MP3
     * playback keeps working after recording. */
    furi_hal_speaker_es8311_restore_playback();
    furi_hal_speaker_resume_clocks();
    FURI_LOG_I(TAG, "mic stopped");
}

#else /* board without ES8311 mic */

uint32_t furi_hal_mic_start(void) {
    return 0;
}

size_t furi_hal_mic_read(int16_t* buf, size_t samples, uint32_t timeout_ms) {
    UNUSED(buf);
    UNUSED(samples);
    UNUSED(timeout_ms);
    return 0;
}

void furi_hal_mic_stop(void) {
}

#endif
