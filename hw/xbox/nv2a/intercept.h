// intercept.h

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef int16_t s16;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef size_t usz;

bool NewDrawBegin(int drawMode_);
void NewDrawEnd(void);
bool NewDrawMain(void);
void NewFrame(bool start);
void SetIndices(u32* indices_, u32 indexCount_, u32 minIndex_, u32 maxIndex_);
void SetIntercept(int value);

#ifdef __cplusplus
}
#endif
