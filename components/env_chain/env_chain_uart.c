/**
 * @file env_chain_uart.c
 * @brief UART1 transport over the Cap Grove pads (GPIO2 TX / GPIO1 RX).
 *
 * See env_chain_uart.h for the pin-sharing rationale. Everything here follows
 * the two in-tree precedents:
 *   - uart_param_config / set_pin / driver_install / delete: furi_hal_rfid.c
 *   - GPIO matrix save via REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + 4*pin):
 *     lib/digital_signal/digital_sequence.c
 */
#include "env_chain_uart.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_rom_gpio.h>
#include <freertos/FreeRTOS.h>
#include <soc/gpio_reg.h>

#define ENV_UART_NUM UART_NUM_1
#define ENV_UART_TX_IO 2 /* Grove yellow = SDA */
#define ENV_UART_RX_IO 1 /* Grove white  = SCL, also IR RX */
#define ENV_UART_BAUD 115200
#define ENV_UART_RX_BUF 256

/* 0x100 = "route to GPIO itself" sentinel used by the ROM; never restore it
 * as a peripheral signal (same guard as digital_sequence.c). */
#define GPIO_OUT_SEL_GPIO_BIT 0x100u
#define GPIO_OUT_SEL_MASK 0x1FFu

typedef struct {
    uint32_t out_sel; /* GPIO_FUN Cn_OUT_SEL_CFG_REG raw value */
    bool saved;
} EnvUartPinState;

static EnvUartPinState s_tx_pin;
static EnvUartPinState s_rx_pin;
static bool s_uart_ready = false;

static void env_uart_save_pin(gpio_num_t pin, EnvUartPinState* state) {
    state->out_sel = REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + 4u * (uint32_t)pin);
    state->saved = true;
}

static void env_uart_restore_pin(gpio_num_t pin, const EnvUartPinState* state) {
    if(!state->saved) return;
    uint32_t sig = state->out_sel & GPIO_OUT_SEL_MASK;
    if(sig && sig != GPIO_OUT_SEL_GPIO_BIT) {
        esp_rom_gpio_connect_out_signal((uint32_t)pin, sig, false, false);
    }
    /* The pad direction/mode is put back by whoever owns it (I2C or IR RX)
     * when it re-initialises; we only hand the matrix routing back here. */
}

bool env_uart_enter(void) {
    if(s_uart_ready) return true;

    env_uart_save_pin((gpio_num_t)ENV_UART_TX_IO, &s_tx_pin);
    env_uart_save_pin((gpio_num_t)ENV_UART_RX_IO, &s_rx_pin);

    uart_config_t cfg = {
        .baud_rate = ENV_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if(uart_param_config(ENV_UART_NUM, &cfg) != ESP_OK) return false;
    if(uart_set_pin(ENV_UART_NUM, ENV_UART_TX_IO, ENV_UART_RX_IO, -1, -1) != ESP_OK) return false;
    if(uart_driver_install(ENV_UART_NUM, ENV_UART_RX_BUF, 0, 0, NULL, 0) != ESP_OK) return false;

    s_uart_ready = true;
    env_uart_flush();
    return true;
}

void env_uart_exit(void) {
    if(!s_uart_ready) return;

    uart_driver_delete(ENV_UART_NUM);
    env_uart_restore_pin((gpio_num_t)ENV_UART_TX_IO, &s_tx_pin);
    env_uart_restore_pin((gpio_num_t)ENV_UART_RX_IO, &s_rx_pin);

    s_tx_pin.saved = false;
    s_rx_pin.saved = false;
    s_uart_ready = false;
}

size_t env_uart_write(const uint8_t* data, size_t len) {
    if(!s_uart_ready || !data || !len) return 0;
    int written = uart_write_bytes(ENV_UART_NUM, (const char*)data, len);
    if(written < 0) return 0;
    /* Wait until the FIFO has drained so a following read is not racing the TX. */
    uart_wait_tx_done(ENV_UART_NUM, pdMS_TO_TICKS(100));
    return (size_t)written;
}

size_t env_uart_read(uint8_t* data, size_t len, uint32_t timeout_ms) {
    if(!s_uart_ready || !data || !len) return 0;
    int got = uart_read_bytes(ENV_UART_NUM, data, len, pdMS_TO_TICKS(timeout_ms));
    return got > 0 ? (size_t)got : 0;
}

void env_uart_flush(void) {
    if(!s_uart_ready) return;
    uart_flush_input(ENV_UART_NUM);
}
