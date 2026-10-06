#include "mgba_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct StubInstance {
    bool loaded;
    char rom_path[LL_PATH_MAX];
    uint32_t framebuffer[LL_GBA_WIDTH * LL_GBA_HEIGHT];
};

struct LLMgbaBackend {
    struct StubInstance instances[LL_MAX_PLAYERS];
    unsigned active_player;
};

static struct StubInstance *instance_mut(LLMgbaBackend *backend, unsigned player_id) {
    if (!backend || player_id < 1 || player_id > LL_MAX_PLAYERS) {
        return NULL;
    }
    return &backend->instances[player_id - 1];
}

static const struct StubInstance *instance_const(const LLMgbaBackend *backend, unsigned player_id) {
    if (!backend || player_id < 1 || player_id > LL_MAX_PLAYERS) {
        return NULL;
    }
    return &backend->instances[player_id - 1];
}

static LLResult stub_load(unsigned player_id, const char *rom_path, void *userdata) {
    LLMgbaBackend *backend = userdata;
    struct StubInstance *instance = instance_mut(backend, player_id);
    if (!instance || !rom_path || !rom_path[0]) {
        return LL_ERR_INVALID_ARGUMENT;
    }
    if (instance->loaded) {
        return LL_ERR_SLOT_OCCUPIED;
    }

    memset(instance, 0, sizeof(*instance));
    instance->loaded = true;
    snprintf(instance->rom_path, sizeof(instance->rom_path), "%s", rom_path);

    uint32_t shade = 0xFF000000u | (0x303030u * player_id);
    for (size_t i = 0; i < LL_GBA_WIDTH * LL_GBA_HEIGHT; ++i) {
        instance->framebuffer[i] = shade;
    }

    return LL_OK;
}

static LLResult stub_save(unsigned player_id, void *userdata) {
    LLMgbaBackend *backend = userdata;
    const struct StubInstance *instance = instance_const(backend, player_id);
    return instance && instance->loaded ? LL_OK : LL_ERR_SLOT_EMPTY;
}

static LLResult stub_close(unsigned player_id, void *userdata) {
    LLMgbaBackend *backend = userdata;
    struct StubInstance *instance = instance_mut(backend, player_id);
    if (!instance || !instance->loaded) {
        return LL_ERR_SLOT_EMPTY;
    }
    memset(instance, 0, sizeof(*instance));
    if (backend->active_player == player_id) {
        backend->active_player = 0;
    }
    return LL_OK;
}

static LLResult stub_set_active(unsigned player_id, void *userdata) {
    LLMgbaBackend *backend = userdata;
    const struct StubInstance *instance = instance_const(backend, player_id);
    if (!instance || !instance->loaded) {
        return LL_ERR_SLOT_EMPTY;
    }
    backend->active_player = player_id;
    return LL_OK;
}

static const LLBackend BACKEND = {
    .load_instance = stub_load,
    .save_instance = stub_save,
    .close_instance = stub_close,
    .set_active_instance = stub_set_active,
};

LLMgbaBackend *ll_mgba_backend_create(void) {
    return calloc(1, sizeof(LLMgbaBackend));
}

void ll_mgba_backend_destroy(LLMgbaBackend *backend) {
    free(backend);
}

const LLBackend *ll_mgba_backend_interface(void) {
    return &BACKEND;
}

LLResult ll_mgba_backend_run_frame(LLMgbaBackend *backend,
                                   const uint32_t keys[LL_MAX_PLAYERS]) {
    (void) keys;
    return backend ? LL_OK : LL_ERR_INVALID_ARGUMENT;
}

LLResult ll_mgba_backend_get_frame(const LLMgbaBackend *backend,
                                   unsigned player_id,
                                   LLVideoFrame *out_frame) {
    const struct StubInstance *instance = instance_const(backend, player_id);
    if (!instance || !instance->loaded || !out_frame) {
        return LL_ERR_SLOT_EMPTY;
    }

    out_frame->pixels = instance->framebuffer;
    out_frame->width = LL_GBA_WIDTH;
    out_frame->height = LL_GBA_HEIGHT;
    out_frame->pitch_bytes = LL_GBA_WIDTH * sizeof(uint32_t);
    return LL_OK;
}

bool ll_mgba_backend_is_loaded(const LLMgbaBackend *backend, unsigned player_id) {
    const struct StubInstance *instance = instance_const(backend, player_id);
    return instance && instance->loaded;
}

unsigned ll_mgba_backend_active_player(const LLMgbaBackend *backend) {
    return backend ? backend->active_player : 0;
}
