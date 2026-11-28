/**
 * @file env_chain.h
 * @brief Minimal M5Stack Chain protocol client for the Chain ENV node.
 *
 * Reimplements only the slice of M5Chain needed here, byte-for-byte against
 * ChainCommon.cpp / ChainENV.cpp (MIT, M5Stack). All multi-byte fields are
 * little-endian; CRC8 is a summing checksum over frame bytes [4 .. size-4].
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Chain status codes (subset of chain_status_t). */
typedef enum {
    EnvChainStatusOk = 0,
    EnvChainStatusParameterError,
    EnvChainStatusTimeout,
    EnvChainStatusReturnPacketError,
    EnvChainStatusBusy,
    EnvChainStatusNotFound,
} EnvChainStatus;

typedef struct {
    int16_t temp_aht_x100;  /* 0.01 degC, from AHT20 */
    uint8_t humidity;       /* %RH */
    uint16_t pressure_x10;  /* hPa * 10, from SPA06 */
    int32_t altitude_cm;    /* cm (divide by 100 for metres) */
} EnvChainData;

/* ---- Chain command bytes (ChainCommon.hpp / ChainENV.hpp) ---- */
#define CHAIN_HEARTBEAT 0xFD
#define CHAIN_ENUM 0xFE
#define CHAIN_GET_DEVICE_TYPE 0xFB
#define CHAIN_ENV_GET_AHT20_DATA 0x30
#define CHAIN_ENV_GET_SPA06_DATA 0x31
#define CHAIN_ENV_SET_SPA06_SEA_LEVEL_PRESSURE 0x36
#define CHAIN_ENV_GET_SPA06_SEA_LEVEL_PRESSURE 0x37
#define CHAIN_ENV_CALIBRATE_SPA06_SEA_LEVEL_PRESSURE 0x38
#define CHAIN_ENV_TYPE_CODE 0x000F

#define CHAIN_ENV_SEA_LEVEL_PRESSURE_MIN_HPA_X10 3000u /* 300.0 hPa */
#define CHAIN_ENV_SEA_LEVEL_PRESSURE_MAX_HPA_X10 11000u /* 1100.0 hPa */
#define CHAIN_ENV_CALIBRATION_MIN_TIMEOUT_MS 5000u
#define CHAIN_SAVE_FLASH_ENABLE 1u

#define CHAIN_BROADCAST_ID 0xFFu

/**
 * Bring up the transport and scan ids 1..16 for a node whose type is 0x000F.
 * On success the discovered id is cached inside the module.
 * Returns true if an ENV node answered.
 */
bool env_chain_init(void);

/** Tear down the transport. Safe to call repeatedly. */
void env_chain_deinit(void);

/** Cached id found by env_chain_init(), or 0. */
uint8_t env_chain_get_id(void);

/** Read temperature/humidity + pressure/altitude. */
EnvChainStatus env_chain_read(EnvChainData* out);

/** Set sea-level pressure directly. pressure_x10 in [3000,11000]. */
EnvChainStatus env_chain_set_sealevel(uint16_t pressure_x10, bool save_to_flash);

/** Calibrate sea-level pressure from a reference altitude (cm, signed). */
EnvChainStatus env_chain_calibrate_altitude(int32_t altitude_cm, bool save_to_flash);

/** Read back the sea-level pressure currently held by the ENV node. */
EnvChainStatus env_chain_get_sealevel(uint16_t* pressure_x10);

/** Sum-check CRC used by ChainCommon.cpp. Exposed for offline unit test. */
uint8_t env_chain_crc8(const uint8_t* frame, uint16_t size);

/** Build a request frame. Exposed for offline unit test. Returns frame length. */
uint16_t env_chain_build_frame(
    uint16_t id,
    uint8_t cmd,
    const uint8_t* data,
    uint16_t data_len,
    uint8_t* out_frame);

#ifdef __cplusplus
}
#endif
