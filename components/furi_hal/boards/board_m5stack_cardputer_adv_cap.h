/**
 * @file board_m5stack_cardputer_adv_cap.h
 * Board definition: M5Stack Cardputer-ADV + M5Stack Cap CC1101 (SKU U219)
 *
 * MCU / display / keyboard / audio / SD are identical to the plain ADV board;
 * only the EXT 14P sub-GHz wiring differs, because the Cap module re-assigns
 * three of those pins relative to a bare CC1101 breakout:
 *
 *   signal          bare CC1101 (adv header)   Cap CC1101
 *   CC1101 CS       GPIO13 (BOARD_PIN_CC1101_CSN)   GPIO5
 *   CC1101 GDO0     GPIO5  (BOARD_PIN_CC1101_GDO0)  GPIO15
 *   RF_SW0 (band)   not wired                        GPIO13
 *   RF_SW1 (band)   not wired                        CC1101's own GDO2 pin (330R)
 *   NFC CS / IRQ    -                                GPIO6 / GPIO4 (ST25R3916)
 *   POWER_EN        -                                GPIO3 (100k pull-up to VIN, on by default)
 *
 * Consequences of this variant:
 *  - The band switch needs SW0 as a host GPIO and SW1 driven through the CC1101
 *    IOCFG2 register (BOARD_CC1101_SW1_VIA_GDO2), see furi_hal_subghz_set_path().
 *    Cap truth table: 315MHz = SW0 0/SW1 1, 433MHz = 1/0, 868-915MHz = 1/1.
 *  - GPIO4/GPIO6 belong to the Cap's ST25R3916, so NRF24 is unmapped here —
 *    an nRF24L01 module cannot be used while the Cap is plugged in.
 *  - This firmware has no ST25R3916 driver: GPIO6 is held HIGH at boot so the
 *    NFC chip stays off the shared SPI3 bus (BOARD_PIN_NFC_CS_GUARD).
 *  - Never drive GPIO3 low — that is the module's DC-DC enable.
 */

#pragma once

#include "board_m5stack_cardputer_adv.h"

#undef BOARD_NAME
#define BOARD_NAME "M5Stack Cardputer-ADV + Cap CC1101"

/* ---- SubGHz / CC1101 on the Cap 14P bus ---- */
#undef BOARD_PIN_CC1101_CSN
#define BOARD_PIN_CC1101_CSN    5  /* Cap CC1101_CS     */
#undef BOARD_PIN_CC1101_GDO0
#define BOARD_PIN_CC1101_GDO0   15 /* Cap CC1101_G0     */
#undef BOARD_PIN_CC1101_SW0
#define BOARD_PIN_CC1101_SW0    13 /* Cap CC1101_RF_SW0 */

/* RF_SW1 is not routed to the MCU: it hangs off the CC1101's GDO2 pin, so it is
 * driven by writing IOCFG2 = 0x2F (HW to 1) / 0x2E (HW to 0). BOARD_PIN_CC1101_SW1
 * therefore stays unmapped (UINT16_MAX) from the parent header. */
#define BOARD_CC1101_SW1_VIA_GDO2 1

/* ---- NRF24L01 — pins taken over by the Cap's NFC front-end ---- */
#undef BOARD_PIN_NRF24_CSN
#define BOARD_PIN_NRF24_CSN     UINT16_MAX
#undef BOARD_PIN_NRF24_CE
#define BOARD_PIN_NRF24_CE      UINT16_MAX
#undef BOARD_HAS_NRF24
#define BOARD_HAS_NRF24         0

/* ---- Cap NFC front-end (ST25R3916) ----
 * Shares SPI3 (SCLK=G40, MOSI=G14, MISO=G39) with the CC1101 and the SD card;
 * it is selected by its own CS on GPIO6 and signals events on GPIO4.
 * SPI mode 1 (CPOL=0, CPHA=1), register read opcode = 0x40 | addr. */
#undef BOARD_HAS_NFC
#define BOARD_HAS_NFC           1
#define BOARD_HAS_ST25R3916     1
#define BOARD_PIN_NFC_CS        6
#define BOARD_PIN_NFC_IRQ       4

/* Legacy guard: while the ST25R3916 driver is not active at boot, keep CS high
 * so the chip never drives MISO during CC1101/SD transactions. */
#define BOARD_PIN_NFC_CS_GUARD  6
