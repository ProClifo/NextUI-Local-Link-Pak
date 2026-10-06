#include "session.h"

#include <stdio.h>
#include <string.h>

static LLSlot *slot_mut(LLSession *session, unsigned player_id) {
    if (!session || player_id < 1 || player_id > LL_MAX_PLAYERS) return NULL;
    return &session->slots[player_id - 1];
}

static const LLSlot *slot_const(const LLSession *session, unsigned player_id) {
    if (!session || player_id < 1 || player_id > LL_MAX_PLAYERS) return NULL;
    return &session->slots[player_id - 1];
}

static unsigned next_occupied_after(const LLSession *session, unsigned player_id) {
    if (!session || ll_session_count(session) == 0) return 0;

    for (unsigned offset = 1; offset <= LL_MAX_PLAYERS; ++offset) {
        unsigned candidate = ((player_id - 1 + offset) % LL_MAX_PLAYERS) + 1;
        if (ll_session_has_player(session, candidate)) return candidate;
    }
    return 0;
}

void ll_session_init(LLSession *session, const LLBackend *backend, void *backend_userdata) {
    if (!session) return;

    memset(session, 0, sizeof(*session));
    for (unsigned i = 0; i < LL_MAX_PLAYERS; ++i) {
        session->slots[i].player_id = i + 1;
    }
    if (backend) session->backend = *backend;
    session->backend_userdata = backend_userdata;
}

size_t ll_session_count(const LLSession *session) {
    if (!session) return 0;
    size_t count = 0;
    for (unsigned i = 0; i < LL_MAX_PLAYERS; ++i) {
        if (session->slots[i].occupied) ++count;
    }
    return count;
}

bool ll_session_has_player(const LLSession *session, unsigned player_id) {
    const LLSlot *slot = slot_const(session, player_id);
    return slot && slot->occupied;
}

unsigned ll_session_active_player(const LLSession *session) {
    return session ? session->active_player_id : 0;
}

unsigned ll_session_first_free_player(const LLSession *session) {
    if (!session) return 0;
    for (unsigned i = 0; i < LL_MAX_PLAYERS; ++i) {
        if (!session->slots[i].occupied) return i + 1;
    }
    return 0;
}

LLResult ll_session_add(LLSession *session, unsigned player_id, const char *rom_path) {
    if (!session || !rom_path || !rom_path[0]) return LL_ERR_INVALID_ARGUMENT;

    LLSlot *slot = slot_mut(session, player_id);
    if (!slot) return LL_ERR_INVALID_ARGUMENT;
    if (slot->occupied) return LL_ERR_SLOT_OCCUPIED;

    if (session->backend.load_instance) {
        LLResult result = session->backend.load_instance(player_id, rom_path, session->backend_userdata);
        if (result != LL_OK) return LL_ERR_BACKEND;
    }

    slot->occupied = true;
    snprintf(slot->rom_path, sizeof(slot->rom_path), "%s", rom_path);

    if (session->active_player_id == 0) {
        session->active_player_id = player_id;
        if (session->backend.set_active_instance) {
            LLResult result = session->backend.set_active_instance(player_id, session->backend_userdata);
            if (result != LL_OK) {
                if (session->backend.close_instance) {
                    session->backend.close_instance(player_id, session->backend_userdata);
                }
                slot->occupied = false;
                slot->rom_path[0] = '\0';
                session->active_player_id = 0;
                return LL_ERR_BACKEND;
            }
        }
    }

    return LL_OK;
}

LLResult ll_session_add_next(LLSession *session, const char *rom_path, unsigned *player_id_out) {
    unsigned player_id = ll_session_first_free_player(session);
    if (!player_id) return LL_ERR_NO_FREE_SLOT;

    LLResult result = ll_session_add(session, player_id, rom_path);
    if (result == LL_OK && player_id_out) *player_id_out = player_id;
    return result;
}

LLResult ll_session_switch(LLSession *session, unsigned player_id) {
    if (!session) return LL_ERR_INVALID_ARGUMENT;
    if (!ll_session_has_player(session, player_id)) return LL_ERR_SLOT_EMPTY;

    if (session->backend.set_active_instance) {
        LLResult result = session->backend.set_active_instance(player_id, session->backend_userdata);
        if (result != LL_OK) return LL_ERR_BACKEND;
    }

    session->active_player_id = player_id;
    return LL_OK;
}

LLResult ll_session_switch_next(LLSession *session) {
    if (!session) return LL_ERR_INVALID_ARGUMENT;
    if (ll_session_count(session) == 0) return LL_ERR_SESSION_EMPTY;

    unsigned start = session->active_player_id ? session->active_player_id : 1;
    unsigned next = next_occupied_after(session, start);
    if (!next) return LL_ERR_SESSION_EMPTY;
    return ll_session_switch(session, next);
}

LLResult ll_session_remove(LLSession *session, unsigned player_id, bool save_first) {
    if (!session) return LL_ERR_INVALID_ARGUMENT;

    LLSlot *slot = slot_mut(session, player_id);
    if (!slot || !slot->occupied) return LL_ERR_SLOT_EMPTY;

    if (save_first && session->backend.save_instance) {
        LLResult result = session->backend.save_instance(player_id, session->backend_userdata);
        if (result != LL_OK) return LL_ERR_BACKEND;
    }

    if (session->backend.close_instance) {
        LLResult result = session->backend.close_instance(player_id, session->backend_userdata);
        if (result != LL_OK) return LL_ERR_BACKEND;
    }

    bool was_active = session->active_player_id == player_id;
    slot->occupied = false;
    slot->rom_path[0] = '\0';

    if (ll_session_count(session) == 0) {
        session->active_player_id = 0;
        return LL_OK;
    }

    if (was_active) {
        unsigned next = next_occupied_after(session, player_id);
        if (!next) return LL_ERR_SESSION_EMPTY;
        return ll_session_switch(session, next);
    }

    return LL_OK;
}

LLResult ll_session_quit_active(LLSession *session, bool save_first) {
    if (!session) return LL_ERR_INVALID_ARGUMENT;
    if (session->active_player_id == 0) return LL_ERR_SESSION_EMPTY;
    return ll_session_remove(session, session->active_player_id, save_first);
}

const LLSlot *ll_session_get_slot(const LLSession *session, unsigned player_id) {
    return slot_const(session, player_id);
}

const char *ll_result_string(LLResult result) {
    switch (result) {
        case LL_OK: return "ok";
        case LL_ERR_INVALID_ARGUMENT: return "invalid argument";
        case LL_ERR_SLOT_OCCUPIED: return "player slot already occupied";
        case LL_ERR_SLOT_EMPTY: return "player slot is empty";
        case LL_ERR_NO_FREE_SLOT: return "no free player slot";
        case LL_ERR_BACKEND: return "emulator backend error";
        case LL_ERR_SESSION_EMPTY: return "link session is empty";
        default: return "unknown error";
    }
}
