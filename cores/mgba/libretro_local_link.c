/*
 * mGBA Local Link libretro frontend
 *
 * One libretro core owns up to four independent mGBA instances and attaches
 * occupied slots to one in-process GBA lockstep coordinator. MinArch sees one
 * ordinary core plus the optional retro_local_link_* extension ABI.
 *
 * Initial implementation based on mGBA's lockstep APIs and the paired-core
 * design demonstrated by bmpriest/nextui-netplay.
 */

#include "libretro.h"
#include "local_link_abi.h"

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/memory.h>
#include <mgba/internal/gba/savedata.h>
#include <mgba/internal/gba/sio/lockstep.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
#include <mgba-util/vfs.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_STRIDE 256u
#define VIDEO_HEIGHT 224u
#define AUDIO_FRAMES 4096u
#define AUDIO_OUTPUT_FRAMES (AUDIO_FRAMES * 2u)
#define AUDIO_DEFAULT_RATE 65536u
#define RUN_GUARD 131072u

struct LocalConsole {
    struct mCore *core;
    struct GBASIOLockstepDriver driver;
    struct mLockstepUser user;
    struct mAudioBuffer resample_buffer;
    struct mAudioResampler resampler;
    unsigned resampler_source_rate;
    mColor *video;
    void *rom_data;
    size_t rom_size;
    uint8_t *savedata;
    unsigned slot;
    int cable_id;
    bool loaded;
    bool attached;
    bool asleep;
    bool audio_init;
};

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static struct LocalConsole consoles[LOCAL_LINK_MAX_PLAYERS];
static struct GBASIOLockstepCoordinator coordinator;
static bool coordinator_initialized;
static unsigned active_slot;
static bool input_bitmasks;
static unsigned audio_output_rate = AUDIO_DEFAULT_RATE;

static struct LocalConsole *console_from_user(struct mLockstepUser *user) {
    return (struct LocalConsole *) ((char *) user - offsetof(struct LocalConsole, user));
}

static void user_sleep(struct mLockstepUser *user) {
    console_from_user(user)->asleep = true;
}

static void user_wake(struct mLockstepUser *user) {
    console_from_user(user)->asleep = false;
}

static int user_requested_id(struct mLockstepUser *user) {
    return (int) console_from_user(user)->slot;
}

static void user_player_id_changed(struct mLockstepUser *user, int id) {
    console_from_user(user)->cable_id = id;
}

static uint32_t loaded_mask(void) {
    uint32_t mask = 0;
    for (unsigned i = 0; i < LOCAL_LINK_MAX_PLAYERS; ++i) {
        if (consoles[i].loaded) mask |= 1u << i;
    }
    return mask;
}

static unsigned loaded_count(void) {
    uint32_t mask = loaded_mask();
    unsigned count = 0;
    while (mask) {
        count += mask & 1u;
        mask >>= 1;
    }
    return count;
}

static unsigned next_loaded(unsigned after) {
    uint32_t mask = loaded_mask();
    if (!mask) return 0;
    for (unsigned offset = 1; offset <= LOCAL_LINK_MAX_PLAYERS; ++offset) {
        unsigned slot = (after + offset) % LOCAL_LINK_MAX_PLAYERS;
        if (mask & (1u << slot)) return slot;
    }
    return 0;
}

static struct VFile *open_rom(const struct retro_game_info *game,
                              struct LocalConsole *console) {
    if (game->data && game->size) {
        console->rom_data = malloc(game->size);
        if (!console->rom_data) return NULL;
        memcpy(console->rom_data, game->data, game->size);
        console->rom_size = game->size;
        return VFileFromMemory(console->rom_data, console->rom_size);
    }
    return game->path ? VFileOpen(game->path, O_RDONLY) : NULL;
}

static void console_clear(struct LocalConsole *console) {
    if (!console) return;
    if (console->audio_init) {
        mAudioResamplerDeinit(&console->resampler);
        mAudioBufferDeinit(&console->resample_buffer);
    }
    if (console->core) {
        mCoreConfigDeinit(&console->core->config);
        console->core->deinit(console->core);
    }
    free(console->video);
    free(console->rom_data);
    free(console->savedata);

    unsigned slot = console->slot;
    memset(console, 0, sizeof(*console));
    console->slot = slot;
    console->cable_id = -1;
}

static void destroy_link_bus(void) {
    if (!coordinator_initialized) return;

    for (unsigned i = 0; i < LOCAL_LINK_MAX_PLAYERS; ++i) {
        struct LocalConsole *console = &consoles[i];
        if (!console->loaded || !console->attached) continue;
        struct GBA *gba = console->core->board;
        GBASIOSetDriver(&gba->sio, NULL);
        GBASIOLockstepCoordinatorDetach(&coordinator, &console->driver);
        console->attached = false;
        console->asleep = false;
        console->cable_id = -1;
    }

    GBASIOLockstepCoordinatorDeinit(&coordinator);
    memset(&coordinator, 0, sizeof(coordinator));
    coordinator_initialized = false;
}

static bool rebuild_link_bus(void) {
    destroy_link_bus();
    if (loaded_count() < 2) return true;

    GBASIOLockstepCoordinatorInit(&coordinator);
    coordinator_initialized = true;

    for (unsigned i = 0; i < LOCAL_LINK_MAX_PLAYERS; ++i) {
        struct LocalConsole *console = &consoles[i];
        if (!console->loaded) continue;

        memset(&console->driver, 0, sizeof(console->driver));
        memset(&console->user, 0, sizeof(console->user));
        console->asleep = false;
        console->cable_id = -1;
        console->user.sleep = user_sleep;
        console->user.wake = user_wake;
        console->user.requestedId = user_requested_id;
        console->user.playerIdChanged = user_player_id_changed;

        GBASIOLockstepDriverCreate(&console->driver, &console->user);
        GBASIOLockstepCoordinatorAttach(&coordinator, &console->driver);
        struct GBA *gba = console->core->board;
        GBASIOSetDriver(&gba->sio, &console->driver.d);
        console->attached = true;
    }
    return true;
}

static bool console_load(unsigned slot, const struct retro_game_info *game) {
    if (slot >= LOCAL_LINK_MAX_PLAYERS || !game || consoles[slot].loaded) return false;

    struct LocalConsole *console = &consoles[slot];
    console_clear(console);
    console->slot = slot;
    console->cable_id = -1;

    struct VFile *rom = open_rom(game, console);
    if (!rom) goto fail;

    console->core = mCoreFindVF(rom);
    if (!console->core || console->core->platform(console->core) != mPLATFORM_GBA) {
        rom->close(rom);
        goto fail;
    }

    mCoreInitConfig(console->core, NULL);
    if (!console->core->init(console->core)) {
        rom->close(rom);
        goto fail;
    }

    struct mCoreOptions options = {
        .useBios = true,
        .volume = 0x100,
    };
    mCoreConfigLoadDefaults(&console->core->config, &options);
    mCoreLoadConfig(console->core);
    console->core->opts.skipBios = true;

    console->video = calloc(VIDEO_STRIDE * VIDEO_HEIGHT, sizeof(*console->video));
    console->savedata = malloc(GBA_SIZE_FLASH1M);
    if (!console->video || !console->savedata) {
        rom->close(rom);
        goto fail;
    }
    memset(console->savedata, 0xFF, GBA_SIZE_FLASH1M);

    console->core->setVideoBuffer(console->core, console->video, VIDEO_STRIDE);
    console->core->setAudioBufferSize(console->core, AUDIO_FRAMES);
    mAudioBufferInit(&console->resample_buffer, AUDIO_OUTPUT_FRAMES, 2);
    mAudioResamplerInit(&console->resampler, mINTERPOLATOR_SINC);
    mAudioResamplerSetDestination(&console->resampler, &console->resample_buffer,
                                  audio_output_rate);
    console->audio_init = true;

    if (!console->core->loadROM(console->core, rom)) {
        rom->close(rom);
        goto fail;
    }
    console->core->reset(console->core);

    struct VFile *save = VFileFromMemory(console->savedata, GBA_SIZE_FLASH1M);
    if (!save || !console->core->loadSave(console->core, save)) {
        if (save) save->close(save);
        goto fail;
    }

    console->loaded = true;
    return true;

fail:
    console_clear(console);
    return false;
}

static void unload_all(void) {
    destroy_link_bus();
    for (unsigned i = 0; i < LOCAL_LINK_MAX_PLAYERS; ++i) {
        console_clear(&consoles[i]);
    }
    active_slot = 0;
}

static uint32_t read_local_keys(void) {
    static const unsigned keymap[] = {
        RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B,
        RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START,
        RETRO_DEVICE_ID_JOYPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_LEFT,
        RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN,
        RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L,
    };

    uint32_t keys = 0;
    int16_t mask = input_state_cb && input_bitmasks
        ? input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK)
        : 0;

    for (unsigned i = 0; i < sizeof(keymap) / sizeof(*keymap); ++i) {
        if ((input_bitmasks && (mask & (1 << keymap[i]))) ||
            (!input_bitmasks && input_state_cb &&
             input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, keymap[i]))) {
            keys |= 1u << i;
        }
    }
    return keys;
}

static bool run_linked_frame(void) {
    uint32_t targets[LOCAL_LINK_MAX_PLAYERS] = {0};
    for (unsigned i = 0; i < LOCAL_LINK_MAX_PLAYERS; ++i) {
        if (consoles[i].loaded) {
            targets[i] = consoles[i].core->frameCounter(consoles[i].core) + 1;
        }
    }

    unsigned steps = 0;
    while (steps < RUN_GUARD) {
        bool all_at_target = true;
        for (unsigned i = 0; i < LOCAL_LINK_MAX_PLAYERS; ++i) {
            if (consoles[i].loaded &&
                consoles[i].core->frameCounter(consoles[i].core) < targets[i]) {
                all_at_target = false;
                break;
            }
        }

        bool link_idle = !coordinator_initialized ||
                         (!coordinator.waiting && !coordinator.transferActive);
        if (all_at_target && link_idle) return true;

        bool ran = false;
        for (unsigned i = 0; i < LOCAL_LINK_MAX_PLAYERS; ++i) {
            if (!consoles[i].loaded || consoles[i].asleep) continue;
            consoles[i].core->runLoop(consoles[i].core);
            ran = true;
            if (++steps >= RUN_GUARD) break;
        }
        if (!ran) return false;
    }
    return false;
}

static void drain_audio(unsigned slot) {
    static int16_t samples[AUDIO_OUTPUT_FRAMES * 2];

    for (unsigned i = 0; i < LOCAL_LINK_MAX_PLAYERS; ++i) {
        struct LocalConsole *console = &consoles[i];
        if (!console->loaded) continue;

        struct mAudioBuffer *buffer = console->core->getAudioBuffer(console->core);
        if (i == slot) {
            unsigned source_rate = console->core->audioSampleRate(console->core);
            if (source_rate != audio_output_rate) {
                if (source_rate != console->resampler_source_rate) {
                    mAudioResamplerSetSource(&console->resampler, buffer, source_rate, true);
                    console->resampler_source_rate = source_rate;
                }
                mAudioResamplerProcess(&console->resampler);
                buffer = &console->resample_buffer;
            }
        }

        size_t available;
        while ((available = mAudioBufferAvailable(buffer)) != 0) {
            if (available > AUDIO_OUTPUT_FRAMES) available = AUDIO_OUTPUT_FRAMES;
            size_t produced = mAudioBufferRead(buffer, samples, available);
            if (i == slot && audio_batch_cb && produced) {
                audio_batch_cb(samples, produced);
            }
        }
    }
}

static size_t save_state(unsigned slot, void *output, size_t capacity) {
    if (slot >= LOCAL_LINK_MAX_PLAYERS || !consoles[slot].loaded) return 0;
    struct VFile *vf = VFileMemChunk(NULL, 0);
    if (!vf || !mCoreSaveStateNamed(consoles[slot].core, vf,
                                    SAVESTATE_SAVEDATA | SAVESTATE_RTC)) {
        if (vf) vf->close(vf);
        return 0;
    }
    size_t size = (size_t) vf->size(vf);
    if (output) {
        if (size > capacity || vf->seek(vf, 0, SEEK_SET) < 0 ||
            vf->read(vf, output, size) != (ssize_t) size) {
            vf->close(vf);
            return 0;
        }
    }
    vf->close(vf);
    return size;
}

static bool load_state(unsigned slot, const void *data, size_t size) {
    if (slot >= LOCAL_LINK_MAX_PLAYERS || !consoles[slot].loaded || !data || !size) return false;
    struct VFile *vf = VFileFromConstMemory(data, size);
    if (!vf) return false;
    bool ok = mCoreLoadStateNamed(consoles[slot].core, vf,
                                  SAVESTATE_SAVEDATA | SAVESTATE_RTC);
    vf->close(vf);
    return ok;
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_environment(retro_environment_t cb) { environ_cb = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void) cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void) {
    enum retro_pixel_format format = RETRO_PIXEL_FORMAT_RGB565;
    audio_output_rate = AUDIO_DEFAULT_RATE;
    if (environ_cb) {
        environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
        input_bitmasks = environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);
        unsigned requested_rate = audio_output_rate;
        if (environ_cb(RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE, &requested_rate) && requested_rate) {
            audio_output_rate = requested_rate;
        }
    }
    for (unsigned i = 0; i < LOCAL_LINK_MAX_PLAYERS; ++i) {
        consoles[i].slot = i;
        consoles[i].cable_id = -1;
    }
}

void retro_deinit(void) { unload_all(); }

void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name = "mGBA Local Link";
    info->library_version = "0.1";
    info->valid_extensions = "gba";
    info->need_fullpath = false;
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof(*info));
    info->geometry.base_width = 240;
    info->geometry.base_height = 160;
    info->geometry.max_width = VIDEO_STRIDE;
    info->geometry.max_height = VIDEO_HEIGHT;
    info->geometry.aspect_ratio = 3.0f / 2.0f;
    info->timing.fps = consoles[active_slot].loaded
        ? consoles[active_slot].core->frequency(consoles[active_slot].core) /
          (double) consoles[active_slot].core->frameCycles(consoles[active_slot].core)
        : 59.7275;
    info->timing.sample_rate = audio_output_rate;
}

bool retro_load_game(const struct retro_game_info *game) {
    unload_all();
    if (!console_load(0, game)) return false;
    active_slot = 0;
    return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t count) {
    (void) type;
    (void) info;
    (void) count;
    return false;
}

void retro_unload_game(void) { unload_all(); }

void retro_run(void) {
    uint32_t mask = loaded_mask();
    if (!mask) return;
    if (!(mask & (1u << active_slot))) active_slot = next_loaded(active_slot);

    if (input_poll_cb) input_poll_cb();
    uint32_t keys = read_local_keys();
    for (unsigned i = 0; i < LOCAL_LINK_MAX_PLAYERS; ++i) {
        if (consoles[i].loaded) {
            consoles[i].core->setKeys(consoles[i].core, i == active_slot ? keys : 0);
        }
    }

    bool ok = run_linked_frame();
    unsigned width = 240, height = 160;
    consoles[active_slot].core->currentVideoSize(consoles[active_slot].core, &width, &height);
    if (video_cb) {
        video_cb(ok ? consoles[active_slot].video : NULL,
                 width, height, VIDEO_STRIDE * sizeof(mColor));
    }
    drain_audio(active_slot);
}

void retro_reset(void) {
    if (active_slot < LOCAL_LINK_MAX_PLAYERS && consoles[active_slot].loaded) {
        consoles[active_slot].core->reset(consoles[active_slot].core);
    }
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void) port;
    (void) device;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    (void) index; (void) enabled; (void) code;
}
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

void *retro_local_link_get_memory_data(unsigned slot, unsigned id) {
    if (slot >= LOCAL_LINK_MAX_PLAYERS || !consoles[slot].loaded) return NULL;
    struct GBA *gba = consoles[slot].core->board;
    switch (id) {
        case RETRO_MEMORY_SAVE_RAM: return consoles[slot].savedata;
        case RETRO_MEMORY_SYSTEM_RAM: return gba->memory.wram;
        case RETRO_MEMORY_VIDEO_RAM: return gba->video.vram;
        default: return NULL;
    }
}

size_t retro_local_link_get_memory_size(unsigned slot, unsigned id) {
    if (slot >= LOCAL_LINK_MAX_PLAYERS || !consoles[slot].loaded) return 0;
    struct GBA *gba = consoles[slot].core->board;
    switch (id) {
        case RETRO_MEMORY_SAVE_RAM:
            return gba->memory.savedata.type == GBA_SAVEDATA_AUTODETECT
                ? GBA_SIZE_FLASH1M : GBASavedataSize(&gba->memory.savedata);
        case RETRO_MEMORY_SYSTEM_RAM: return GBA_SIZE_EWRAM;
        case RETRO_MEMORY_VIDEO_RAM: return GBA_SIZE_VRAM;
        default: return 0;
    }
}

void *retro_get_memory_data(unsigned id) {
    return retro_local_link_get_memory_data(active_slot, id);
}

size_t retro_get_memory_size(unsigned id) {
    return retro_local_link_get_memory_size(active_slot, id);
}

size_t retro_serialize_size(void) { return save_state(active_slot, NULL, 0); }
bool retro_serialize(void *data, size_t size) {
    size_t expected = retro_serialize_size();
    return expected && size >= expected && save_state(active_slot, data, size) == expected;
}
bool retro_unserialize(const void *data, size_t size) {
    return load_state(active_slot, data, size);
}

unsigned retro_local_link_get_abi_version(void) { return LOCAL_LINK_ABI_VERSION; }

uint64_t retro_local_link_get_capabilities(void) {
    return LOCAL_LINK_CAP_ADD_REMOVE |
           LOCAL_LINK_CAP_ACTIVE_INSTANCE |
           LOCAL_LINK_CAP_PER_PLAYER_RAM |
           LOCAL_LINK_CAP_PER_PLAYER_STATE |
           LOCAL_LINK_CAP_FIXED_PLAYER_ID;
}

uint32_t retro_local_link_get_loaded_mask(void) { return loaded_mask(); }
unsigned retro_local_link_get_active_instance(void) { return active_slot; }

bool retro_local_link_set_active_instance(unsigned slot) {
    if (slot >= LOCAL_LINK_MAX_PLAYERS || !consoles[slot].loaded) return false;
    active_slot = slot;
    return true;
}

bool retro_local_link_add_instance(unsigned slot, const struct retro_game_info *game) {
    if (slot >= LOCAL_LINK_MAX_PLAYERS || consoles[slot].loaded || !game) return false;
    if (!console_load(slot, game)) return false;
    if (!rebuild_link_bus()) {
        console_clear(&consoles[slot]);
        rebuild_link_bus();
        return false;
    }
    return true;
}

bool retro_local_link_remove_instance(unsigned slot) {
    if (slot >= LOCAL_LINK_MAX_PLAYERS || !consoles[slot].loaded) return false;

    bool was_active = slot == active_slot;
    destroy_link_bus();
    console_clear(&consoles[slot]);
    if (!rebuild_link_bus()) return false;

    if (was_active && loaded_mask()) active_slot = next_loaded(slot);
    return loaded_mask() != 0;
}

size_t retro_local_link_serialize_size(unsigned slot) {
    return save_state(slot, NULL, 0);
}

bool retro_local_link_serialize(unsigned slot, void *data, size_t size) {
    size_t expected = save_state(slot, NULL, 0);
    return expected && size >= expected && save_state(slot, data, size) == expected;
}

bool retro_local_link_unserialize(unsigned slot, const void *data, size_t size) {
    return load_state(slot, data, size);
}
