#pragma once

#include <stdint.h>

/* Thin MinArch-side wrapper around the optional Local Link libretro ABI. */
int LLMinarch_supported(void);
uint32_t LLMinarch_loadedMask(void);
unsigned LLMinarch_activeSlot(void);
int LLMinarch_addPath(unsigned slot, const char *path);
int LLMinarch_addNextPath(const char *path, unsigned *slot_out);
int LLMinarch_switchTo(unsigned slot);
int LLMinarch_switchNext(void);
int LLMinarch_remove(unsigned slot);
int LLMinarch_removeActive(void);
