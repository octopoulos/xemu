#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>

uint64_t fast_hash(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
