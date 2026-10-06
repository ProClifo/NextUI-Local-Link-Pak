#ifndef NEXTUI_LOCAL_LINK_ABI_H
#define NEXTUI_LOCAL_LINK_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOCAL_LINK_ABI_VERSION 1u
#define LOCAL_LINK_MAX_PLAYERS 4u

/* Optional libretro extension implemented by the Local Link mGBA core.
 * Slots are zero-based here (0..3). A slot is a stable Local Link identity used
 * for ROM/save/state ownership and presented as Player 1..Player 4 in the UI.
 *
 * Important: a stable slot is not automatically the same thing as a sticky GBA
 * multiplayer cable ID after another powered-on console disappears. Stock mGBA
 * lockstep compacts its transport player IDs. Sticky cable IDs are therefore a
 * separate capability and must not be inferred from FIXED_SLOT_ID. */
enum LocalLinkCapability {
    LOCAL_LINK_CAP_ADD_REMOVE        = 1u << 0,
    LOCAL_LINK_CAP_ACTIVE_INSTANCE   = 1u << 1,
    LOCAL_LINK_CAP_PER_PLAYER_RAM    = 1u << 2,
    LOCAL_LINK_CAP_PER_PLAYER_STATE  = 1u << 3,
    LOCAL_LINK_CAP_FIXED_SLOT_ID     = 1u << 4,
    LOCAL_LINK_CAP_STICKY_CABLE_ID   = 1u << 5,
};

typedef unsigned (*local_link_get_abi_version_t)(void);
typedef uint64_t (*local_link_get_capabilities_t)(void);
typedef uint32_t (*local_link_get_loaded_mask_t)(void);
typedef unsigned (*local_link_get_active_instance_t)(void);
typedef bool (*local_link_set_active_instance_t)(unsigned slot);
typedef bool (*local_link_add_instance_t)(unsigned slot, const struct retro_game_info *game);
typedef bool (*local_link_remove_instance_t)(unsigned slot);
typedef void *(*local_link_get_memory_data_t)(unsigned slot, unsigned id);
typedef size_t (*local_link_get_memory_size_t)(unsigned slot, unsigned id);
typedef size_t (*local_link_serialize_size_t)(unsigned slot);
typedef bool (*local_link_serialize_t)(unsigned slot, void *data, size_t size);
typedef bool (*local_link_unserialize_t)(unsigned slot, const void *data, size_t size);

/* Exported symbol names. MinArch discovers these with dlsym(), so a normal
 * libretro core remains completely unaffected. */
unsigned retro_local_link_get_abi_version(void);
uint64_t retro_local_link_get_capabilities(void);
uint32_t retro_local_link_get_loaded_mask(void);
unsigned retro_local_link_get_active_instance(void);
bool retro_local_link_set_active_instance(unsigned slot);
bool retro_local_link_add_instance(unsigned slot, const struct retro_game_info *game);
bool retro_local_link_remove_instance(unsigned slot);
void *retro_local_link_get_memory_data(unsigned slot, unsigned id);
size_t retro_local_link_get_memory_size(unsigned slot, unsigned id);
size_t retro_local_link_serialize_size(unsigned slot);
bool retro_local_link_serialize(unsigned slot, void *data, size_t size);
bool retro_local_link_unserialize(unsigned slot, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
