/**
 * @file env_chain_protocol_test.c
 * @brief Host-side unit test for the Chain frame codec (no ESP-IDF needed).
 *
 * The crc8/build_frame/logic below are kept byte-for-byte in sync with
 * components/env_chain/env_chain.c. `check()` is a replica of M5Stack's
 * ChainCommon.cpp::checkPacket, so a passing test means our frames are
 * accepted by the official firmware and our decoding of its replies matches.
 *
 * Build:  gcc -Wall -Wextra -o env_chain_protocol_test env_chain_protocol_test.c
 * Run:    ./env_chain_protocol_test
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PACK_HEAD_HIGH 0xAAu
#define PACK_HEAD_LOW 0x55u
#define PACK_END_HIGH 0x55u
#define PACK_END_LOW 0xAAu
#define MAX_FRAME_DATA 32u

static uint8_t crc8(const uint8_t* frame, uint16_t size) {
    uint8_t crc = 0;
    if(!frame || size < 4) return 0;
    for(uint16_t i = 4; i < (uint16_t)(size - 3); i++) crc = (uint8_t)(crc + frame[i]);
    return crc;
}

static uint16_t build(
    uint16_t id,
    uint8_t cmd,
    const uint8_t* data,
    uint16_t data_len,
    uint8_t* out) {
    if(data_len > MAX_FRAME_DATA) return 0;
    const uint16_t total = (uint16_t)(data_len + 9u);
    const uint16_t cmd_size = (uint16_t)(3u + data_len);
    out[0] = PACK_HEAD_HIGH;
    out[1] = PACK_HEAD_LOW;
    out[2] = (uint8_t)(cmd_size & 0xFFu);
    out[3] = (uint8_t)((cmd_size >> 8) & 0xFFu);
    out[4] = (uint8_t)(id & 0xFFu);
    out[5] = cmd;
    if(data_len && data) memcpy(out + 6, data, data_len);
    out[total - 3] = crc8(out, total);
    out[total - 2] = PACK_END_HIGH;
    out[total - 1] = PACK_END_LOW;
    return total;
}

/* Replica of ChainCommon.cpp::checkPacket. */
static int check(const uint8_t* b, uint16_t size) {
    if(size < 9) return 0;
    if(b[0] != 0xAA || b[1] != 0x55 || b[size - 1] != 0xAA || b[size - 2] != 0x55) return 0;
    uint16_t length = (uint16_t)((uint16_t)b[3] << 8) | b[2];
    length = (uint16_t)(length + 6u);
    if(length != size) return 0;
    if(crc8(b, size) != b[size - 3]) return 0;
    return 1;
}

static void dump(const char* label, uint16_t n, const uint8_t* f) {
    printf("%-22s len=%2u: ", label, n);
    for(uint16_t i = 0; i < n; i++) printf("%02X ", f[i]);
    printf("\n");
}

int main(void) {
    uint8_t f[64];

    uint16_t n = build(0xFF, 0xFD, NULL, 0, f);
    dump("heartbeat", n, f);
    assert(n == 9);
    assert(check(f, n));
    assert(f[2] == 3 && f[3] == 0 && f[4] == 0xFF && f[5] == 0xFD);
    uint8_t expect_crc = 0;
    for(int i = 4; i < n - 3; i++) expect_crc = (uint8_t)(expect_crc + f[i]);
    assert(f[n - 3] == expect_crc);

    const uint8_t d1[] = {0x00};
    n = build(0xFF, 0xFE, d1, 1, f);
    dump("enumerate", n, f);
    assert(n == 10);
    assert(check(f, n));
    assert(f[6] == 0x00);

    /* 1013.2 hPa -> 10132 = 0x2794, save=1 */
    const uint8_t d2[] = {0x94, 0x27, 0x01};
    n = build(0x01, 0x36, d2, 3, f);
    dump("set sealevel", n, f);
    assert(n == 12);
    assert(check(f, n));
    assert(f[4] == 1 && f[5] == 0x36 && f[6] == 0x94 && f[7] == 0x27 && f[8] == 1);

    /* 42.5 m -> 4250 cm = 0x0000109A, save=1 */
    const uint8_t d3[] = {0x9A, 0x10, 0x00, 0x00, 0x01};
    n = build(0x01, 0x38, d3, 5, f);
    dump("calib by altitude", n, f);
    assert(n == 14);
    assert(check(f, n));

    /* AHT20 reply: size 12 => length field 6 (3 id/cmd + 3 data) */
    uint8_t aht[12] = {0xAA, 0x55, 6, 0, 1, 0x30, 0xE2, 0x09, 61, 0, 0, 0};
    aht[9] = crc8(aht, 12);
    aht[10] = 0x55;
    aht[11] = 0xAA;
    assert(check(aht, 12));
    const int16_t t = (int16_t)((uint16_t)aht[6] | ((uint16_t)aht[7] << 8));
    printf("AHT decode: temp=%d.%02dC humi=%u%%\n", t / 100, t % 100, aht[8]);
    assert(t == 2530 && aht[8] == 61);

    /* SPA06 reply: size 17 => length field 11 (3 + 8 data) */
    uint8_t spa[17] = {
        0xAA, 0x55, 11, 0, 1, 0x31, 0xE2, 0x09, 0x64, 0x27, 0x68, 0x10, 0, 0, 0, 0, 0};
    spa[14] = crc8(spa, 17);
    spa[15] = 0x55;
    spa[16] = 0xAA;
    assert(check(spa, 17));
    const uint16_t p = (uint16_t)spa[8] | ((uint16_t)spa[9] << 8);
    const int32_t alt = (int32_t)((uint32_t)spa[10] | ((uint32_t)spa[11] << 8) |
                                  ((uint32_t)spa[12] << 16) | ((uint32_t)spa[13] << 24));
    printf("SPA decode: press=%u.%uhPa alt=%dcm\n", p / 10, p % 10, alt);
    assert(p == 10084 && alt == 4200);

    spa[8] ^= 0xFF; /* corrupted payload must be rejected */
    assert(!check(spa, 17));

    puts("ALL PROTOCOL TESTS PASSED");
    return 0;
}
