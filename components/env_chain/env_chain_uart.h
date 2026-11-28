/**
 * @file env_chain_uart.h
 * @brief UART transport for M5Stack Chain bus on Cap CC1101 Grove port.
 *
 * The Grove HY2.0-4P header on the Cap is a fixed I2C pass-through:
 *   yellow = SDA = GPIO2, white = SCL = GPIO1 (I2C_NUM_1).
 * Chain ENV speaks UART 115200 8N1 instead, so this layer temporarily
 * re-routes those two pads to UART1. GPIO1 is also the IR receiver input,
 * hence the enter/exit pinmux save/restore.
 *
 * Pins (wiring): host TX(GPIO2) -> ENV RX, host RX(GPIO1) <- ENV TX.
 * 5V/GND come from the same Grove header.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Enter: save GPIO1/2 mux, install UART1 (115200/8N1), route pads. */
bool env_uart_enter(void);

/** Leave: drain, delete UART1 driver, restore GPIO1/2 mux. */
void env_uart_exit(void);

/** Blocking write. Returns bytes actually queued. */
size_t env_uart_write(const uint8_t* data, size_t len);

/** Read up to len bytes, returns count (0 on timeout / error). */
size_t env_uart_read(uint8_t* data, size_t len, uint32_t timeout_ms);

/** Consume any bytes already sitting in the RX fifo. */
void env_uart_flush(void);

#ifdef __cplusplus
}
#endif
