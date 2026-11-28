/**
 * @file furi_hal_mic.h
 * Microphone capture HAL. On the Cardputer-ADV the mic is the ES8311 codec ADC,
 * read over a dedicated 16 kHz I2S_NUM_0 RX channel (BCLK=41, WS=43, DIN=46),
 * matching M5Unified's official board setup (speaker = I2S1, mic = I2S0). The
 * speaker clocks are suspended while recording. No-op on boards without a mic.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start microphone capture.
 *
 * Acquires exclusive audio ownership (shared with the speaker), brings up the
 * codec ADC path, and enables the I2S RX channel.
 *
 * @return sample rate in Hz, or 0 on failure (audio busy / no mic).
 */
uint32_t furi_hal_mic_start(void);

/** Read mono 16-bit PCM samples.
 *
 * @param buf         destination for `samples` int16 mono samples
 * @param samples     number of samples requested
 * @param timeout_ms  per-DMA-read timeout
 * @return number of mono samples actually written to `buf`
 */
size_t furi_hal_mic_read(int16_t* buf, size_t samples, uint32_t timeout_ms);

/** Stop capture and release audio ownership. */
void furi_hal_mic_stop(void);

#ifdef __cplusplus
}
#endif
