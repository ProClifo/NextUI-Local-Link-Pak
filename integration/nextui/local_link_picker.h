#pragma once

#include <stddef.h>

/* Pick the first GBA ROM for Tools/h700/Local Link.pak.
 * Returns 1 and writes the full path on success, 0 on cancel/failure. */
int LLPicker_pickFirstRom(const char *directory, char *out_path, size_t out_size);
