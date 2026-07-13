/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "backends/platform/playdate/osystem_playdate.h"
#include "backends/platform/playdate/playdate-coroutine.h"
#include "base/main.h"

#include "backends/platform/playdate/playdate-sdk.h"

#if TARGET_PLAYDATE
// Provided by newlib; runs the C++ static constructors. The Playdate
// firmware does not process the .init_array section of pdex.bin, so
// this must be called before any ScummVM code runs, and after the
// allocator has been stored by eventHandlerShim().
extern "C" void __libc_init_array(void);
#endif

// Stack for the coroutine that runs scummvm_main(). ScummVM is a heavy
// blocking program, so it gets a generous stack.
static const uint kEmuStackSize = 512 * 1024;

static PlaydateAPI *s_pd;

// The OS allocator, stored before anything else runs. On the device
// build newlib's malloc is routed here (there is no sbrk-style heap),
// equivalent to what setup.c in the Playdate SDK does.
static void *(*s_pdrealloc)(void *ptr, size_t size);

#if TARGET_PLAYDATE
// Building with -nostartfiles leaves out crti.o/crtn.o, but newlib's
// __libc_init_array() still references these.
extern "C" void _init(void) {}
extern "C" void _fini(void) {}

extern "C" void *_malloc_r(struct _reent *, size_t size) {
	return s_pdrealloc(nullptr, size);
}

extern "C" void *_realloc_r(struct _reent *, void *ptr, size_t size) {
	return s_pdrealloc(ptr, size);
}

extern "C" void _free_r(struct _reent *, void *ptr) {
	if (ptr)
		s_pdrealloc(ptr, 0);
}
#endif

// If an "autorun" file is present at the pd->file root, its first line
// is treated as a game target to launch directly, skipping the
// launcher. This lets a pdx be shipped preconfigured for a single game.
static char s_autorunTarget[64];

static bool readAutorunTarget() {
	SDFile *file = s_pd->file->open("autorun", (FileOptions)(kFileRead | kFileReadData));
	if (!file)
		return false;

	int len = s_pd->file->read(file, s_autorunTarget, sizeof(s_autorunTarget) - 1);
	s_pd->file->close(file);
	if (len <= 0)
		return false;

	s_autorunTarget[len] = '\0';
	// Trim trailing whitespace / newlines
	for (int i = len - 1; i >= 0 && (s_autorunTarget[i] == '\n' || s_autorunTarget[i] == '\r' ||
	                                 s_autorunTarget[i] == ' ' || s_autorunTarget[i] == '\t'); i--)
		s_autorunTarget[i] = '\0';

	return s_autorunTarget[0] != '\0';
}

// Without an autorun file, the first (alphabetically) subdirectory of
// "games/" - inside the pdx bundle or the data directory - is detected
// and started directly, so a pdx with game data boots straight into a
// game instead of the ScummVM launcher, which is neither legible nor
// operable on a 1-bit, keyboard-less handheld.
static char s_firstGameDir[64];

static void firstGameDirCallback(const char *filename, void *userdata) {
	(void)userdata;

	// Directories are reported with a trailing slash.
	size_t len = strlen(filename);
	if (len < 2 || len >= sizeof(s_firstGameDir) || filename[len - 1] != '/')
		return;
	if (filename[0] == '.')
		return;

	// Keep the alphabetically first directory so the pick is stable.
	if (s_firstGameDir[0] != '\0' && strcmp(filename, s_firstGameDir) >= 0)
		return;

	memcpy(s_firstGameDir, filename, len - 1);
	s_firstGameDir[len - 1] = '\0';
}

static bool findFirstGameDir() {
	s_firstGameDir[0] = '\0';
	s_pd->file->listfiles("games", firstGameDirCallback, nullptr, 0);
	return s_firstGameDir[0] != '\0';
}

// Coroutine entry: runs the whole of ScummVM. Never returns; when
// ScummVM exits we idle, yielding a frame at a time.
static void emuMain() {
	OSystem_Playdate *system = new OSystem_Playdate(s_pd);
	g_system = system;

	static char s_gamePathArg[80];

	const char *argv[] = { "scummvm", nullptr, nullptr, nullptr };
	int argc = 1;
	if (readAutorunTarget()) {
		// Explicit target (needs a matching scummvm.ini section).
		s_pd->system->logToConsole("autorun target: %s", s_autorunTarget);
		argv[argc++] = s_autorunTarget;
	} else if (findFirstGameDir()) {
		// Detect and start the game in games/<dir>.
		snprintf(s_gamePathArg, sizeof(s_gamePathArg), "--path=/games/%s", s_firstGameDir);
		s_pd->system->logToConsole("booting first game: %s", s_gamePathArg);
		argv[argc++] = "--auto-detect";
		argv[argc++] = s_gamePathArg;
	}

	int res = scummvm_main(argc, const_cast<char **>(argv));

	s_pd->system->logToConsole("ScummVM exited (%d)", res);
	system->destroy();

	for (;;)
		Playdate::coroutineYield();
}

// Playdate per-frame callback. Resumes ScummVM until it yields back a
// rendered frame, then returns 1 so the OS refreshes the display.
static int updateCallback(void *userdata) {
	Playdate::coroutineResume();
	return 1;
}

// Entry point: the device linker script and the simulator both look
// for eventHandlerShim.
extern "C" int eventHandlerShim(PlaydateAPI *pd, PDSystemEvent event, uint32_t arg) {
	switch (event) {
	case kEventInit:
		s_pd = pd;
		s_pdrealloc = pd->system->realloc;
#if TARGET_PLAYDATE
		__libc_init_array();
#endif
		// Run at the display's native refresh rate.
		pd->display->setRefreshRate(30.0f);

		Playdate::coroutineInit();
		Playdate::coroutineCreate(emuMain, kEmuStackSize);
		pd->system->setUpdateCallback(updateCallback, nullptr);
		break;
	case kEventTerminate:
		if (g_system)
			g_system->quit();
		break;
	default:
		break;
	}

	return 0;
}

// Some SDK versions resolve the unshimmed name in the simulator
extern "C" int eventHandler(PlaydateAPI *pd, PDSystemEvent event, uint32_t arg) {
	return eventHandlerShim(pd, event, arg);
}
