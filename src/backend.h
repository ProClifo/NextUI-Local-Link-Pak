#ifndef LOCAL_LINK_BACKEND_H
#define LOCAL_LINK_BACKEND_H

#include <stdbool.h>

#include "session.h"

typedef struct LLBackendState LLBackendState;

LLBackendState *ll_backend_create(void);
void ll_backend_destroy(LLBackendState *state);

LLResult ll_backend_load_instance(unsigned player_id, const char *rom_path, void *userdata);
LLResult ll_backend_save_instance(unsigned player_id, void *userdata);
LLResult ll_backend_close_instance(unsigned player_id, void *userdata);
LLResult ll_backend_set_active_instance(unsigned player_id, void *userdata);

LLBackend ll_backend_callbacks(void);

bool ll_backend_has_instance(const LLBackendState *state, unsigned player_id);
unsigned ll_backend_active_instance(const LLBackendState *state);
const char *ll_backend_rom_path(const LLBackendState *state, unsigned player_id);

#endif
