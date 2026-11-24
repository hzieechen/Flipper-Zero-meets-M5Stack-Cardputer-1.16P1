#include "furi_hal_nfc_i.h"

#include <lib/drivers/st25r3916.h>
#include <furi_hal_resources.h>

static void furi_hal_nfc_int_callback(void* context) {
    UNUSED(context);
    furi_hal_nfc_event_set(FuriHalNfcEventInternalTypeIrq);
}

uint32_t furi_hal_nfc_get_irq(const FuriHalSpiBusHandle* handle) {
    uint32_t irq = 0;
    /* ST25R3916 IRQ is push-pull, active high. Drain the status registers until
     * the line drops, otherwise a second simultaneously-pending source (e.g.
     * RXS + RXE) would be lost with the first read. */
    while(furi_hal_gpio_read(&gpio_nfc_irq)) {
        irq |= st25r3916_get_irq(handle);
    }
    return irq;
}

void furi_hal_nfc_init_gpio_isr(void) {
    furi_hal_gpio_init(&gpio_nfc_irq, GpioModeInterruptRise, GpioPullDown, GpioSpeedVeryHigh);
    furi_hal_gpio_add_int_callback(&gpio_nfc_irq, furi_hal_nfc_int_callback, NULL);
    furi_hal_gpio_enable_int_callback(&gpio_nfc_irq);
}

void furi_hal_nfc_deinit_gpio_isr(void) {
    furi_hal_gpio_remove_int_callback(&gpio_nfc_irq);
    furi_hal_gpio_init(&gpio_nfc_irq, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}
