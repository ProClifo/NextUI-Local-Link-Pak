#ifndef NEXTUI_LOCAL_LINK_MGBA_BACKEND_H
#define NEXTUI_LOCAL_LINK_MGBA_BACKEND_H

#include "session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LL_GBA_WIDTH 240
#define LL_GBA_HEIGHT 160

typedef struct LLMgbaBackend LLMgbaBackend;

typedef struct {
    const uint32_t *pixels;
    unsigned width;
    unsigned height;
    size_t pitch_bytes;
} LLVideoFrame;

LLMgbaBackend *ll_mgba_backend_create(void);
void ll_mgba_backend_destroy(LLMgbaBackend *backend);

const LLBackend *ll_mgba_backend_interface(void);

LLResult ll_mgba_backend_run_frame(LLMgbaBackend *backend,
                                   const uint32_t keys[LL_MAX_PLAYERS]);
LLResult ll_mgba_backend_get_frame(const LLMgbaBackend *backend,
                                   unsigned player_id,
                                   LLVideoFrame *out_frame);

bool ll_mgba_backend_is_loaded(const LLMgbaBackend *backend, unsigned player_id);
unsigned ll_mgba_backend_active_player(const LLMgbaBackend *backend);

#endif
