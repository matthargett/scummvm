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

#define FORBIDDEN_SYMBOL_EXCEPTION_mkdir

#include "backends/platform/playdate/osystem_playdate.h"
#include "backends/platform/playdate/playdate-coroutine.h"
#include "backends/fs/playdate/playdate-stream.h"
#include "base/main.h"
#include "common/algorithm.h"
#include "common/archive.h"
#include "common/compression/unzip.h"
#include "common/ptr.h"
#include "common/str-array.h"

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

static const uint kPathArgSize = 300;

static PlaydateAPI *s_pd;

// The OS allocator, stored before anything else runs. On the device
// build newlib's malloc is routed here (there is no sbrk-style heap),
// equivalent to what setup.c in the Playdate SDK does.
static void *(*s_pdrealloc)(void *ptr, size_t size);

#if TARGET_PLAYDATE
// These are called from outside the program (newlib's startup and allocator),
// which the link-time optimiser cannot see, so mark them "used" or LTO's
// dead-code pass would strip them and break the link.
#define PD_KEEP __attribute__((used))

// Building with -nostartfiles leaves out crti.o/crtn.o, but newlib's
// __libc_init_array() still references these.
PD_KEEP extern "C" void _init(void) {}
PD_KEEP extern "C" void _fini(void) {}

PD_KEEP extern "C" void *_malloc_r(struct _reent *, size_t size) {
	return s_pdrealloc(nullptr, size);
}

PD_KEEP extern "C" void *_realloc_r(struct _reent *, void *ptr, size_t size) {
	return s_pdrealloc(ptr, size);
}

PD_KEEP extern "C" void _free_r(struct _reent *, void *ptr) {
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

// Shows a line of text and lets the OS present it
static void showStatus(const Common::String &text) {
	s_pd->graphics->clear(kColorBlack);
	s_pd->graphics->setDrawMode(kDrawModeFillWhite);
	s_pd->graphics->drawText(text.c_str(), text.size(), kASCIIEncoding, 8, 110);
	s_pd->graphics->setDrawMode(kDrawModeCopy);
	Playdate::coroutineYield();
}

static void makeDirs(const Common::String &path) {
	for (uint i = 0; i < path.size(); i++) {
		if (path[i] == '/')
			s_pd->file->mkdir(Common::String(path.c_str(), i).c_str());
	}
	s_pd->file->mkdir(path.c_str());
}

// The part of a path up to and including its last slash
static Common::String parentDir(const Common::String &path) {
	const size_t slash = path.findLastOf('/');
	return (slash == Common::String::npos) ? Common::String() : Common::String(path.c_str(), slash + 1);
}

static void zipCallback(const char *filename, void *userdata) {
	const Common::String name(filename);
	if (name.hasSuffixIgnoreCase(".zip"))
		((Common::StringArray *)userdata)->push_back(name);
}

// A zip archive copied into games/ is unpacked into a directory of the same
// name the first time it is seen. The folders all of its files share are
// dropped, so the game files end up directly inside that directory.
static void unpackGameArchives() {
	Common::StringArray zips;
	s_pd->file->listfiles("games", zipCallback, &zips, 0);

	for (uint z = 0; z < zips.size(); z++) {
		const Common::String name(zips[z].c_str(), zips[z].size() - 4);
		const Common::String target = "games/" + name;

		FileStat st;
		if (s_pd->file->stat(target.c_str(), &st) == 0)
			continue;

		PlaydateIoStream *zip = PlaydateIoStream::makeFromPath(s_pd, "games/" + zips[z], false);
		if (!zip)
			continue;
		Common::ScopedPtr<Common::Archive> archive(Common::makeZipArchive(zip));
		if (!archive)
			continue;

		showStatus("Unpacking " + name);

		Common::ArchiveMemberList members;
		archive->listMembers(members);

		// The members that are files, not the folders holding other members
		Common::StringArray all, paths;
		for (const auto &member : members)
			all.push_back(member->getPathInArchive().toString('/'));
		for (uint i = 0; i < all.size(); i++) {
			bool folder = all[i].empty() || all[i].lastChar() == '/';
			for (uint j = 0; j < all.size() && !folder; j++)
				folder = all[j].hasPrefix(all[i] + "/");
			if (!folder)
				paths.push_back(all[i]);
		}
		if (paths.empty())
			continue;

		Common::String prefix = parentDir(paths[0]);
		for (uint i = 1; i < paths.size(); i++) {
			while (!prefix.empty() && !paths[i].hasPrefix(prefix)) {
				prefix.deleteLastChar();
				prefix = parentDir(prefix);
			}
		}

		for (const auto &member : members) {
			const Common::String path = member->getPathInArchive().toString('/');
			if (Common::find(paths.begin(), paths.end(), path) == paths.end())
				continue;

			const Common::String out = target + "/" + (path.c_str() + prefix.size());
			Common::String outDir = parentDir(out);
			outDir.deleteLastChar();
			makeDirs(outDir);

			Common::ScopedPtr<Common::SeekableReadStream> in(member->createReadStream());
			SDFile *file = in ? s_pd->file->open(out.c_str(), kFileWrite) : nullptr;
			if (!file) {
				s_pd->system->logToConsole("unpack: cannot write %s", out.c_str());
				continue;
			}

			byte buffer[4096];
			for (;;) {
				const uint32 len = in->read(buffer, sizeof(buffer));
				if (len == 0)
					break;
				s_pd->file->write(file, buffer, len);
			}
			s_pd->file->close(file);
		}
	}
}

static void gameDirCallback(const char *filename, void *userdata) {
	// Directories are reported with a trailing slash
	const size_t len = strlen(filename);
	if (len < 2 || filename[len - 1] != '/' || filename[0] == '.')
		return;
	((Common::StringArray *)userdata)->push_back(Common::String(filename, len - 1));
}

// Returns the directory in games/ to start: the only one there, or the one
// the player picks from a list. Empty if there is none.
static Common::String chooseGameDir() {
	Common::StringArray dirs;
	s_pd->file->listfiles("games", gameDirCallback, &dirs, 0);
	Common::sort(dirs.begin(), dirs.end());

	if (dirs.size() <= 1)
		return dirs.empty() ? Common::String() : dirs[0];

	const int kRowHeight = 20;
	const int kRows = LCD_ROWS / kRowHeight;
	const float kCrankDegreesPerRow = 30.0f;

	int selected = 0;
	float crank = 0.0f;

	// Drop the press that launched the app
	PDButtons current, pushed, released;
	s_pd->system->getButtonState(&current, &pushed, &released);

	for (;;) {
		const int first = CLIP<int>(selected - kRows / 2, 0, MAX<int>(0, (int)dirs.size() - kRows));

		s_pd->graphics->clear(kColorBlack);
		for (int row = 0; row < kRows && first + row < (int)dirs.size(); row++) {
			Common::String title = dirs[first + row];
			for (uint i = 0; i < title.size(); i++) {
				if (title[i] == '_')
					title.setChar(' ', i);
			}

			const bool highlight = first + row == selected;
			if (highlight)
				s_pd->graphics->fillRect(0, row * kRowHeight, LCD_COLUMNS, kRowHeight, kColorWhite);
			s_pd->graphics->setDrawMode(highlight ? kDrawModeFillBlack : kDrawModeFillWhite);
			s_pd->graphics->drawText(title.c_str(), title.size(), kASCIIEncoding, 6, row * kRowHeight + 1);
		}
		s_pd->graphics->setDrawMode(kDrawModeCopy);

		Playdate::coroutineYield();

		s_pd->system->getButtonState(&current, &pushed, &released);

		crank += s_pd->system->getCrankChange();
		int delta = (int)(crank / kCrankDegreesPerRow);
		crank -= delta * kCrankDegreesPerRow;
		if (pushed & kButtonUp)
			delta--;
		if (pushed & kButtonDown)
			delta++;
		selected = CLIP<int>(selected + delta, 0, (int)dirs.size() - 1);

		if (pushed & kButtonA)
			break;
	}

	s_pd->graphics->clear(kColorBlack);
	return dirs[selected];
}

// The OSystem, for the update callback to service timers/audio each frame.
static OSystem_Playdate *s_system;

// Coroutine entry: runs the whole of ScummVM. Never returns; when
// ScummVM exits we idle, yielding a frame at a time.
static void emuMain() {
	OSystem_Playdate *system = new OSystem_Playdate(s_pd);
	g_system = system;
	s_system = system;

	const char *argv[] = { "scummvm", nullptr, nullptr, nullptr };
	int argc = 1;
	if (readAutorunTarget()) {
		// Explicit target (needs a matching scummvm.ini section).
		s_pd->system->logToConsole("autorun target: %s", s_autorunTarget);
		argv[argc++] = s_autorunTarget;
	} else {
		unpackGameArchives();
		const Common::String dir = chooseGameDir();
		if (!dir.empty()) {
			// Detect and start the game in games/<dir>
			static char gamePathArg[kPathArgSize];
			snprintf(gamePathArg, sizeof(gamePathArg), "--path=/games/%s", dir.c_str());
			s_pd->system->logToConsole("booting game: %s", gamePathArg);
			argv[argc++] = "--auto-detect";
			argv[argc++] = gamePathArg;
		}
	}

	int res = scummvm_main(argc, const_cast<char **>(argv));

	s_pd->system->logToConsole("ScummVM exited (%d)", res);
	s_system = nullptr; // the update callback must not touch it once destroyed
	system->destroy();

	for (;;)
		Playdate::coroutineYield();
}

// Playdate per-frame callback. Resumes ScummVM until it yields back a
// rendered frame, then returns 1 so the OS refreshes the display.
static int updateCallback(void *userdata) {
	// WATCHDOG (debug): a single resume that runs far longer than a frame
	// means ScummVM executed a path that never reached updateScreen() or
	// delayMillis() (the only yield points). That starves the OS update
	// callback and shows as a beach ball. Log it so the offending path can
	// be found.
	// Service timers and the audio ring buffer once per display frame, so
	// audio stays fed no matter how the engine's compute happens to be
	// scheduled between yields (delayMillis and pollEvent also pump this,
	// but nothing guarantees the engine passes through them regularly).
	if (s_system)
		s_system->updateSubsystems();

	const unsigned start = s_pd->system->getCurrentTimeMilliseconds();
	Playdate::coroutineSliceBegin(start);
	Playdate::coroutineResume();
	const unsigned elapsed = s_pd->system->getCurrentTimeMilliseconds() - start;
	if (elapsed > 500)
		s_pd->system->logToConsole("WATCHDOG: resume took %u ms without yielding", elapsed);
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
