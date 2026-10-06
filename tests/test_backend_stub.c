#include "mgba_backend.h"
#include "session.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    LLMgbaBackend *backend = ll_mgba_backend_create();
    assert(backend);

    LLSession session;
    ll_session_init(&session, ll_mgba_backend_interface(), backend);

    assert(ll_session_add(&session, 1, "Emerald.gba") == LL_OK);
    assert(ll_session_add(&session, 3, "e-Reader.gba") == LL_OK);
    assert(ll_mgba_backend_is_loaded(backend, 1));
    assert(ll_mgba_backend_is_loaded(backend, 3));
    assert(ll_mgba_backend_active_player(backend) == 1);

    LLVideoFrame frame;
    assert(ll_mgba_backend_get_frame(backend, 3, &frame) == LL_OK);
    assert(frame.width == 240 && frame.height == 160);

    assert(ll_session_switch(&session, 3) == LL_OK);
    assert(ll_mgba_backend_active_player(backend) == 3);
    assert(ll_session_quit_active(&session, false) == LL_OK);
    assert(!ll_mgba_backend_is_loaded(backend, 3));
    assert(ll_mgba_backend_active_player(backend) == 1);

    ll_mgba_backend_destroy(backend);
    puts("backend stub tests passed");
    return 0;
}
