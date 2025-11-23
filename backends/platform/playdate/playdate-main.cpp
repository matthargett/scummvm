extern "C" {
#include "pd_api.h"
}
#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "base/main.h"
#include "base/plugins.h"
#include "common/system.h"
#include "common/util.h"
#include "playdate-input.h"
#include "playdate-backend.h"
#include <string>
#include <cstring>
#include <thread>

static OSystem_Playdate *g_playdate_system = nullptr;
static PlaydateAPI *g_pd = nullptr;
static bool g_scummvm_initialized = false;
static bool g_scummvm_started = false;
std::string g_playdateGamePath;
static float g_lastCrank = 0.0f;
static std::thread g_scummvm_thread;

static bool fileExists(PlaydateAPI *pd, const std::string &path) {
	if (!pd || !pd->file)
		return false;
	FileStat stat;
	return pd->file->stat(path.c_str(), &stat) == 0 && stat.isdir == 0;
}

static bool dirHasAgi(PlaydateAPI *pd, const std::string &dir) {
	return fileExists(pd, dir + "/LOGDIR") && fileExists(pd, dir + "/OBJECT") && fileExists(pd, dir + "/PICDIR") && fileExists(pd, dir + "/WORDS.TOK");
}

static void logDir(PlaydateAPI *pd, const char *label, const std::string &dir) {
	if (!pd || !pd->file || !pd->system)
		return;
	pd->system->logToConsole("Listing %s (%s)", label, dir.c_str());
	pd->file->listfiles(dir.c_str(), [](const char *filename, void *userdata) {
		PlaydateAPI *p = static_cast<PlaydateAPI *>(userdata);
		if (p && p->system)
			p->system->logToConsole("  %s", filename);
	}, pd, 0);
}

static std::string findFirstAgiGame(PlaydateAPI *pd) {
	if (!pd || !pd->file)
		return "/games";

	std::string best;

	struct Context {
		PlaydateAPI *pd;
		std::string parent;
		std::string *best;
	} ctx{pd, "/games", &best};

	logDir(pd, "root", "/");
	logDir(pd, "/games", "/games");
	logDir(pd, "games", "games");

	// Probe /games
	pd->file->listfiles(ctx.parent.c_str(), [](const char *filename, void *userdata) {
		Context *c = static_cast<Context *>(userdata);
		std::string full = c->parent;
		if (!full.empty() && full.back() != '/')
			full += "/";
		full += filename;
		FileStat st;
		if (c->pd->file->stat(full.c_str(), &st) != 0)
			return;
		if (!st.isdir)
			return;
		if (dirHasAgi(c->pd, full)) {
			if (c->best->empty())
				*c->best = full;
		}
	}, &ctx, 0);

	// If nothing found, try without leading slash (some Playdate SDK setups)
	if (best.empty()) {
		ctx.parent = "games";
		pd->file->listfiles(ctx.parent.c_str(), [](const char *filename, void *userdata) {
			Context *c = static_cast<Context *>(userdata);
			std::string full = c->parent;
			if (!full.empty() && full.back() != '/')
				full += "/";
			full += filename;
			FileStat st;
			if (c->pd->file->stat(full.c_str(), &st) != 0)
				return;
			if (!st.isdir)
				return;
			if (dirHasAgi(c->pd, full)) {
				if (c->best->empty())
					*c->best = full;
			}
		}, &ctx, 0);
	}

	if (best.empty())
		best = "/games/kq1";
	// normalize: drop trailing slash to avoid stat edge cases
	while (best.size() > 1 && best.back() == '/')
		best.pop_back();
	if (pd && pd->system)
		pd->system->logToConsole("Selected AGI path: %s", best.c_str());
	g_playdateGamePath = best;
	return best;
}

static int update(void *userdata) {
	// Poll input on main thread and enqueue to event source
	PDButtons current = (PDButtons)0, pushed = (PDButtons)0, released = (PDButtons)0;
	if (g_pd && g_pd->system)
		g_pd->system->getButtonState(&current, &pushed, &released);
	auto pushKey = [&](Common::EventType type, Common::KeyCode code, int ascii) {
		Common::Event e;
		e.type = type;
		e.kbd.keycode = code;
		e.kbd.ascii = ascii;
		PlaydateEventSource::enqueue(e);
	};
	if (pushed & kButtonUp) pushKey(Common::EVENT_KEYDOWN, Common::KEYCODE_UP, 0);
	if (pushed & kButtonDown) pushKey(Common::EVENT_KEYDOWN, Common::KEYCODE_DOWN, 0);
	if (pushed & kButtonLeft) pushKey(Common::EVENT_KEYDOWN, Common::KEYCODE_LEFT, 0);
	if (pushed & kButtonRight) pushKey(Common::EVENT_KEYDOWN, Common::KEYCODE_RIGHT, 0);
	if (pushed & kButtonA) pushKey(Common::EVENT_KEYDOWN, Common::KEYCODE_RETURN, 13);
	if (pushed & kButtonB) pushKey(Common::EVENT_KEYDOWN, Common::KEYCODE_BACKSPACE, 8);
	if (released & kButtonUp) pushKey(Common::EVENT_KEYUP, Common::KEYCODE_UP, 0);
	if (released & kButtonDown) pushKey(Common::EVENT_KEYUP, Common::KEYCODE_DOWN, 0);
	if (released & kButtonLeft) pushKey(Common::EVENT_KEYUP, Common::KEYCODE_LEFT, 0);
	if (released & kButtonRight) pushKey(Common::EVENT_KEYUP, Common::KEYCODE_RIGHT, 0);
	if (released & kButtonA) pushKey(Common::EVENT_KEYUP, Common::KEYCODE_RETURN, 13);
	if (released & kButtonB) pushKey(Common::EVENT_KEYUP, Common::KEYCODE_BACKSPACE, 8);

	// Crank rotation -> mouse wheel events
	if (g_pd && g_pd->system) {
		const float currentCrank = g_pd->system->getCrankAngle();
		float delta = currentCrank - g_lastCrank;
		while (delta > 180.0f) delta -= 360.0f;
		while (delta < -180.0f) delta += 360.0f;
		const float stepDegrees = 12.0f;
		if (ABS(delta) >= stepDegrees) {
			const int steps = (int)(delta / stepDegrees);
			Common::Event wheel;
			wheel.type = (steps > 0) ? Common::EVENT_WHEELDOWN : Common::EVENT_WHEELUP;
			for (int i = 0; i < ABS(steps); ++i)
				PlaydateEventSource::enqueue(wheel);
		}
		g_lastCrank = currentCrank;
	}

	static std::string cachedGamePath;
	if (g_system && g_scummvm_initialized && !g_scummvm_started) {
		if (cachedGamePath.empty())
			cachedGamePath = findFirstAgiGame(g_pd);

		g_scummvm_started = true;
		const std::string pathArgStr = "--path=" + cachedGamePath;
		g_scummvm_thread = std::thread([pathArgStr]() {
			const char *argv[] = {
				"scummvm",
				"--render-mode=playdate",
				"--music-driver=adlib",
				"--engine=agi",
				"--auto-detect",
				pathArgStr.c_str()};
			int argc = 6;
			scummvm_main(argc, argv);
		});
		g_scummvm_thread.detach();
	}

	// Blit the shared backbuffer to the Playdate framebuffer
	if (g_pd && g_pd->graphics && playdateConsumeDirtyFlag()) {
		const Common::Array<uint8_t> &src = playdateBackBuffer();
		uint8_t *dst = g_pd->graphics->getFrame();
		// Playdate framebuffer stride is 52 bytes (400 bits padded to 32-bit boundaries)
		const int stride = 52;
		std::memset(dst, 0, stride * 240);

		const int srcW = playdateBackBufferWidth() ? playdateBackBufferWidth() : 320;
		const int srcH = playdateBackBufferHeight() ? playdateBackBufferHeight() : 200;

		// Layout: reserve 80px on the right for word list, 40px on the bottom for input bar.
		const int reservedRight = playdateReservedRight();
		const int reservedBottom = playdateReservedBottom();
		const int gameAreaW = 400 - reservedRight;
		const int gameAreaH = 240 - reservedBottom;

		const float scale = MIN((float)gameAreaW / srcW, (float)gameAreaH / srcH);
		const int destW = (int)(srcW * scale);
		const int destH = (int)(srcH * scale);
		// Place at top-left of game area (0,0) for now; UI will occupy the reserved area
		const int offsetX = 0;
		const int offsetY = 0;

		// Blit scaled
		for (int y = 0; y < destH; ++y) {
			int srcY = (int)((y / scale));
			if (srcY < 0) srcY = 0;
			if (srcY >= srcH) srcY = srcH - 1;
			const uint8_t *srcRow = src.data() + srcY * srcW;

			uint8_t *row = dst + (y + offsetY) * stride;

			for (int x = 0; x < destW; ++x) {
				int srcX = (int)((x / scale));
				if (srcX < 0) srcX = 0;
				if (srcX >= srcW) srcX = srcW - 1;
				int destX = x + offsetX;
				if (srcRow[srcX]) {
					int byteIndex = destX / 8;
					int bitIndex = 7 - (destX % 8);
					row[byteIndex] |= (1 << bitIndex);
				}
			}
		}

		g_pd->graphics->markUpdatedRows(0, 240);
	}
	return 1; // Continue running
}

// Main Playdate entry point
#ifdef __cplusplus
extern "C" {
#endif

#if defined(__APPLE__)
__attribute__((visibility("default")))
#endif
int
eventHandler(PlaydateAPI *pd, PDSystemEvent event, uint32_t arg) {
	if (event == kEventInit) {
		g_pd = pd;

		pd->system->logToConsole("ScummVM: eventHandler called with kEventInit");

		// Create and initialize the OSystem
		g_playdate_system = new OSystem_Playdate(pd);
		g_system = g_playdate_system;

		// Set the update callback
		pd->system->setUpdateCallback(update, pd);

		pd->system->logToConsole("ScummVM: Backend created");

		pd->system->logToConsole("ScummVM: Starting main...");
		g_scummvm_initialized = true;
	} else if (event == kEventTerminate) {
		if (g_system) {
			g_system->quit();
			delete g_playdate_system;
			g_playdate_system = nullptr;
			g_system = nullptr;
		}
	}

	return 0;
}

#ifdef __cplusplus
}
#endif
