/*
 * Native libmgba backend for NextUI Local Link.
 *
 * This file intentionally is not part of the default host test build. It is
 * compiled by the NextUI/cross build when LL_WITH_LIBMGBA is enabled and the
 * mGBA headers/library are available.
 *
 * The lockstep approach follows mGBA's GBASIOLockstepCoordinator API and the
 * proven in-process dual-instance design used by bmpriest/nextui-netplay.
 */

#include "mgba_backend.h"

#ifdef LL_WITH_LIBMGBA

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/lockstep.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/sio/lockstep.h>
#include <mgba-util/vfs.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LL_VIDEO_STRIDE 256u
#define LL_VIDEO_HEIGHT 224u
#define LL_RUN_GUARD 131072u

struct LLMgbaInstance {
    struct mCore *core;
    struct GBASIOLockstepDriver driver;
    struct mLockstepUser user;
    uint32_t *video;
    unsigned slot_index;      /* 0..3; also the requested physical cable ID */
    int assigned_player_id;   /* mGBA's actual cable ID, -1 while detached */
    bool loaded;
    bool asleep;
    bool attached;
    char rom_path[LL_PATH_MAX];
};

struct LLMgbaBackend {
    struct LLMgbaInstance instances[LL_MAX_PLAYERS];
    struct GBASIOLockstepCoordinator coordinator;
    bool coordinator_initialized;
    unsigned active_player;
};

static struct LLMgbaInstance *instance_from_user(struct mLockstepUser *user) {
    return (struct LLMgbaInstance *) ((char *) user - offsetof(struct LLMgbaInstance, user));
}

static void link_sleep(struct mLockstepUser *user) {
    instance_from_user(user)->asleep = true;
}

static void link_wake(struct mLockstepUser *user) {
    instance_from_user(user)->asleep = false;
}

static int link_requested_id(struct mLockstepUser *user) {
    return (int) instance_from_user(user)->slot_index;
}

static void link_player_id_changed(struct mLockstepUser *user, int id) {
    instance_from_user(user)->assigned_player_id = id;
}

static struct LLMgbaInstance *instance_mut(LLMgaBackend *backend, unsigned player_id) {
    if (!backend || player_id < 1 || player_id > LL_MAX_PLAYERS) {
        return NULL;
    }
    return &backend->instances[player_id - 1];
}

static const struct LLMgbaInstance *instance_const(const LLMgbaBackend *backend,
                                                   unsigned player_id) {
    if (!backend || player_id < 1 || player_id > LL_MAX_PLAYERS) {
        return NULL;
    }
    return &backend->instances[player_id - 1];
}

static size_t loaded_count(const LLMgbaBackend *backend) {
    size_t count = 0;
    for (unsigned i = 0; i < LL_MAX_PLAYERS; ++i) {
        if (backend->instances[i].loaded) {
            ++count;
        }
    }
    return count;
}

static void detach_instance(LLMgaBackend *backend, struct LLMgbaInstance *instance) {
    if (!backend || !instance || !instance->attached || !backend->coordinator_initialized) {
        return;
    }

    struct GBA *gba = instance->core ? instance->core->board : NULL;
    if (gba) {
        GBASIOSetDriver(&gba->sio, NULL);
    }
    GBASIOLockstepCoordinatorDetach(&backend->coordinator, &instance->driver);
    instance->attached = false;
    instance->asleep = false;
    instance->assigned_player_id = -1;
}

static void destroy_coordinator(LLMgaBackend *backend) {
    if (!backend || !backend->coordinator_initialized) {
        return;
    }

    for (unsigned i = 0; i < LL_MAX_PLAYERS; ++i) {
        detach_instance(backend, &backend->instances[i]);
    }
    GBASIOLockstepCoordinatorDeinit(&backend->coordinator);
    memset(&backend->coordinator, 0, sizeof(backend->coordinator));
    backend->coordinator_initialized = false;
}

/* Rebuild the physical cable topology from the currently occupied fixed slots.
 * requestedId() returns slot_index, so P1/P3 remain cable IDs 0/2 even if P2
 * has been removed. */
static LLResult rebuild_link_bus(LLMgaBackend *backend) {
    if (!backend) {
        return LL_ERR_INVALID_ARGUMENT;
    }

    destroy_coordinator(backend);

    if (loaded_count(backend) < 2) {
        return LL_OK;
    }

    GBASIOLockstepCoordinatorInit(&backend->coordinator);
    backend->coordinator_initialized = true;

    for (unsigned i = 0; i < LL_MAX_PLAYERS; ++i) {
        struct LLMgbaInstance *instance = &backend->instances[i];
        if (!instance->loaded) {
            continue;
        }

        memset(&instance->driver, 0, sizeof(instance->driver));
        memset(&instance->user, 0, sizeof(instance->user));
        instance->slot_index = i;
        instance->assigned_player_id = -1;
        instance->asleep = false;
        instance->user.sleep = link_sleep;
        instance->user.wake = link_wake;
        instance->user.requestedId = link_requested_id;
        instance->user.playerIdChanged = link_player_id_changed;

        GBASIOLockstepDriverCreate(&instance->driver, &instance->user);
        GBASIOLockstepCoordinatorAttach(&backend->coordinator, &instance->driver);

        struct GBA *gba = instance->core->board;
        GBASIOSetDriver(&gba->sio, &instance->driver.d);
        instance->attached = true;
    }

    return LL_OK;
}

static void clear_instance(LLMgaBackend *backend, struct LLMgbaInstance *instance) {
    if (!instance) {
        return;
    }

    if (backend && instance->attached) {
        detach_instance(backend, instance);
    }
    if (instance->core) {
        mCoreConfigDeinit(&instance->core->config);
        instance->core->deinit(instance->core);
    }
    free(instance->video);

    unsigned slot_index = instance->slot_index;
    memset(instance, 0, sizeof(*instance));
    instance->slot_index = slot_index;
    instance->assigned_player_id = -1;
}

static LLResult load_core(struct LLMgbaInstance *instance, const char *rom_path) {
    struct VFile *rom = VFileOpen(rom_path, O_RDONLY);
    if (!rom) {
        return LL_ERR_BACKEND;
    }

    struct mCore *core = mCoreFindVF(rom);
    if (!core || core->platform(core) != mPLATFORM_GBA) {
        rom->close(rom);
        return LL_ERR_BACKEND;
    }

    mCoreInitConfig(core, NULL);
    if (!core->init(core)) {
        rom->close(rom);
        mCoreConfigDeinit(&core->config);
        core->deinit(core);
        return LL_ERR_BACKEND;
    }

    struct mCoreOptions opts = {
        .useBios = true,
        .volume = 0x100,
    };
    mCoreConfigLoadDefaults(&core->config, &opts);
    mCoreLoadConfig(core);
    core->opts.skipBios = true;

    uint32_t *video = calloc(LL_VIDEO_STRIDE * LL_VIDEO_HEIGHT, sizeof(*video));
    if (!video) {
        mCoreConfigDeinit(&core->config);
        core->deinit(core);
        rom->close(rom);
        return LL_ERR_BACKEND;
    }
    core->setVideoBuffer(core, video, LL_VIDEO_STRIDE);

    if (!core->loadROM(core, rom)) {
        free(video);
        mCoreConfigDeinit(&core->config);
        core->deinit(core);
        rom->close(rom);
        return LL_ERR_BACKEND;
    }
    core->reset(core);

    instance->core = core;
    instance->video = video;
    instance->loaded = true;
    instance->assigned_player_id = -1;
    instance->asleep = false;
    instance->attached = false;
    snprintf(instance->rom_path, sizeof(instance->rom_path), "%s", rom_path);
    return LL_OK;
}

static LLResult backend_load(unsigned player_id, const char *rom_path, void *userdata) {
    LLMgbaBackend *backend = userdata;
    struct LLMgbaInstance *instance = instance_mut(backend, player_id);
    if (!instance || !rom_path || !rom_path[0]) {
        return LL_ERR_INVALID_ARGUMENT;
    }
    if (instance->loaded) {
        return LL_ERR_SLOT_OCCUPIED;
    }

    LLResult result = load_core(instance, rom_path);
    if (result != LL_OK) {
        return result;
    }

    result = rebuild_link_bus(backend);
    if (result != LL_OK) {
        clear_instance(backend, instance);
        rebuild_link_bus(backend);
        return result;
    }
    return LL_OK;
}

static LLResult backend_save(unsigned player_id, void *userdata) {
    LLMgbaBackend *backend = userdata;
    const struct LLMgbaInstance *instance = instance_const(backend, player_id);
    if (!instance || !instance->loaded) {
        return LL_ERR_SLOT_EMPTY;
    }

    /* Battery-save path integration is intentionally handled by the NextUI
     * adapter. mGBA's loaded save VFile is persistent; this hook exists so
     * Save & Quit can force a sync once the NextUI save-path layer is wired. */
    return LL_OK;
}

static LLResult backend_close(unsigned player_id, void *userdata) {
    LLMgbaBackend *backend = userdata;
    struct LLMgbaInstance *instance = instance_mut(backend, player_id);
    if (!instance || !instance->loaded) {
        return LL_ERR_SLOT_EMPTY;
    }

    bool was_active = backend->active_player == player_id;
    clear_instance(backend, instance);
    if (was_active) {
        backend->active_player = 0;
    }
    return rebuild_link_bus(backend);
}

static LLResult backend_set_active(unsigned player_id, void *userdata) {
    LLMgbaBackend *backend = userdata;
    const struct LLMgbaInstance *instance = instance_const(backend, player_id);
    if (!instance || !instance->loaded) {
        return LL_ERR_SLOT_EMPTY;
    }
    backend->active_player = player_id;
    return LL_OK;
}

static const LLBackend BACKEND = {
    .load_instance = backend_load,
    .save_instance = backend_save,
    .close_instance = backend_close,
    .set_active_instance = backend_set_active,
};

LLMgaBackend *ll_mgba_backend_create(void) {
    LLMgbaBackend *backend = calloc(1, sizeof(*backend));
    if (!backend) {
        return NULL;
    }
    for (unsigned i = 0; i < LL_MAX_PLAYERS; ++i) {
        backend->instances[i].slot_index = i;
        backend->instances[i].assigned_player_id = -1;
    }
    return backend;
}

void ll_mgba_backend_destroy(LLMgaBackend *backend) {
    if (!backend) {
        return;
    }
    destroy_coordinator(backend);
    for (unsigned i = 0; i < LL_MAX_PLAYERS; ++i) {
        clear_instance(NULL, &backend->instances[i]);
    }
    free(backend);
}

const LLBackend *ll_mgba_backend_interface(void) {
    return &BACKEND;
}

/* Advance every occupied GBA by one frame. During a cable transfer mGBA can
 * put one participant to sleep until another catches up, so we round-robin
 * runLoop() calls exactly like the dual-instance proof of concept. */
LLResult ll_mgba_backend_run_frame(LLMgaBackend *backend,
                                   const uint32_t keys[LL_MAX_PLAYERS]) {
    if (!backend) {
        return LL_ERR_INVALID_ARGUMENT;
    }

    uint32_t targets[LL_MAX_PLAYERS] = {0};
    size_t count = 0;
    for (unsigned i = 0; i < LL_MAX_PLAYERS; ++i) {
        struct LLMgbaInstance *instance = &backend->instances[i];
        if (!instance->loaded) {
            continue;
        }
        instance->core->setKeys(instance->core, keys ? keys[i] : 0);
        targets[i] = instance->core->frameCounter(instance->core) + 1;
        ++count;
    }
    if (!count) {
        return LL_ERR_SESSION_EMPTY;
    }

    unsigned steps = 0;
    while (steps < LL_RUN_GUARD) {
        bool all_at_target = true;
        for (unsigned i = 0; i < LL_MAX_PLAYERS; ++i) {
            struct LLMgbaInstance *instance = &backend->instances[i];
            if (instance->loaded && instance->core->frameCounter(instance->core) < targets[i]) {
                all_at_target = false;
                break;
            }
        }

        bool cable_idle = !backend->coordinator_initialized ||
                          (!backend->coordinator.waiting && !backend->coordinator.transferActive);
        if (all_at_target && cable_idle) {
            return LL_OK;
        }

        bool ran = false;
        for (unsigned i = 0; i < LL_MAX_PLAYERS; ++i) {
            struct LLMgbaInstance *instance = &backend->instances[i];
            if (!instance->loaded || instance->asleep) {
                continue;
            }
            instance->core->runLoop(instance->core);
            ran = true;
            ++steps;
            if (steps >= LL_RUN_GUARD) {
                break;
            }
        }
        if (!ran) {
            return LL_ERR_BACKEND;
        }
    }

    return LL_ERR_BACKEND;
}

LLResult ll_mgba_backend_get_frame(const LLMgbaBackend *backend,
                                   unsigned player_id,
                                   LLVideoFrame *out_frame) {
    const struct LLMgbaInstance *instance = instance_const(backend, player_id);
    if (!instance || !instance->loaded || !out_frame) {
        return LL_ERR_SLOT_EMPTY;
    }

    unsigned width = LL_GBA_WIDTH;
    unsigned height = LL_GBA_HEIGHT;
    instance->core->currentVideoSize(instance->core, &width, &height);
    out_frame->pixels = instance->video;
    out_frame->width = width;
    out_frame->height = height;
    out_frame->pitch_bytes = LL_VIDEO_STRIDE * sizeof(uint32_t);
    return LL_OK;
}

bool ll_mgba_backend_is_loaded(const LLMgbaBackend *backend, unsigned player_id) {
    const struct LLMgbaInstance *instance = instance_const(backend, player_id);
    return instance && instance->loaded;
}

unsigned ll_mgba_backend_active_player(const LLMgbaBackend *backend) {
    return backend ? backend->active_player : 0;
}

#else
#error "mgba_backend_libmgba.c requires LL_WITH_LIBMGBA"
#endif
