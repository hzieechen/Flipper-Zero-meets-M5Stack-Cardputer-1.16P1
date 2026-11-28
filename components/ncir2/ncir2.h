/**
 * @file ncir2.h
 * @brief Driver for M5Stack Unit NCIR2 (MLX90614 + STM32, I2C @0x5A).
 *
 * Reads the object temperature from register 0x00. The value is a signed
 * int16 in centidegrees Celsius (e.g. 2530 -> 25.30 C), matching the
 * official M5Unit-NCIR2 Arduino library.
 *
 * The sensor sits on the Cap CC1101 Grove port (SDA=GPIO8, SCL=GPIO9), the
 * host's shared I2C_NUM_0 bus that the keyboard target already owns and
 * installs at boot. This driver reuses that bus; it never installs/deletes
 * its own I2C driver.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NCIR2_I2C_ADDR 0x5Au
#define NCIR2_REG_TEMP 0x00u
#define NCIR2_REG_SOC_TEMP 0x90u
#define NCIR2_REG_EMISSIVITY 0x10u
#define NCIR2_REG_VERSION 0xF0u
#define NCIR2_REG_RGB_LED 0x60u
#define NCIR2_REG_SAVE_CFG 0x80u

/** Mark the shared bus ready (the driver itself is owned by the keyboard target). Idempotent. */
bool ncir2_init(void);

/** Release our claim; does NOT delete the keyboard's I2C driver. Idempotent. */
void ncir2_deinit(void);

/** Return true when a device ACKs at 0x5A. */
bool ncir2_is_present(void);

/** Read the firmware version register (also used as a presence probe). */
bool ncir2_read_version(uint8_t* version);

/**
 * Read object temperature.
 * @param temp_x100 output, centidegrees C (e.g. 2530 = 25.30 C).
 * @return true on success.
 */
bool ncir2_read_temp(int16_t* temp_x100);

/**
 * Read the sensor's own (ambient) temperature, centidegrees C.
 */
bool ncir2_read_soc_temp(int16_t* temp_x100);

/**
 * Read emissivity as a raw uint16 (official value / 65535.0).
 */
bool ncir2_read_emissivity(uint16_t* raw);

/** Write bytes to a register. */
bool ncir2_write_reg(uint8_t reg_addr, const uint8_t* data, size_t len);

/** Set the RGB LED colour (each channel 0-255). */
bool ncir2_set_led(uint8_t r, uint8_t g, uint8_t b);

/**
 * Set the emissivity (0.1 .. 1.0). Writes reg 0x10 = 65535 * rate.
 * Note: the sensor needs a reconnect after each emissivity write.
 */
bool ncir2_set_emissivity(float rate);

/** Save current config to the sensor's flash (reg 0x80 = 1). */
bool ncir2_save_config(void);

#ifdef __cplusplus
}
#endif
