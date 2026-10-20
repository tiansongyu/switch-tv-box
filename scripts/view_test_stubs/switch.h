#pragma once
#include <stdint.h>
typedef uint32_t u32;
typedef struct { int unused; } Framebuffer;
uint64_t armGetSystemTick(void);
uint64_t armTicksToNs(uint64_t ticks);
void consoleInit(void *console);
void consoleClear(void);
