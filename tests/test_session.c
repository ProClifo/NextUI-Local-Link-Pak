#include "session.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned loaded[LL_MAX_PLAYERS + 1];
    unsigned saved[LL_MAX_PLAYERS + 1];
    unsigned closed[LL_MAX_PLAYERS + 1];
    unsigned active;
} FakeBackend;

static LLResult fake_load(unsigned player_id, const char *rom_path, void *userdata) {
    FakeBackend *fake = userdata;
    assert(rom_path && rom_path[0]);
    fake->loaded[player_id]++;
    return LL_OK;
}

static LLResult fake_save(unsigned player_id, void *userdata) {
    FakeBackend *fake = userdata;
    fake->saved[player_id]++;
    return LL_OK;
}

static LLResult fake_close(unsigned player_id, void *userdata) {
    FakeBackend *fake = userdata;
    fake->closed[player_id]++;
    return LL_OK;
}

static LLResult fake_set_active(unsigned player_id, void *userdata) {
    FakeBackend *fake = userdata;
    fake->active = player_id;
    return LL_OK;
}

static LLSession new_session(FakeBackend *fake) {
    LLBackend backend = {
        .load_instance = fake_load,
        .save_instance = fake_save,
        .close_instance = fake_close,
        .set_active_instance = fake_set_active,
    };
    LLSession session;
    memset(fake, 0, sizeof(*fake));
    ll_session_init(&session, &backend, fake);
    return session;
}

static void test_fixed_player_numbers(void) {
    FakeBackend fake;
    LLSession session = new_session(&fake);

    assert(ll_session_add(&session, 1, "Emerald.gba") == LL_OK);
    assert(ll_session_add(&session, 2, "FireRed.gba") == LL_OK);
    assert(ll_session_add(&session, 3, "e-Reader.gba") == LL_OK);
    assert(ll_session_count(&session) == 3);

    assert(ll_session_remove(&session, 2, false) == LL_OK);
    assert(!ll_session_has_player(&session, 2));
    assert(ll_session_has_player(&session, 3));
    assert(ll_session_get_slot(&session, 3)->player_id == 3);
    assert(strcmp(ll_session_get_slot(&session, 3)->rom_path, "e-Reader.gba") == 0);
}

static void test_switch_skips_empty_slots(void) {
    FakeBackend fake;
    LLSession session = new_session(&fake);

    assert(ll_session_add(&session, 1, "Emerald.gba") == LL_OK);
    assert(ll_session_add(&session, 3, "e-Reader.gba") == LL_OK);
    assert(ll_session_active_player(&session) == 1);

    assert(ll_session_switch_next(&session) == LL_OK);
    assert(ll_session_active_player(&session) == 3);
    assert(fake.active == 3);

    assert(ll_session_switch_next(&session) == LL_OK);
    assert(ll_session_active_player(&session) == 1);
}

static void test_save_and_quit_only_active(void) {
    FakeBackend fake;
    LLSession session = new_session(&fake);

    assert(ll_session_add(&session, 1, "Emerald.gba") == LL_OK);
    assert(ll_session_add(&session, 2, "FireRed.gba") == LL_OK);
    assert(ll_session_switch(&session, 2) == LL_OK);

    assert(ll_session_quit_active(&session, true) == LL_OK);
    assert(fake.saved[2] == 1);
    assert(fake.closed[2] == 1);
    assert(fake.saved[1] == 0);
    assert(fake.closed[1] == 0);
    assert(ll_session_has_player(&session, 1));
    assert(!ll_session_has_player(&session, 2));
    assert(ll_session_active_player(&session) == 1);
}

static void test_add_next_fills_first_free_slot_without_renumbering(void) {
    FakeBackend fake;
    LLSession session = new_session(&fake);
    unsigned player = 0;

    assert(ll_session_add(&session, 1, "Emerald.gba") == LL_OK);
    assert(ll_session_add(&session, 2, "FireRed.gba") == LL_OK);
    assert(ll_session_add(&session, 3, "e-Reader.gba") == LL_OK);
    assert(ll_session_remove(&session, 2, false) == LL_OK);

    assert(ll_session_add_next(&session, "Ruby.gba", &player) == LL_OK);
    assert(player == 2);
    assert(ll_session_has_player(&session, 3));
    assert(strcmp(ll_session_get_slot(&session, 3)->rom_path, "e-Reader.gba") == 0);
}

int main(void) {
    test_fixed_player_numbers();
    test_switch_skips_empty_slots();
    test_save_and_quit_only_active();
    test_add_next_fills_first_free_slot_without_renumbering();
    puts("session tests: ok");
    return 0;
}
