/**
 * @file digital_sequence.c
 * @brief DigitalSequence back-end for ESP32-S3, built on the RMT TX peripheral.
 *
 * Upstream Flipper Zero drives the sequence from TIM2 (64 MHz, 15.625 ns tick)
 * with two DMA channels: one feeding TIM2->ARR with the next period length, the
 * other clocked by the timer update event and copying BSRR values into the GPIO
 * port so that the level flips exactly on every period boundary.
 *
 * ESP32-S3 has no such "timer update event -> DMA -> GPIO" path, but RMT TX is a
 * direct equivalent: each rmt_symbol_word_t carries two (duration, level) pairs
 * and the peripheral applies them back-to-back without CPU involvement. So the
 * whole waveform is flattened into a symbol array up-front and handed to RMT in
 * one blocking rmt_transmit() call.
 *
 * Timing budget check (why a full pre-build is acceptable here):
 *   - ISO14443-3A MIFARE frame: <= 18 bytes -> <= 162 bit slots -> <= ~1460
 *     periods -> ~730 symbols -> ~2.9 KiB. Building it costs tens of
 *     microseconds, well inside the reader's frame waiting time.
 *   - The peripheral streams the array through its 48-word memory with
 *     ping-pong refills, so a long array does not need a bigger RMT block.
 */

#include "digital_sequence.h"
#include "digital_signal_i.h"

#include <furi.h>
#include <furi_hal_gpio.h>

#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>
#include <stdlib.h>
#include <string.h>

/* Maximum amount of registered signals. */
#define DIGITAL_SEQUENCE_BANK_SIZE 32

/* RMT runs from APB (80 MHz) on the S3, i.e. one count == 12.5 ns. */
#define DIGITAL_SEQUENCE_RMT_RESOLUTION_HZ (80 * 1000 * 1000)
#define DIGITAL_SEQUENCE_RMT_TICK_PS       (12500) /* 12.5 ns, in the same 10 ps units as digital_signal_i.h */

/* One RMT duration field is 15 bits wide. */
#define DIGITAL_SEQUENCE_RMT_MAX_DURATION (0x7FFF)

/* RMT hardware memory for a single channel, in symbols. */
#define DIGITAL_SEQUENCE_RMT_MEM_SYMBOLS (48)

typedef const DigitalSignal* DigitalSequenceSignalBank[DIGITAL_SEQUENCE_BANK_SIZE];

struct DigitalSequence {
    const GpioPin* gpio;

    uint32_t size;
    uint32_t max_size;

    DigitalSequenceSignalBank signals;

    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;

    /* GPIO-matrix output signal index that rmt_new_tx_channel() routed to our
     * pad. Cached so that the pad can be handed back to RMT before every
     * transmission: the driver only performs that routing once, at channel
     * creation, and the NFC SPI bus takes the pad over after each frame. */
    uint32_t rmt_out_sig;

    /* Waveform scratch buffer, grown on demand and reused between frames. */
    rmt_symbol_word_t* symbols;
    size_t symbols_capacity; /* in symbols (words), not periods */

    uint8_t data[];
};

DigitalSequence* digital_sequence_alloc(uint32_t size, const GpioPin* gpio) {
    furi_assert(size);
    furi_assert(gpio);

    DigitalSequence* sequence = malloc(sizeof(DigitalSequence) + size);
    furi_check(sequence);

    sequence->gpio = gpio;
    sequence->max_size = size;
    sequence->size = 0;
    memset(sequence->signals, 0, sizeof(sequence->signals));
    sequence->symbols = NULL;
    sequence->symbols_capacity = 0;

    /* Creating the channel immediately points the pad at the RMT TX signal
     * through the GPIO matrix -- it is NOT deferred to rmt_enable(). Whoever
     * shares that pad (the NFC SPI bus, here) has to take it back explicitly
     * right after this call; digital_sequence_transmit() re-applies the routing
     * before each frame. */
    const rmt_tx_channel_config_t channel_config = {
        .gpio_num = (gpio_num_t)gpio->pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = DIGITAL_SEQUENCE_RMT_RESOLUTION_HZ,
        .mem_block_symbols = DIGITAL_SEQUENCE_RMT_MEM_SYMBOLS,
        .trans_queue_depth = 2,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    esp_err_t err = rmt_new_tx_channel(&channel_config, &sequence->channel);
    furi_check(err == ESP_OK);

    /* rmt_new_tx_channel() already routed the pad to the RMT TX signal through
     * the GPIO matrix (see its esp_rom_gpio_connect_out_signal() call); read
     * that index back so the routing can be re-applied before every frame. */
    sequence->rmt_out_sig = REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + 4 * gpio->pin) & 0x1FF;

    const rmt_copy_encoder_config_t encoder_config = {};
    err = rmt_new_copy_encoder(&encoder_config, &sequence->encoder);
    furi_check(err == ESP_OK);

    return sequence;
}

void digital_sequence_free(DigitalSequence* sequence) {
    furi_assert(sequence);

    if(sequence->encoder) rmt_del_encoder(sequence->encoder);
    if(sequence->channel) rmt_del_channel(sequence->channel);
    if(sequence->symbols) free(sequence->symbols);

    free(sequence);
}

void digital_sequence_register_signal(
    DigitalSequence* sequence,
    uint8_t signal_index,
    const DigitalSignal* signal) {
    furi_check(sequence);
    furi_check(signal);
    furi_check(signal_index < DIGITAL_SEQUENCE_BANK_SIZE);

    sequence->signals[signal_index] = signal;
}

void digital_sequence_add_signal(DigitalSequence* sequence, uint8_t signal_index) {
    furi_check(sequence);
    furi_check(signal_index < DIGITAL_SEQUENCE_BANK_SIZE);
    furi_check(sequence->size < sequence->max_size);

    sequence->data[sequence->size++] = signal_index;
}

void digital_sequence_clear(DigitalSequence* sequence) {
    furi_assert(sequence);

    sequence->size = 0;
}

/* Convert a digital_signal period (DIGITAL_SIGNAL_T_TIM units, 15.625 ns) into
 * RMT counts (12.5 ns), carrying the fractional part so that the accumulated
 * rounding error never exceeds one RMT tick over the whole frame. */
static inline uint32_t
    digital_sequence_ticks_to_counts(uint32_t ticks, int32_t* accumulator) {
    *accumulator += (int32_t)(ticks * DIGITAL_SIGNAL_T_TIM);

    const int32_t counts = *accumulator / DIGITAL_SEQUENCE_RMT_TICK_PS;
    *accumulator -= counts * DIGITAL_SEQUENCE_RMT_TICK_PS;

    return ((uint32_t)counts > 0) ? (uint32_t)counts : 1;
}

static inline void digital_sequence_ensure_capacity(DigitalSequence* sequence, size_t wanted) {
    if(sequence->symbols_capacity >= wanted) return;

    /* Grow with some headroom: frames repeat at the same size over and over. */
    size_t capacity = sequence->symbols_capacity ? sequence->symbols_capacity : 256;
    while(capacity < wanted) capacity *= 2;

    rmt_symbol_word_t* buffer = realloc(sequence->symbols, capacity * sizeof(rmt_symbol_word_t));
    furi_check(buffer);

    sequence->symbols = buffer;
    sequence->symbols_capacity = capacity;
}

/**
 * @brief Append one (duration, level) pair to the symbol stream.
 *
 * Two consecutive pairs share one rmt_symbol_word_t; the array is
 * zero-initialised, so an odd number of pairs naturally terminates the stream
 * with duration1 == 0, which is exactly the RMT end-of-transmission marker.
 */
static inline void digital_sequence_emit(
    DigitalSequence* sequence,
    size_t* position,
    uint32_t duration,
    bool level) {
    rmt_symbol_word_t* symbol = &sequence->symbols[*position / 2];

    if(*position & 1) {
        symbol->duration1 = duration;
        symbol->level1 = level;
    } else {
        symbol->duration0 = duration;
        symbol->level0 = level;
    }

    (*position)++;
}

/**
 * @brief Flush one run of identical-level periods.
 *
 * Consecutive periods of the same level (which happens at signal boundaries,
 * and is how digital_sequence merges adjacent signals) become a single longer
 * duration, exactly like the upstream timeline. Runs longer than the 15-bit
 * duration field are split into several symbols of the same level.
 */
static inline void digital_sequence_emit_run(
    DigitalSequence* sequence,
    size_t* position,
    uint32_t duration,
    bool level) {
    while(duration > DIGITAL_SEQUENCE_RMT_MAX_DURATION) {
        digital_sequence_emit(sequence, position, DIGITAL_SEQUENCE_RMT_MAX_DURATION, level);
        duration -= DIGITAL_SEQUENCE_RMT_MAX_DURATION;
    }

    if(duration) {
        digital_sequence_emit(sequence, position, duration, level);
    }
}

/**
 * @brief Flatten the signal-index stream into the RMT symbol buffer.
 *
 * @returns the number of symbols (words) that were filled in.
 */
static size_t digital_sequence_build(DigitalSequence* sequence) {
    /* Upper bound on the number of (duration, level) pairs: every registered
     * period becomes at most one pair (merging only ever reduces the count). */
    size_t max_pairs = 0;
    for(uint32_t i = 0; i < sequence->size; i++) {
        const DigitalSignal* signal = sequence->signals[sequence->data[i]];
        furi_check(signal);
        max_pairs += signal->size;
    }
    furi_check(max_pairs);

    const size_t max_symbols = (max_pairs + 2) / 2 + 1;
    digital_sequence_ensure_capacity(sequence, max_symbols);
    memset(sequence->symbols, 0, max_symbols * sizeof(rmt_symbol_word_t));

    size_t position = 0;

    uint32_t run_duration = 0;
    bool run_level = false;
    bool run_active = false;

    int32_t conversion_remainder = 0;
    int32_t signal_remainder = 0;
    uint32_t carry = 0;

    for(uint32_t i = 0; i < sequence->size; i++) {
        const DigitalSignal* signal = sequence->signals[sequence->data[i]];

        for(uint32_t p = 0; p < signal->size; p++) {
            const uint32_t ticks = signal->data[p] + 1 + carry;
            carry = 0;

            const bool level = signal->start_level ^ (p & 1);
            const uint32_t duration = digital_sequence_ticks_to_counts(ticks, &conversion_remainder);

            if(run_active && (level == run_level)) {
                run_duration += duration;
            } else {
                if(run_active) {
                    digital_sequence_emit_run(sequence, &position, run_duration, run_level);
                }
                run_duration = duration;
                run_level = level;
                run_active = true;
            }
        }

        /* Distribute the container's rounding error over the next signal, the
         * same way the upstream engine does, so that a long frame cannot drift
         * by more than half a tick per signal. */
        signal_remainder += signal->remainder;
        if(signal_remainder >= DIGITAL_SIGNAL_T_TIM_DIV2) {
            signal_remainder -= DIGITAL_SIGNAL_T_TIM;
            carry = 1;
        }
    }

    if(run_active) {
        digital_sequence_emit_run(sequence, &position, run_duration, run_level);
    }

    furi_check(position);

    return (position + 1) / 2;
}

void digital_sequence_transmit(DigitalSequence* sequence) {
    furi_check(sequence);
    furi_check(sequence->size);

    const size_t symbol_count = digital_sequence_build(sequence);

    /* Take the pad back from whoever owns it between frames (the NFC SPI bus
     * restores its own routing after every listener frame, so the GPIO matrix
     * no longer points at RMT). Guard the index so a failed read-back cannot
     * route the pad to a nonsense peripheral. */
    if(sequence->rmt_out_sig && sequence->rmt_out_sig != 0x100) {
        esp_rom_gpio_connect_out_signal(
            (uint32_t)sequence->gpio->pin, sequence->rmt_out_sig, false, false);
    }

    /* No critical section here on purpose: the frame is clocked out by the RMT
     * peripheral and we block until it is done, so masking interrupts would
     * prevent the RMT ISR from ever retiring the transaction. */
    esp_err_t err = rmt_enable(sequence->channel);
    furi_check(err == ESP_OK);

    const rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
        .flags.eot_level = false,
    };
    err = rmt_transmit(
        sequence->channel,
        sequence->encoder,
        sequence->symbols,
        symbol_count * sizeof(rmt_symbol_word_t),
        &transmit_config);
    furi_check(err == ESP_OK);

    err = rmt_tx_wait_all_done(sequence->channel, portMAX_DELAY);
    furi_check(err == ESP_OK);

    /* Stops the channel and releases the GPIO output so the caller can hand the
     * pin back to the peripheral that owns it (the NFC SPI bus, in our case). */
    rmt_disable(sequence->channel);
}
