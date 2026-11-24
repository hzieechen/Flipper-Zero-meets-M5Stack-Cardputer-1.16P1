#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <furi_hal_gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FuriHalSpiBus FuriHalSpiBus;
typedef struct FuriHalSpiBusHandle FuriHalSpiBusHandle;

struct FuriHalSpiBus {
    int host_id;
    bool bitbang;
    bool initialized;
    void* mutex;
    const FuriHalSpiBusHandle* current_handle;
    uint16_t mosi_pin;
    uint16_t miso_pin;
    uint16_t sck_pin;
};

struct FuriHalSpiBusHandle {
    FuriHalSpiBus* bus;
    const GpioPin* miso;
    const GpioPin* mosi;
    const GpioPin* sck;
    const GpioPin* cs;
    void* device;
    bool initialized;
    /* Driver drives CS itself between transactions (ST25R3916 register access
     * needs CS held low across several FIFO/register transfers). When true the
     * ESP-IDF device is created with no hardware CS and the pin is left as a
     * plain GPIO output parked high. */
    bool manual_cs;
    uint32_t frequency_hz;
    uint8_t mode;
};

extern FuriHalSpiBus furi_hal_spi_bus_external;
extern FuriHalSpiBus furi_hal_spi_bus_subghz;

extern FuriHalSpiBusHandle furi_hal_spi_bus_handle_external;
extern FuriHalSpiBusHandle furi_hal_spi_bus_handle_subghz;
extern FuriHalSpiBusHandle furi_hal_spi_bus_handle_nrf24;
/* NFC front-end (Cap CC1101's ST25R3916): same physical bus as the sub-GHz
 * radio, selected by its own CS. Only registered when the board maps it. */
extern FuriHalSpiBusHandle furi_hal_spi_bus_handle_nfc;

#ifdef __cplusplus
}
#endif
