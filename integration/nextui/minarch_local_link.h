#pragma once

#include <stdint.h>

struct MenuList;

/* Thin MinArch-side wrapper around the optional Local Link libretro ABI. */
int LLMinarch_supported(void);
uint32_t LLMinarch_loadedMask(void);
unsigned LLMinarch_activeSlot(void);
unsigned LLMinarch_instanceCount(void);
int LLMinarch_addPath(unsigned slot, const char *path);
int LLMinarch_addNextPath(const char *path, unsigned *slot_out);
int LLMinarch_switchTo(unsigned slot);
int LLMinarch_switchNext(void);
int LLMinarch_remove(unsigned slot);
int LLMinarch_removeActive(void);

/* Menu callbacks used by Options > Emulator. */
int LLMinarch_menuAdd(struct MenuList *list, int index);
int LLMinarch_menuSwitch(struct MenuList *list, int index);
int LLMinarch_menuRemove(struct MenuList *list, int index);
