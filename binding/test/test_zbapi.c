/* C smoke test: drive the app like a host shell does. */
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

    /* initial frame renders the board (a green background) */
    zb_paint(app);
    uint32_t w = 0, h = 0;
    const uint8_t *buf = zb_buffer(app, &w, &h);
    assert(buf != NULL);
    assert(w == 320 && h == 240);

    /* the first pixel of a 32bpp build is the green mat; 16bpp has one byte
     * per halfword -- both are non-zero after the first frame */
    int nonzero = 0;
    for (size_t i = 0; i < (size_t)w * h * 4; i += 4)
    {
        if (buf[i] != 0 || buf[i + 1] != 0 || buf[i + 2] != 0)
        {
            nonzero = 1;
            break;
        }
    }
    assert(nonzero);

    /* clicking a board cell places a mark, also without error */
    zb_input(app, ZB_INPUT_TOUCH_DOWN, 60, 60, 0, 0);
    zb_input(app, ZB_INPUT_TOUCH_UP, 60, 60, 0, 0);
    zb_paint(app);
    assert(painted_calls >= 1);

    zb_app_destroy(app);

    printf("zbapi smoke test passed\n");
    return 0;
}