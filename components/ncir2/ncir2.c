/**
 * @file ncir2.c
 * @brief I2C driver for M5Stack Unit NCIR2, on the Cap Grove port.
 *
 * The NCIR2 sits on the Cap CC1101 (U219) Grove port (J1), whose SDA/SCL are
 * GPIO8/GPIO9 — the SAME pads as the Cardputer-ADV keyboard (TCA8418), IMU
 * (BMI270) and audio codec (ES8311). That shared bus is the host's I2C_NUM_0,
 * and its legacy driver is installed once at boot by the keyboard target
 * (targets/m5stack_cardputer_adv/target_input.c kb_i2c_install()) and stays
 * resident for the whole session.
 *
 * So this driver does NOT install or delete its own I2C driver: it reuses the
 * keyboard's already-installed I2C_NUM_0 and just talks to 0x5A on it. The
 * addresses don't collide (keyboard 0x34, ES8311 0x18, BMI270 0x69, NCIR2 0x5A).
 *
 * The board's own Grove port (Cardputer-ADV motherboard) is a DIFFERENT pair
 * of pads (GPIO2/GPIO1); that is where the UART-based Chain ENV lives, not the
 * I2C NCIR2.
 */
#include "ncir2.h"

#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define NCIR2_I2C_PORT I2C_NUM_0
#define NCIR2_I2C_TIMEOUT_MS 100u

static bool s_i2c_ready = false;

bool ncir2_init(void) {
    if(s_i2c_ready) return true;
    /* The bus driver is owned by the keyboard target and is always installed;
     * nothing to configure here. Just mark ready and let callers probe. */
    s_i2c_ready = true;
    return true;
}

void ncir2_deinit(void) {
    /* We never installed the driver; do not delete the keyboard's bus. */
    s_i2c_ready = false;
}

static bool ncir2_probe_addr(i2c_port_t port, uint8_t addr) {
    /* Write-probe (START + addr<<1|WRITE + STOP): the standard way to detect an
     * I2C device, and the same method the keyboard's kb_i2c_scan uses. A
     * read-probe (i2c_master_read_from_device) can be NACK'd by slaves that
     * require a register pointer before any read. */
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}

bool ncir2_is_present(void) {
    if(!s_i2c_ready) return false;
    return ncir2_probe_addr(NCIR2_I2C_PORT, NCIR2_I2C_ADDR);
}

static bool ncir2_read_reg(uint8_t reg_addr, uint8_t* data, size_t len) {
    if(!s_i2c_ready || !data || !len) return false;

    /* NCIR2's STM32 I2C slave does NOT support a repeated start. The official
     * Arduino library reads with TWO separate transactions: write the register
     * address (STOP), then a fresh START to read. Mirror that here. */
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (NCIR2_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(NCIR2_I2C_PORT, cmd, pdMS_TO_TICKS(NCIR2_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    if(ret != ESP_OK) return false;

    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (NCIR2_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    if(len > 1) i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(NCIR2_I2C_PORT, cmd, pdMS_TO_TICKS(NCIR2_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}

bool ncir2_read_version(uint8_t* version) {
    return ncir2_read_reg(NCIR2_REG_VERSION, version, 1);
}

static bool ncir2_read_u16(uint8_t reg_addr, uint16_t* out) {
    uint8_t data[2] = {0, 0};
    if(!ncir2_read_reg(reg_addr, data, sizeof(data))) return false;
    *out = (uint16_t)data[0] | ((uint16_t)data[1] << 8); /* little endian */
    return true;
}

bool ncir2_read_temp(int16_t* temp_x100) {
    uint16_t raw = 0;
    if(!ncir2_read_u16(NCIR2_REG_TEMP, &raw)) return false;
    if(temp_x100) *temp_x100 = (int16_t)raw;
    return true;
}

bool ncir2_read_soc_temp(int16_t* temp_x100) {
    uint16_t raw = 0;
    if(!ncir2_read_u16(NCIR2_REG_SOC_TEMP, &raw)) return false;
    if(temp_x100) *temp_x100 = (int16_t)raw;
    return true;
}

bool ncir2_read_emissivity(uint16_t* raw) {
    return ncir2_read_u16(NCIR2_REG_EMISSIVITY, raw);
}

bool ncir2_write_reg(uint8_t reg_addr, const uint8_t* data, size_t len) {
    if(!s_i2c_ready || !data || !len) return false;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (NCIR2_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(NCIR2_I2C_PORT, cmd, pdMS_TO_TICKS(NCIR2_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}

bool ncir2_set_led(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t data[3] = {r, g, b};
    return ncir2_write_reg(NCIR2_REG_RGB_LED, data, sizeof(data));
}

bool ncir2_set_emissivity(float rate) {
    if(rate < 0.1f || rate > 1.0f) return false;
    uint16_t raw = (uint16_t)(65535.0f * rate);
    uint8_t data[2] = {(uint8_t)(raw & 0xFFu), (uint8_t)(raw >> 8)}; /* little endian */
    return ncir2_write_reg(NCIR2_REG_EMISSIVITY, data, sizeof(data));
}

bool ncir2_save_config(void) {
    uint8_t data[1] = {1};
    return ncir2_write_reg(NCIR2_REG_SAVE_CFG, data, sizeof(data));
}
