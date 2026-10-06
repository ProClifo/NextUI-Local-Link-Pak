#ifndef NEXTUI_LOCAL_LINK_SESSION_H
#define NEXTUI_LOCAL_LINK_SESSION_H

#include <stdbool.h>
#include <stddef.h>

#define LL_MAX_PLAYERS 4
#define LL_PATH_MAX 1024

typedef enum {
    LL_OK = 0,
    LL_ERR_INVALID_ARGUMENT,
    LL_ERR_SLOT_OCCUPIED,
    LL_ERR_SLOT_EMPTY,
    LL_ERR_NO_FREE_SLOT,
    LL_ERR_BACKEND,
    LL_ERR_SESSION_EMPTY
} LLResult;

typedef struct {
    unsigned player_id;
    bool occupied;
    char rom_path[LL_PATH_MAX];
} LLSlot;

typedef struct {
    LLResult (*load_instance)(unsigned player_id, const char *rom_path, void *userdata);
    LLResult (*save_instance)(unsigned player_id, void *userdata);
    LLResult (*close_instance)(unsigned player_id, void *userdata);
    LLResult (*set_active_instance)(unsigned player_id, void *userdata);
} LLBackend;

typedef struct {
    LLSlot slots[LL_MAX_PLAYERS];
    unsigned active_player_id;
    LLBackend backend;
    void *backend_userdata;
} LLSession;

void ll_session_init(LLSession *session, const LLBackend *backend, void *backend_userdata);
size_t ll_session_count(const LLSession *session);
bool ll_session_has_player(const LLSession *session, unsigned player_id);
unsigned ll_session_active_player(const LLSession *session);
unsigned ll_session_first_free_player(const LLSession *session);
LLResult ll_session_add(LLSession *session, unsigned player_id, const char *rom_path);
LLResult ll_session_add_next(LLSession *session, const char *rom_path, unsigned *player_id_out);
LLResult ll_session_switch(LLSession *session, unsigned player_id);
LLResult ll_session_switch_next(LLSession *session);
LLResult ll_session_remove(LLSession *session, unsigned player_id, bool save_first);
LLResult ll_session_quit_active(LLSession *session, bool save_first);
const LLSlot *ll_session_get_slot(const LLSession *session, unsigned player_id);
const char *ll_result_string(LLResult result);

#endif
