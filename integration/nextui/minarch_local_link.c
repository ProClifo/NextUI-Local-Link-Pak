#include "ma_internal.h"
#include "minarch_local_link.h"
#include "local_link_abi.h"

#include <string.h>

static int abi_valid(void) {
    return core.local_link_get_abi_version &&
           core.local_link_get_capabilities &&
           core.local_link_get_loaded_mask &&
           core.local_link_get_active_instance &&
           core.local_link_set_active_instance &&
           core.local_link_add_instance &&
           core.local_link_remove_instance &&
           core.local_link_get_abi_version() == LOCAL_LINK_ABI_VERSION;
}

int LLMinarch_supported(void) {
    if (!abi_valid()) return 0;
    uint64_t caps = core.local_link_get_capabilities();
    uint64_t required = LOCAL_LINK_CAP_ADD_REMOVE |
                        LOCAL_LINK_CAP_ACTIVE_INSTANCE |
                        LOCAL_LINK_CAP_FIXED_PLAYER_ID;
    return (caps & required) == required;
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

int LLMinarch_addPath(unsigned slot, const char *path) {
    if (!LLMinarch_supported() || !path || !*path || slot >= LOCAL_LINK_MAX_PLAYERS) return 0;
    if (LLMinarch_loadedMask() & (1u << slot)) return 0;

    struct retro_game_info game_info;
    memset(&game_info, 0, sizeof(game_info));
    game_info.path = path;
    return core.local_link_add_instance(slot, &game_info) ? 1 : 0;
}

int LLMinarch_addNextPath(const char *path, unsigned *slot_out) {
    uint32_t mask = LLMinarch_loadedMask();
    /* Slot 0 is the game MinArch launched normally. Fill P2-P4 in order. */
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
    return core.local_link_remove_instance(slot) ? 1 : 0;
}

int LLMinarch_removeActive(void) {
    return LLMinarch_remove(LLMinarch_activeSlot());
}
