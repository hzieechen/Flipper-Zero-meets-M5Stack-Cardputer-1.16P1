#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <furi_hal_spi_types.h>

#ifdef __cplusplus
extern "C" {
#endif

void furi_hal_spi_bus_handle_init(const FuriHalSpiBusHandle* handle);
void furi_hal_spi_bus_handle_deinit(const FuriHalSpiBusHandle* handle);

/**
 * @brief Give the bus pins back to the SPI peripheral.
 *
 * Needed after another peripheral drove one of the bus lines through the GPIO
 * matrix (RMT generating the ST25R3916 transparent-mode TX stream on the MOSI
 * line, for instance). furi_hal_spi_bus_handle_init() only re-adds the device
 * and reconfigures CS; it does not re-run the bus-level pin routing, so the
 * MOSI output selector would otherwise stay pointed at the RMT signal.
 *
 * @param[in] handle pointer to the bus handle whose pins must be restored.
 */
void furi_hal_spi_bus_handle_restore_pins(const FuriHalSpiBusHandle* handle);

void furi_hal_spi_acquire(const FuriHalSpiBusHandle* handle);
void furi_hal_spi_release(const FuriHalSpiBusHandle* handle);

bool furi_hal_spi_bus_rx(
    const FuriHalSpiBusHandle* handle,
    uint8_t* buffer,
    size_t size,
    uint32_t timeout);

bool furi_hal_spi_bus_tx(
    const FuriHalSpiBusHandle* handle,
    const uint8_t* buffer,
    size_t size,
    uint32_t timeout);

bool furi_hal_spi_bus_trx(
    const FuriHalSpiBusHandle* handle,
    const uint8_t* tx_buffer,
    uint8_t* rx_buffer,
    size_t size,
    uint32_t timeout);

bool furi_hal_spi_bus_trx_dma(
    const FuriHalSpiBusHandle* handle,
    uint8_t* tx_buffer,
    uint8_t* rx_buffer,
    size_t size,
    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
