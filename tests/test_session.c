#include "session.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct FakeBackend {
    bool loaded[LL_MAX_PLAYERS];
    unsigned active;
    unsigned saves[LL_MAX_PLAYERS];
    unsigned closes[LL_MAX_PLAYERS];
};

static LLResult fake_load(unsigned player_id, const char *rom_path, void *userdata) {
    (void) rom_path;
    struct FakeBackend *fake = userdata;
    fake->loaded[player_id - 1] = true;
    return LL_OK;
}

static LLResult fake_save(unsigned player_id, void *userdata) {
    struct FakeBackend *fake = userdata;
    fake->saves[player_id - 1]++;
    return LL_OK;
}

static LLResult fake_close(unsigned player_id, void *userdata) {
    struct FakeBackend *fake = userdata;
    fake->loaded[player_id - 1] = false;
    fake->closes[player_id - 1]++;
    if (fake->active == player_id) fake->active = 0;
    return LL_OK;
}

static LLResult fake_active(unsigned player_id, void *userdata) {
    struct FakeBackend *fake = userdata;
    if (!fake->loaded[player_id - 1]) return LL_ERR_SLOT_EMPTY;
    fake->active = player_id;
    return LL_OK;
}

int main(void) {
    struct FakeBackend fake = {0};
    LLBackend backend = {
        .load_instance = fake_load,
        .save_instance = fake_save,
        .close_instance = fake_close,
        .set_active_instance = fake_active,
    };
    LLSession session;
    ll_session_init(&session, &backend, &fake);

    assert(ll_session_add(&session, 1, "Emerald.gba") == LL_OK);
    assert(ll_session_add(&session, 2, "FireRed.gba") == LL_OK);
    assert(ll_session_add(&session, 3, "e-Reader.gba") == LL_OK);
    assert(ll_session_count(&session) == 3);
    assert(ll_session_active_player(&session) == 1);

    assert(ll_session_remove(&session, 2, false) == LL_OK);
    assert(!ll_session_has_player(&session, 2));
    assert(ll_session_has_player(&session, 3));
    assert(ll_session_get_slot(&session, 3)->player_id == 3);

    assert(ll_session_switch(&session, 3) == LL_OK);
    assert(ll_session_quit_active(&session, true) == LL_OK);
    assert(fake.saves[2] == 1);
    assert(fake.closes[2] == 1);
    assert(ll_session_has_player(&session, 1));
    assert(ll_session_active_player(&session) == 1);

    puts("session tests passed");
    return 0;
}
