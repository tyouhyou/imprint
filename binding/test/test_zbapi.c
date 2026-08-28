/* C smoke test: drive the app like a host shell does.
 *
 * Deterministic by construction (F11): the demo opens its setup dialogs
 * as modals, so the drive is keyboard-only -- Tab focuses a dialog
 * button (the modal keeps the focus inside), Enter activates it. Two
 * dialogs, two rounds; no screen coordinates are involved, so a layout
 * change cannot rot this test. The pixel scan is sized by
 * zb_buffer_bpp (a 16bpp build has half the bytes).
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "zbapi.h"

int painted_calls = 0;

static void on_painted(void *userdata)
{
    (void)userdata;
    painted_calls++;
}

static void on_log(int level, const char *message)
{
    (void)level;
    (void)message;
}

int main(void)
{
    zb_set_log_callback(on_log);

    zb_app_t *app = zb_app_create(320, 240);
    assert(app != NULL);
    zb_set_painted_callback(app, on_painted, NULL);

    /* the initial frame renders the setup dialog */
    zb_paint(app);
    assert(painted_calls >= 1);

    uint32_t w = 0, h = 0;
    const uint8_t *buf = zb_buffer(app, &w, &h);
    assert(buf != NULL);
    assert(w == 320 && h == 240);

    /* the first pixel row is the dialog mask over the board: non-zero in
     * every depth; the scan is bounded by the build's bytes per pixel */
    const int bpp = zb_buffer_bpp();
    assert(bpp == 4 || bpp == 2);

    /* runtime capability queries (batch K / D7): the linked library
     * speaks this header's ABI, and the format agrees with the width */
    assert(zb_version() == ZB_API_VERSION);
    const int fmt = zb_buffer_format();
    assert(fmt == (bpp == 4 ? ZB_FORMAT_BGRA8 : ZB_FORMAT_ABGR1555));
    int nonzero = 0;
    for (size_t i = 0; i < (size_t)w * h * (size_t)bpp; i += (size_t)bpp)
    {
        if (buf[i] != 0 || buf[i + 1] != 0)
        {
            nonzero = 1;
            break;
        }
    }
    assert(nonzero);

    /* keyboard drives both setup dialogs: difficulty, then the side.
     * Every claimed key repaints (a frame was owed and painted). */
    for (int round = 0; round < 2; ++round)
    {
        const int base = painted_calls;
        zb_input(app, ZB_INPUT_KEY_DOWN, 0, 0, ZB_KEY_TAB, 0, 0);
        assert(painted_calls > base);  /* focus move into the dialog */
        const int after_tab = painted_calls;
        zb_input(app, ZB_INPUT_KEY_DOWN, 0, 0, ZB_KEY_ENTER, 0, 0);
        assert(painted_calls > after_tab);  /* button activated */
    }

    /* the game is up: printable characters travel through the ch channel;
     * the demo has no text input, so the dispatcher drops them (B1) --
     * no crash, and nothing is claimed */
    const int base = painted_calls;
    zb_input(app, ZB_INPUT_KEY_DOWN, 0, 0, 0, 'A', 0);
    zb_input(app, ZB_INPUT_KEY_UP, 0, 0, 0, 0, 0);
    assert(painted_calls == base);

    zb_app_destroy(app);

    printf("zbapi smoke test passed\n");
    return 0;
}
