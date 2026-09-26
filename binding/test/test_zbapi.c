/* C smoke test: drive the app like a host shell does.
 *
 * Deterministic by construction (F11): the demo opens its setup dialogs
 * as modals, so the drive is keyboard-only -- Tab focuses a dialog
 * button (the modal keeps the focus inside), Enter activates it. Two
 * dialogs, two rounds; no screen coordinates are involved, so a layout
 * change cannot rot this test. The pixel scan is sized by
 * zb_buffer_bpp (a 16bpp build has half the bytes).
 *
 * The second half drives a declarative app (P3): a design file instead
 * of the story app, actions by id, widget text read/write -- the same
 * keyboard-only discipline.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zbapi.h"

int painted_calls = 0;

static void on_painted(void *userdata)
{
    (void)userdata;
    painted_calls++;
}

static void on_action_count(const char *widget_id, void *userdata)
{
    (void)widget_id;
    ++*(int *)userdata;
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

    /* ---------- declarative apps (P3, ARCHITECTURE 4.8) ---------- *
     * a design file instead of the story app: one button, one label.
     * The drive is keyboard-only like above -- the button takes focus
     * (first focusable), Enter activates it, the by-id callback fires,
     * and the host writes the label through the widget-text exports. */
    static const char *UI_TEXT =
        "column id=\"root\" spacing=6 padding=10\n"
        "  label id=\"count\" text=\"Clicks: 0\"\n"
        "  button id=\"ok\" text=\"OK\"\n";

    zb_app_t *ui = zb_app_create_from_ui(UI_TEXT, 0, 320, 240);
    assert(ui != NULL);

    int clicks = 0;
    zb_set_event_callback(ui, "ok", on_action_count, &clicks);
    /* a second registration replaces, NULL unregisters: exercise both */
    zb_set_event_callback(ui, "missing_widget", on_action_count, &clicks);

    zb_paint(ui);
    const uint8_t *ubuf = zb_buffer(ui, &w, &h);
    assert(ubuf != NULL && w == 320 && h == 240);

    /* the label reads back what the design file declared */
    char text[64];
    assert(zb_widget_text(ui, "count", text, sizeof(text)) > 0);
    assert(strcmp(text, "Clicks: 0") == 0);

    /* missing widget: -1, no crash */
    assert(zb_widget_text(ui, "nope", text, sizeof(text)) == -1);

    /* Tab focuses the button, Enter activates it: the action callback
     * fires with the id, and the host writes through zb_widget_set_text */
    assert(clicks == 0);
    zb_input(ui, ZB_INPUT_KEY_DOWN, 0, 0, ZB_KEY_TAB, 0, 0);
    zb_input(ui, ZB_INPUT_KEY_DOWN, 0, 0, ZB_KEY_ENTER, 0, 0);
    assert(clicks == 1);

    zb_widget_set_text(ui, "count", "Clicks: 1");
    assert(zb_widget_text(ui, "count", text, sizeof(text)) > 0);
    assert(strcmp(text, "Clicks: 1") == 0);

    /* unregister stops the firing; the frame still paints */
    zb_set_event_callback(ui, "ok", NULL, NULL);
    zb_input(ui, ZB_INPUT_KEY_DOWN, 0, 0, ZB_KEY_TAB, 0, 0);
    zb_input(ui, ZB_INPUT_KEY_DOWN, 0, 0, ZB_KEY_ENTER, 0, 0);
    assert(clicks == 1);

    /* a malformed design fails the create (exit code 2 semantics) */
    assert(zb_app_create_from_ui("garbage ?? no tags", 0, 100, 100) == NULL);
    assert(zb_app_create_from_ui(NULL, 0, 100, 100) == NULL);

    /* the HTML front-end drives the same path (page box sizes it) */
    zb_app_t *html_app = zb_app_create_from_ui(
        "<body style=\"width:200px;height:100px\">"
        "<button id=\"b\" text=\"hi\"></button></body>", 1, 0, 0);
    assert(html_app != NULL);
    {
        uint32_t hw = 0, hh = 0;
        assert(zb_buffer(html_app, &hw, &hh) != NULL);
        assert(hw == 200 && hh == 100);
    }
    zb_app_destroy(html_app);

    zb_app_destroy(ui);

    printf("zbapi smoke test passed\n");
    return 0;
}
