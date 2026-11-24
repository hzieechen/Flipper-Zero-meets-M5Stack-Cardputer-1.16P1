/**
 * @file furi_hal_speaker.h
 * Speaker HAL — I2S tone generation (T-Embed) / no-op stubs (other boards)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <common_defines.h>
#include "furi_hal_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Init speaker */
void furi_hal_speaker_init(void);

/** Deinit speaker */
void furi_hal_speaker_deinit(void);

/** Acquire speaker ownership
 *
 * @warning    You must acquire speaker ownership before use
 *
 * @param      timeout  Timeout during which speaker ownership must be acquired
 *
 * @return     bool  returns true on success
 */
FURI_WARN_UNUSED bool furi_hal_speaker_acquire(uint32_t timeout);

/** Release speaker ownership
 *
 * @warning    You must release speaker ownership after use
 */
void furi_hal_speaker_release(void);

/** Check current process speaker ownership
 *
 * @warning    always returns true if called from ISR
 *
 * @return     bool returns true if process owns speaker
 */
bool furi_hal_speaker_is_mine(void);

/** Play a note
 *
 * @warning    no ownership check if called from ISR
 *
 * @param      frequency  The frequency
 * @param      volume     The volume
 */
void furi_hal_speaker_start(float frequency, float volume);

/** Set volume
 *
 * @warning    no ownership check if called from ISR
 *
 * @param      volume  The volume
 */
void furi_hal_speaker_set_volume(float volume);

/** Stop playback
 *
 * @warning    no ownership check if called from ISR
 */
void furi_hal_speaker_stop(void);

/** Initialize the audio codec for apps that drive I2S directly.
 *
 * On Cardputer-ADV the speaker is an ES8311 codec that needs an I2C register
 * sequence before it will pass I2S data. The normal path configures it lazily
 * in furi_hal_speaker_acquire(); apps that deinit the speaker HAL and take over
 * I2S_NUM_0 themselves (e.g. the MP3 player) must call this after enabling the
 * I2S channel. No-op on boards with a plain I2S amplifier.
 */
void furi_hal_speaker_codec_init(void);

/** Mirror a GPIO pin (e.g. CC1101 GDO0) onto the speaker as 1-bit audio.
 *
 * Replaces the STM32 hardware TIM-bridge that wired the demodulator output
 * straight to the speaker PWM pin. On ESP32 we sample the pin in software at
 * a fixed rate and emit ±amplitude PCM via the existing I2S writer thread.
 *
 * Caller must hold speaker ownership (acquire) before calling.
 *
 * @param      gdo_pin  GPIO pin to mirror (must be a valid input pin)
 * @param      volume   Output volume 0.0–1.0 (cubic-curve scaled, like _start)
 */
void furi_hal_speaker_start_gdo_mirror(const GpioPin* gdo_pin, float volume);

#ifdef __cplusplus
}
#endif
