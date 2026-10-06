#include "ma_internal.h"
#include "ma_frontend_opts.h"
#include "ma_saves.h"
#include "minarch_local_link.h"
#include "local_link_abi.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* The basename/alt-name used by NextUI's normal SRAM/state naming rules for
 * each fixed Local Link slot. P1 is populated from game.alt_name; P2-P4 are
 * the selected ROM basenames. */
static char slot_save_name[LOCAL_LINK_MAX_PLAYERS][MAX_PATH];

static void remember_primary_name(void) {
    if (!slot_save_name[0][0] && game.alt_name[0]) {
        snprintf(slot_save_name[0], sizeof(slot_save_name[0]), "%s", game.alt_name);
    }
}

static void remember_path_name(unsigned slot, const char *path) {
    if (slot >= LOCAL_LINK_MAX_PLAYERS || !path) return;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    snprintf(slot_save_name[slot], sizeof(slot_save_name[slot]), "%s", name);
}

static int abi_valid(void) {
    return core.local_link_get_abi_version &&
           core.local_link_get_capabilities &&
           core.local_link_get_loaded_mask &&
           core.local_link_get_active_instance &&
           core.local_link_set_active_instance &&
           core.local_link_add_instance &&
           core.local_link_remove_instance &&
           core.local_link_get_memory_data &&
           core.local_link_get_memory_size &&
           core.local_link_get_abi_version() == LOCAL_LINK_ABI_VERSION;
}

static int state_abi_valid(void) {
    if (!abi_valid()) return 0;
    if (!(core.local_link_get_capabilities() & LOCAL_LINK_CAP_PER_PLAYER_STATE)) return 0;
    return core.local_link_serialize_size &&
           core.local_link_serialize &&
           core.local_link_unserialize;
}

int LLMinarch_supported(void) {
    if (!abi_valid()) return 0;
    uint64_t caps = core.local_link_get_capabilities();
    uint64_t required = LOCAL_LINK_CAP_ADD_REMOVE |
                        LOCAL_LINK_CAP_ACTIVE_INSTANCE |
                        LOCAL_LINK_CAP_PER_PLAYER_RAM |
                        LOCAL_LINK_CAP_FIXED_PLAYER_ID;
    if ((caps & required) != required) return 0;
    remember_primary_name();
    return 1;
}

uint32_t LLMinarch_loadedMask(void) {
    return LLMinarch_supported() ?
        (core.local_link_get_loaded_mask() & ((1u << LOCAL_LINK_MAX_PLAYERS) - 1u)) : 0;
}

unsigned LLMinarch_activeSlot(void) {
    if (!LLMinarch_supported()) return 0;
    unsigned slot = core.local_link_get_active_instance();
    return slot < LOCAL_LINK_MAX_PLAYERS ? slot : 0;
}

unsigned LLMinarch_instanceCount(void) {
    uint32_t mask = LLMinarch_loadedMask();
    unsigned count = 0;
    while (mask) {
        count += mask & 1u;
        mask >>= 1;
    }
    return count;
}

int LLMinarch_saveSlot(unsigned slot) {
    if (!LLMinarch_supported() || slot >= LOCAL_LINK_MAX_PLAYERS) return 0;
    if (!(LLMinarch_loadedMask() & (1u << slot))) return 0;

    if (slot == 0) remember_primary_name();
    if (!slot_save_name[slot][0]) return 0;

    void *sram = core.local_link_get_memory_data(slot, RETRO_MEMORY_SAVE_RAM);
    size_t size = core.local_link_get_memory_size(slot, RETRO_MEMORY_SAVE_RAM);
    if (!sram || !size) return 1;

    return SRAM_writeNamed(slot_save_name[slot], sram, size);
}

int LLMinarch_saveAll(void) {
    if (!LLMinarch_supported()) return 0;
    uint32_t mask = LLMinarch_loadedMask();
    int ok = 1;
    for (unsigned slot = 0; slot < LOCAL_LINK_MAX_PLAYERS; ++slot) {
        if ((mask & (1u << slot)) && !LLMinarch_saveSlot(slot)) ok = 0;
    }
    return ok;
}

int LLMinarch_saveStateSlot(unsigned slot, int state_slot) {
    if (!state_abi_valid() || slot >= LOCAL_LINK_MAX_PLAYERS) return 0;
    if (!(LLMinarch_loadedMask() & (1u << slot))) return 0;
    if (slot == 0) remember_primary_name();
    if (!slot_save_name[slot][0]) return 0;

    size_t size = core.local_link_serialize_size(slot);
    if (!size) return 0;
    void *state = malloc(size);
    if (!state) return 0;

    int ok = core.local_link_serialize(slot, state, size) &&
             State_writeNamed(slot_save_name[slot], state_slot, state, size);
    free(state);
    return ok;
}

int LLMinarch_loadStateSlot(unsigned slot, int state_slot) {
    if (!state_abi_valid() || slot >= LOCAL_LINK_MAX_PLAYERS) return 0;
    if (!(LLMinarch_loadedMask() & (1u << slot))) return 0;
    if (slot == 0) remember_primary_name();
    if (!slot_save_name[slot][0]) return 0;

    size_t size = core.local_link_serialize_size(slot);
    if (!size) return 0;
    void *state = calloc(1, size);
    if (!state) return 0;

    int ok = State_readNamed(slot_save_name[slot], state_slot, state, size) &&
             core.local_link_unserialize(slot, state, size);
    free(state);
    return ok;
}

int LLMinarch_addPath(unsigned slot, const char *path) {
    if (!LLMinarch_supported() || !path || !*path || slot >= LOCAL_LINK_MAX_PLAYERS) return 0;
    if (LLMinarch_loadedMask() & (1u << slot)) return 0;

    struct retro_game_info game_info;
    memset(&game_info, 0, sizeof(game_info));
    game_info.path = path;

    if (!core.local_link_add_instance(slot, &game_info)) return 0;

    remember_path_name(slot, path);
    void *sram = core.local_link_get_memory_data(slot, RETRO_MEMORY_SAVE_RAM);
    size_t size = core.local_link_get_memory_size(slot, RETRO_MEMORY_SAVE_RAM);
    if (sram && size) {
        /* Missing files are normal for a new game, so SRAM_readNamed returning
         * false is not an add-instance failure. The core's RAM remains 0xFF. */
        SRAM_readNamed(slot_save_name[slot], sram, size);
    }
    return 1;
}

int LLMinarch_addNextPath(const char *path, unsigned *slot_out) {
    uint32_t mask = LLMinarch_loadedMask();
    for (unsigned slot = 1; slot < LOCAL_LINK_MAX_PLAYERS; ++slot) {
        if (!(mask & (1u << slot))) {
            if (!LLMinarch_addPath(slot, path)) return 0;
            if (slot_out) *slot_out = slot;
            return 1;
        }
    }
    return 0;
}

int LLMinarch_switchTo(unsigned slot) {
    if (!LLMinarch_supported() || slot >= LOCAL_LINK_MAX_PLAYERS) return 0;
    if (!(LLMinarch_loadedMask() & (1u << slot))) return 0;
    return core.local_link_set_active_instance(slot) ? 1 : 0;
}

int LLMinarch_switchNext(void) {
    uint32_t mask = LLMinarch_loadedMask();
    if (!mask) return 0;

    unsigned active = LLMinarch_activeSlot();
    for (unsigned offset = 1; offset <= LOCAL_LINK_MAX_PLAYERS; ++offset) {
        unsigned candidate = (active + offset) % LOCAL_LINK_MAX_PLAYERS;
        if (mask & (1u << candidate)) return LLMinarch_switchTo(candidate);
    }
    return 0;
}

int LLMinarch_remove(unsigned slot) {
    if (!LLMinarch_supported() || slot >= LOCAL_LINK_MAX_PLAYERS) return 0;
    if (!(LLMinarch_loadedMask() & (1u << slot))) return 0;

    if (!LLMinarch_saveSlot(slot)) return 0;
    if (!core.local_link_remove_instance(slot)) return 0;

    slot_save_name[slot][0] = '\0';
    return 1;
}

int LLMinarch_removeActive(void) {
    return LLMinarch_remove(LLMinarch_activeSlot());
}

int LLMinarch_saveAndRemoveActive(void) {
    if (!LLMinarch_supported()) return 0;
    unsigned slot = LLMinarch_activeSlot();
    if (!(LLMinarch_loadedMask() & (1u << slot))) return 0;

    if (!LLMinarch_saveSlot(slot)) return 0;
    if (!LLMinarch_saveStateSlot(slot, AUTO_RESUME_SLOT)) return 0;
    if (!core.local_link_remove_instance(slot)) return 0;
    slot_save_name[slot][0] = '\0';
    return 1;
}

struct PickerEntry {
    char *name;
    char *path;
};

static struct PickerEntry *picker_entries;
static size_t picker_count;

static int picker_compare(const void *a, const void *b) {
    const struct PickerEntry *ea = a;
    const struct PickerEntry *eb = b;
    return strcasecmp(ea->name, eb->name);
}

static int is_gba_rom(const char *name) {
    const char *ext = strrchr(name, '.');
    return ext && strcasecmp(ext, ".gba") == 0;
}

static char *display_name(const char *filename) {
    char *name = strdup(filename);
    if (!name) return NULL;
    char *ext = strrchr(name, '.');
    if (ext) *ext = '\0';
    return name;
}

static void picker_clear(void) {
    for (size_t i = 0; i < picker_count; ++i) {
        free(picker_entries[i].name);
        free(picker_entries[i].path);
    }
    free(picker_entries);
    picker_entries = NULL;
    picker_count = 0;
}

static int picker_add(const char *directory, const char *filename) {
    struct PickerEntry *next = realloc(picker_entries,
        (picker_count + 1) * sizeof(*picker_entries));
    if (!next) return 0;
    picker_entries = next;

    struct PickerEntry *entry = &picker_entries[picker_count];
    memset(entry, 0, sizeof(*entry));
    entry->name = display_name(filename);
    if (!entry->name) return 0;

    size_t len = strlen(directory) + 1 + strlen(filename) + 1;
    entry->path = malloc(len);
    if (!entry->path) {
        free(entry->name);
        entry->name = NULL;
        return 0;
    }
    snprintf(entry->path, len, "%s/%s", directory, filename);
    ++picker_count;
    return 1;
}

/* For now the ROM list uses A to choose the game and then asks how to start
 * the new physical GBA. This keeps the semantics identical to NextUI's normal
 * A=start / X=resume behavior without modifying the generic MenuList API. */
static int picker_start_mode(const char *name) {
    GFX_setMode(MODE_MAIN);
    int dirty = 1;
    for (;;) {
        GFX_startFrame();
        PAD_poll();
        if (PAD_justPressed(BTN_A)) { GFX_setMode(MODE_MENU); return 0; }
        if (PAD_justPressed(BTN_X)) { GFX_setMode(MODE_MENU); return 1; }
        if (PAD_justPressed(BTN_B)) { GFX_setMode(MODE_MENU); return -1; }

        PWR_update(&dirty, NULL, Menu_beforeSleep, Menu_afterSleep);
        GFX_clear(screen);
        char message[MAX_PATH + 64];
        snprintf(message, sizeof(message), "%s\n\nStart this linked GBA from its battery save, or resume its auto save state?", name);
        GFX_blitMessage(font.medium, message, screen,
            &(SDL_Rect){SCALE1(PADDING), SCALE1(PADDING),
                        screen->w - SCALE1(2 * PADDING),
                        screen->h - SCALE1(PILL_SIZE + PADDING)});
        GFX_blitButtonGroup((char*[]){ "A", "START", "X", "RESUME", "B", "BACK", NULL },
                            0, screen, 1);
        GFX_flip(screen);
        dirty = 0;
        hdmimon();
    }
}

static int picker_confirm(MenuList *list, int index) {
    (void) list;
    if (index < 0 || (size_t) index >= picker_count) return MENU_CALLBACK_NOP;

    int resume = picker_start_mode(picker_entries[index].name);
    if (resume < 0) return MENU_CALLBACK_NOP;

    unsigned slot = 0;
    if (!LLMinarch_addNextPath(picker_entries[index].path, &slot)) {
        Menu_message("Could not add this GBA instance.", (char*[]){ "B", "BACK", NULL });
        return MENU_CALLBACK_NOP;
    }

    if (resume && !LLMinarch_loadStateSlot(slot, AUTO_RESUME_SLOT)) {
        /* Do not silently fall back to battery save when the user explicitly
         * chose X/Resume. Remove the just-created instance and let them retry. */
        LLMinarch_remove(slot);
        Menu_message("No usable resume state was found for this game.",
                     (char*[]){ "B", "BACK", NULL });
        return MENU_CALLBACK_NOP;
    }

    char message[64];
    snprintf(message, sizeof(message), "Added as Player %u.", slot + 1);
    Menu_message(message, (char*[]){ "A", "OK", NULL });
    return MENU_CALLBACK_EXIT;
}

int LLMinarch_menuAdd(MenuList *parent, int index) {
    (void) parent;
    (void) index;

    if (!LLMinarch_supported()) return MENU_CALLBACK_NOP;
    if (LLMinarch_instanceCount() >= LOCAL_LINK_MAX_PLAYERS) {
        Menu_message("All four GBA link slots are in use.", (char*[]){ "B", "BACK", NULL });
        return MENU_CALLBACK_NOP;
    }

    char directory[MAX_PATH];
    snprintf(directory, sizeof(directory), "%s", game.path);
    char *slash = strrchr(directory, '/');
    if (!slash) {
        Menu_message("Could not find the GBA ROM folder.", (char*[]){ "B", "BACK", NULL });
        return MENU_CALLBACK_NOP;
    }
    *slash = '\0';

    picker_clear();
    DIR *dir = opendir(directory);
    if (!dir) {
        Menu_message("Could not open the GBA ROM folder.", (char*[]){ "B", "BACK", NULL });
        return MENU_CALLBACK_NOP;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.' || !is_gba_rom(ent->d_name)) continue;
        if (!picker_add(directory, ent->d_name)) break;
    }
    closedir(dir);

    if (!picker_count) {
        picker_clear();
        Menu_message("No .gba files found in this ROM folder.", (char*[]){ "B", "BACK", NULL });
        return MENU_CALLBACK_NOP;
    }

    qsort(picker_entries, picker_count, sizeof(*picker_entries), picker_compare);

    MenuItem *items = calloc(picker_count + 1, sizeof(*items));
    if (!items) {
        picker_clear();
        return MENU_CALLBACK_NOP;
    }
    for (size_t i = 0; i < picker_count; ++i) {
        items[i].name = picker_entries[i].name;
        items[i].desc = "Add this game to the local GBA link session.";
        items[i].on_confirm = picker_confirm;
    }

    MenuList picker = {
        .type = MENU_LIST,
        .desc = "Choose a GBA game to link.",
        .items = items,
    };
    Menu_options(&picker);

    free(items);
    picker_clear();
    return MENU_CALLBACK_NOP;
}

int LLMinarch_menuSwitch(MenuList *list, int index) {
    (void) list;
    (void) index;
    if (LLMinarch_instanceCount() < 2) return MENU_CALLBACK_NOP;
    return LLMinarch_switchNext() ? MENU_CALLBACK_EXIT : MENU_CALLBACK_NOP;
}

int LLMinarch_menuRemove(MenuList *list, int index) {
    (void) list;
    (void) index;
    if (LLMinarch_instanceCount() < 2) return MENU_CALLBACK_NOP;

    unsigned player = LLMinarch_activeSlot() + 1;
    if (!LLMinarch_removeActive()) {
        Menu_message("Could not save/remove the active instance.", (char*[]){ "B", "BACK", NULL });
        return MENU_CALLBACK_NOP;
    }

    char message[64];
    snprintf(message, sizeof(message), "Player %u disconnected.", player);
    Menu_message(message, (char*[]){ "A", "OK", NULL });
    return MENU_CALLBACK_EXIT;
}
