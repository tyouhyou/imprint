#include "arm9_setting.h"
#include "arm9_packing.h"
#include <nds.h>
#include <stdio.h>
#include "app_maker.hpp"
#include "input.hpp"
#include "logging.hpp"

using namespace zb::input;
using namespace zb::app;

namespace
{
	// sends a key event; A confirms (enter), B is space
	void send_key(const zb::SharedPtr<IApp> &app, const key_code k)
	{
		input_event ev;
		ev.type = input_type::key_down;
		ev.key = static_cast<int>(k);
		app->input(ev);
	}

	/*
	 * Maps the touch screen to touch_down/touch_up/touch_move events.
	 * touch_move is only sent while the position changes (the dispatcher
	 * uses it to cancel a press that left its widget).
	 * The NDS panel has a single touch point: touch_id is always 0.
	 * Returns true when an event was produced.
	 */
	bool handle_touch(const zb::SharedPtr<IApp> &app, bool &touch_pressed, int &last_x, int &last_y)
	{
		const bool down = (keysHeld() & KEY_TOUCH) != 0;
		constexpr int touch_id = 0;

		if (down && !touch_pressed)
		{
			touchPosition touch;
			touchRead(&touch);
			touch_pressed = true;
			last_x = touch.px;
			last_y = touch.py;

			input_event ev;
			ev.type = input_type::touch_down;
			ev.x = last_x;
			ev.y = last_y;
			ev.touch_id = touch_id;
			LD << "touch_down " << ev.x << "," << ev.y;
			app->input(ev);
			return true;
		}

		if (!down && touch_pressed)
		{
			touch_pressed = false;

			input_event ev;
			ev.type = input_type::touch_up;
			ev.x = last_x;
			ev.y = last_y;
			ev.touch_id = touch_id;
			LD << "touch_up " << ev.x << "," << ev.y;
			app->input(ev);
			return true;
		}

		if (down)
		{
			touchPosition touch;
			touchRead(&touch);
			if (touch.px != last_x || touch.py != last_y)
			{
				last_x = touch.px;
				last_y = touch.py;

				input_event ev;
				ev.type = input_type::touch_move;
				ev.x = last_x;
				ev.y = last_y;
				ev.touch_id = touch_id;
				LD << "touch_move " << ev.x << "," << ev.y;
				app->input(ev);
				return true;
			}
		}
		return false;
	}

	// maps the d-pad to focus movement and A/B to activation
	/*
	 * Maps the NDS buttons to navigation keys. The hardware has no
	 * keyboard: there is no character source on this shell (B2), so
	 * input_event::ch stays 0 here.
	 */
	bool handle_keys(const zb::SharedPtr<IApp> &app)
	{
		const u32 down = keysDown();
		const u32 relevant = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_A | KEY_B;
		if ((down & relevant) == 0)
		{
			return false;
		}

		if (down & KEY_UP)
			send_key(app, key_code::up);
		if (down & KEY_DOWN)
			send_key(app, key_code::down);
		if (down & KEY_LEFT)
			send_key(app, key_code::left);
		if (down & KEY_RIGHT)
			send_key(app, key_code::right);
		if (down & KEY_A)
			send_key(app, key_code::enter);
		if (down & KEY_B)
			send_key(app, key_code::space);
		return true;
	}
} // namespace

int main(void)
{
	// console logs must appear immediately: newlib's stdout is fully
	// buffered by default, which would burst the log lines in batches
	std::setvbuf(stdout, nullptr, _IONBF, 0);

	zb::Logging::set_log_handle(
		[](const zb::Logging_Level &level, const std::string &message)
		{
			printlog(message);
		});
	initSubVideoConsole();
	initFrameBuffer();
	lcdMainOnBottom(); // use lcdMainOnTop() or lcdSwap() to set screen. default main screen is the top one.
	clearscreen();     // start with a clean log screen

	// double buffering: the app draws into an internal buffer, and the
	// completed frame is copied to VRAM_A with DMA during the vblank.
	// Drawing directly into VRAM would race with the LCD scanout and
	// flicker. The framebuffer pixel format (XBBBBBGGGGGRRRRR) matches
	// COLOR_DEPTH=16 abgr1555.
	auto app = zb::app::make_app();
	app->create_window(256, 192);

	const auto window = app->window();
	const void *back_buffer = window->data();
	void *screen = FRAME_BUFFER;

	// the app requests to quit by closing its window (e.g. a QUIT button)
	bool app_closed = false;
	app->on_closed([&app_closed]() { app_closed = true; });

	// a submitted frame (painted event) is copied to VRAM during the next
	// vblank; idle frames are skipped via is_dirty()
	bool frame_owed = false;
	app->on_painted([&frame_owed](const void *) { frame_owed = true; });

	app->paint(); // render the first frame (marks frame_owed)

	bool touch_pressed = false;
	int last_x = 0;
	int last_y = 0;

	// frame loop: the shell owns the loop; the app repaints on input and
	// the shell paints idle frames only when the app owes one (see IApp
	// rendering loop protocol + is_dirty)
	while (!app_closed)
	{
		swiWaitForVBlank();

		if (frame_owed)
		{
			dmaCopy(back_buffer, screen, 256 * 192 * sizeof(uint16_t));
			frame_owed = false;
		}

		scanKeys();

		if (keysDown() & KEY_START)
			break;

		handle_touch(app, touch_pressed, last_x, last_y);
		handle_keys(app);
		if (app->is_dirty())
		{
			app->paint(); // idle frame
		}
	}

	return 0;
}
