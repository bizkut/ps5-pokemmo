/* Minimal GNU byteswap compatibility for the PS5 payload SDK. SPDX-License-Identifier: MIT */
#ifndef POKEMMOPS5_BYTESWAP_H
#define POKEMMOPS5_BYTESWAP_H

#include <stdint.h>

#define bswap_16(value) __builtin_bswap16((uint16_t)(value))
#define bswap_32(value) __builtin_bswap32((uint32_t)(value))
#define bswap_64(value) __builtin_bswap64((uint64_t)(value))

#endif
