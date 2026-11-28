/**
 * @file env_chain.c
 * @brief Minimal M5Stack Chain client (ENV node only).
 *
 * Frame layout (ChainCommon.cpp):
 *   AA 55 lenLo lenHi id cmd data... crc8 55 AA
 *   lenLo/lenHi = 3 + data_len (id + cmd + data + crc8)
 *   total size  = data_len + 9
 *   crc8        = sum(frame[4 .. size-4])   (uint8 wraparound)
 *
 * The device replies with the same id + cmd; its response payload starts at
 * byte 6 and has (size - 9) bytes.
 */
#include "env_chain.h"
#include "env_chain_uart.h"

#include <furi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#define PACK_HEAD_HIGH 0xAAu
#define PACK_HEAD_LOW 0x55u
#define PACK_END_HIGH 0x55u
#define PACK_END_LOW 0xAAu
#define PACK_SIZE_MIN 9u
#define RX_BUFFER_SIZE 256u
#define MAX_FRAME_DATA 32u
#define ENUM_SCAN_MAX 16u
#define READ_TIMEOUT_MS 600u
#define ENUM_TIMEOUT_MS 1200u
#define TYPE_TIMEOUT_MS 600u
#define SET_TIMEOUT_MS 1500u
#define CALIB_TIMEOUT_MS 6500u /* official minimum is 5000 ms */

static uint8_t s_env_id = 0;
static FuriMutex* s_bus_mutex = NULL;

uint8_t env_chain_crc8(const uint8_t* frame, uint16_t size) {
    uint8_t crc = 0;
    if(!frame || size < 4) return 0;
    for(uint16_t i = 4; i < (uint16_t)(size - 3); i++) {
        crc = (uint8_t)(crc + frame[i]);
    }
    return crc;
}

uint16_t env_chain_build_frame(
    uint16_t id,
    uint8_t cmd,
    const uint8_t* data,
    uint16_t data_len,
    uint8_t* out_frame) {
    if(!out_frame || data_len > MAX_FRAME_DATA) return 0;

    const uint16_t total = (uint16_t)(data_len + 9u);
    const uint16_t cmd_size = (uint16_t)(3u + data_len);

    out_frame[0] = PACK_HEAD_HIGH;
    out_frame[1] = PACK_HEAD_LOW;
    out_frame[2] = (uint8_t)(cmd_size & 0xFFu);
    out_frame[3] = (uint8_t)((cmd_size >> 8) & 0xFFu);
    out_frame[4] = (uint8_t)(id & 0xFFu);
    out_frame[5] = cmd;
    if(data_len && data) memcpy(out_frame + 6, data, data_len);
    out_frame[total - 3] = env_chain_crc8(out_frame, total);
    out_frame[total - 2] = PACK_END_HIGH;
    out_frame[total - 1] = PACK_END_LOW;
    return total;
}

/**
 * Receive one frame with the expected id+cmd, discarding unrelated traffic
 * (heartbeat broadcasts, 0xFC enumeration pings, 0xE0 active records).
 */
static bool env_chain_recv(
    uint16_t expect_id,
    uint8_t expect_cmd,
    uint8_t* payload,
    size_t* payload_len,
    uint32_t timeout_ms) {
    static uint8_t rx[RX_BUFFER_SIZE];
    static uint8_t chunk[64];
    size_t rx_len = 0;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while(xTaskGetTickCount() < deadline) {
        const size_t got = env_uart_read(chunk, sizeof(chunk), 20);
        if(got) {
            if(rx_len + got > sizeof(rx)) {
                const size_t drop = rx_len + got - sizeof(rx);
                memmove(rx, rx + drop, rx_len - drop);
                rx_len -= drop;
            }
            memcpy(rx + rx_len, chunk, got);
            rx_len += got;
        }

        size_t pos = 0;
        while((rx_len - pos) >= PACK_SIZE_MIN) {
            if(rx[pos] != PACK_HEAD_HIGH || rx[pos + 1] != PACK_HEAD_LOW) {
                pos++;
                continue;
            }
            uint16_t frame_size = (uint16_t)rx[pos + 2] | ((uint16_t)rx[pos + 3] << 8);
            frame_size = (uint16_t)(frame_size + 6u);
            if(frame_size < PACK_SIZE_MIN || frame_size > sizeof(rx)) {
                pos++;
                continue;
            }
            if((rx_len - pos) < frame_size) break; /* wait for the rest */

            uint8_t* frame = rx + pos;
            if(frame[frame_size - 2] != PACK_END_HIGH || frame[frame_size - 1] != PACK_END_LOW) {
                pos++;
                continue;
            }
            if(env_chain_crc8(frame, frame_size) != frame[frame_size - 3]) {
                pos++;
                continue;
            }

            const uint16_t frame_id = frame[4];
            const uint8_t frame_cmd = frame[5];
            if(frame_id == expect_id && frame_cmd == expect_cmd) {
                const size_t len = frame_size - 9u;
                if(payload && len) memcpy(payload, frame + 6, len);
                if(payload_len) *payload_len = len;
                if(pos) {
                    memmove(rx, rx + pos, rx_len - pos);
                    rx_len -= pos;
                }
                return true;
            }
            pos += frame_size; /* unsolicited frame: drop it whole */
        }

        if(pos) {
            memmove(rx, rx + pos, rx_len - pos);
            rx_len -= pos;
        }
    }
    return false;
}

static EnvChainStatus env_chain_transaction(
    uint16_t id,
    uint8_t cmd,
    const uint8_t* data,
    uint16_t data_len,
    uint8_t* payload,
    size_t* payload_len,
    uint32_t timeout_ms) {
    uint8_t frame[MAX_FRAME_DATA + 9u];
    const uint16_t frame_len = env_chain_build_frame(id, cmd, data, data_len, frame);
    if(!frame_len) return EnvChainStatusParameterError;
    if(!s_bus_mutex) return EnvChainStatusBusy;

    /* Serialise the whole request/response so the 2s poll thread cannot
     * interleave bytes with a calibration (GUI thread) on the same UART. */
    furi_mutex_acquire(s_bus_mutex, FuriWaitForever);
    env_uart_flush();
    EnvChainStatus status = EnvChainStatusTimeout;
    if(env_uart_write(frame, frame_len) == frame_len) {
        status = env_chain_recv(id, cmd, payload, payload_len, timeout_ms) ? EnvChainStatusOk :
                                                                             EnvChainStatusTimeout;
    }
    furi_mutex_release(s_bus_mutex);
    return status;
}

bool env_chain_init(void) {
    s_env_id = 0;
    if(s_bus_mutex) furi_mutex_free(s_bus_mutex);
    s_bus_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!env_uart_enter()) {
        env_chain_deinit();
        return false;
    }

    uint8_t payload[16];
    size_t len = 0;

    /* Heartbeat is best-effort: some direct-wired setups skip it. */
    for(uint8_t retry = 0; retry < 2; retry++) {
        if(env_chain_transaction(
               CHAIN_BROADCAST_ID, CHAIN_HEARTBEAT, NULL, 0, payload, &len, 300) ==
           EnvChainStatusOk) {
            break;
        }
    }

    /* Ask the bus how many devices are attached. */
    const uint8_t enum_data = 0x00;
    EnvChainStatus status = env_chain_transaction(
        CHAIN_BROADCAST_ID, CHAIN_ENUM, &enum_data, 1, payload, &len, ENUM_TIMEOUT_MS);
    if(status != EnvChainStatusOk || len < 1 || payload[0] == 0) {
        env_chain_deinit();
        return false;
    }

    uint8_t count = payload[0];
    if(count > ENUM_SCAN_MAX) count = ENUM_SCAN_MAX;
    for(uint8_t id = 1; id <= count; id++) {
        status = env_chain_transaction(id, CHAIN_GET_DEVICE_TYPE, NULL, 0, payload, &len, TYPE_TIMEOUT_MS);
        if(status != EnvChainStatusOk || len < 2) continue;
        const uint16_t type = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
        if(type == CHAIN_ENV_TYPE_CODE) {
            s_env_id = id;
            return true;
        }
    }

    env_chain_deinit();
    return false;
}

void env_chain_deinit(void) {
    s_env_id = 0;
    env_uart_exit();
    if(s_bus_mutex) {
        furi_mutex_free(s_bus_mutex);
        s_bus_mutex = NULL;
    }
}

uint8_t env_chain_get_id(void) {
    return s_env_id;
}

EnvChainStatus env_chain_read(EnvChainData* out) {
    if(!out || !s_env_id) return EnvChainStatusNotFound;
    memset(out, 0, sizeof(*out));

    uint8_t p[16];
    size_t len = 0;

    EnvChainStatus status = env_chain_transaction(
        s_env_id, CHAIN_ENV_GET_AHT20_DATA, NULL, 0, p, &len, READ_TIMEOUT_MS);
    if(status != EnvChainStatusOk) return status;
    if(len < 3) return EnvChainStatusReturnPacketError;
    out->temp_aht_x100 = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    out->humidity = p[2];

    status = env_chain_transaction(
        s_env_id, CHAIN_ENV_GET_SPA06_DATA, NULL, 0, p, &len, READ_TIMEOUT_MS);
    if(status != EnvChainStatusOk) return status;
    if(len < 8) return EnvChainStatusReturnPacketError;
    out->pressure_x10 = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
    out->altitude_cm = (int32_t)((uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) |
                                 ((uint32_t)p[7] << 24));
    return EnvChainStatusOk;
}

EnvChainStatus env_chain_set_sealevel(uint16_t pressure_x10, bool save_to_flash) {
    if(!s_env_id) return EnvChainStatusNotFound;
    if(pressure_x10 < CHAIN_ENV_SEA_LEVEL_PRESSURE_MIN_HPA_X10 ||
       pressure_x10 > CHAIN_ENV_SEA_LEVEL_PRESSURE_MAX_HPA_X10) {
        return EnvChainStatusParameterError;
    }

    uint8_t data[3] = {
        (uint8_t)(pressure_x10 & 0xFFu),
        (uint8_t)((pressure_x10 >> 8) & 0xFFu),
        save_to_flash ? CHAIN_SAVE_FLASH_ENABLE : 0u,
    };
    uint8_t p[8];
    size_t len = 0;
    const EnvChainStatus status = env_chain_transaction(
        s_env_id,
        CHAIN_ENV_SET_SPA06_SEA_LEVEL_PRESSURE,
        data,
        sizeof(data),
        p,
        &len,
        SET_TIMEOUT_MS);
    if(status != EnvChainStatusOk) return status;
    if(len < 1 || p[0] != 1) return EnvChainStatusReturnPacketError;
    return EnvChainStatusOk;
}

EnvChainStatus env_chain_calibrate_altitude(int32_t altitude_cm, bool save_to_flash) {
    if(!s_env_id) return EnvChainStatusNotFound;

    const uint32_t raw = (uint32_t)altitude_cm;
    uint8_t data[5] = {
        (uint8_t)(raw & 0xFFu),
        (uint8_t)((raw >> 8) & 0xFFu),
        (uint8_t)((raw >> 16) & 0xFFu),
        (uint8_t)((raw >> 24) & 0xFFu),
        save_to_flash ? CHAIN_SAVE_FLASH_ENABLE : 0u,
    };
    uint8_t p[8];
    size_t len = 0;
    const EnvChainStatus status = env_chain_transaction(
        s_env_id,
        CHAIN_ENV_CALIBRATE_SPA06_SEA_LEVEL_PRESSURE,
        data,
        sizeof(data),
        p,
        &len,
        CALIB_TIMEOUT_MS);
    if(status != EnvChainStatusOk) return status;
    if(len < 1 || p[0] != 1) return EnvChainStatusReturnPacketError;
    return EnvChainStatusOk;
}

EnvChainStatus env_chain_get_sealevel(uint16_t* pressure_x10) {
    if(!pressure_x10 || !s_env_id) return EnvChainStatusNotFound;
    uint8_t p[8];
    size_t len = 0;
    const EnvChainStatus status = env_chain_transaction(
        s_env_id, CHAIN_ENV_GET_SPA06_SEA_LEVEL_PRESSURE, NULL, 0, p, &len, READ_TIMEOUT_MS);
    if(status != EnvChainStatusOk) return status;
    if(len < 2) return EnvChainStatusReturnPacketError;
    *pressure_x10 = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    return EnvChainStatusOk;
}
