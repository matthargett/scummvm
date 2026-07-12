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

// pd->file has members named like the (forbidden) POSIX calls
#define FORBIDDEN_SYMBOL_EXCEPTION_mkdir
#define FORBIDDEN_SYMBOL_EXCEPTION_unlink

#include "backends/platform/playdate/osystem_playdate.h"

#include "backends/events/default/default-events.h"
#include "backends/events/playdate/playdate-events.h"
#include "backends/fs/playdate/playdate-fs-factory.h"
#include "backends/graphics/playdate/playdate-graphics.h"
#include "backends/mixer/playdate/playdate-mixer.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"

#include "common/config-manager.h"

#include "backends/platform/playdate/playdate-coroutine.h"
#include "backends/platform/playdate/playdate-sdk.h"

namespace {

/**
 * DefaultSaveFileManager uses the POSIX remove() call to delete
 * savegames, which does not exist on the Playdate. Route deletion
 * through pd->file instead.
 */
class PlaydateSaveFileManager : public DefaultSaveFileManager {
public:
	PlaydateSaveFileManager(PlaydateAPI *pd, const Common::Path &defaultSavepath)
		: DefaultSaveFileManager(defaultSavepath), _pd(pd) {}

protected:
	Common::ErrorCode removeFile(const Common::FSNode &fileNode) override {
		Common::String path = fileNode.getPath().toString('/');
		while (path.hasPrefix("/"))
			path.deleteChar(0);
		if (_pd->file->unlink(path.c_str(), 0) == 0)
			return Common::kNoError;
		return Common::kUnknownError;
	}

private:
	PlaydateAPI *_pd;
};

} // End of anonymous namespace

OSystem_Playdate::OSystem_Playdate(PlaydateAPI *pd)
	: _pd(pd), _eventSource(nullptr), _startTime(0) {
	_fsFactory = new PlaydateFilesystemFactory(pd);
	_startTime = pd->system->getCurrentTimeMilliseconds();
}

OSystem_Playdate::~OSystem_Playdate() {
}

void OSystem_Playdate::initBackend() {
	ConfMan.registerDefault("gui_scale", 100);
	ConfMan.registerDefault("gui_return_to_launcher_at_exit", true);
	ConfMan.registerDefault("themepath", Common::Path("/themes"));

	// Render AGI games in the period-correct 1-bit Hercules mode, sized
	// for the Playdate's 400x240 display. The AGI engine draws the game
	// (with authentic Sierra fill patterns) and the crank word list
	// directly into a 2-color display buffer, which this backend blits
	// verbatim.
	ConfMan.registerDefault("render_mode", "playdate");

	_pd->file->mkdir("saves");

	_timerManager = new DefaultTimerManager();
	_savefileManager = new PlaydateSaveFileManager(_pd, Common::Path("/saves"));
	_graphicsManager = new PlaydateGraphicsManager(_pd);

	_eventSource = new PlaydateEventSource(this);
	_eventManager = new DefaultEventManager(_eventSource);

	PlaydateMixerManager *mixerManager = new PlaydateMixerManager(_pd);
	mixerManager->init();
	_mixerManager = mixerManager;

	BaseBackend::initBackend();
}

Common::MutexInternal *OSystem_Playdate::createMutex() {
	// ScummVM runs single threaded on the Playdate. The audio callback
	// runs asynchronously but only touches the mixer manager's lock-free
	// ring buffer, so no real mutex is needed.
	return new NullMutexInternal();
}

uint32 OSystem_Playdate::getMillis(bool skipRecord) {
	return _pd->system->getCurrentTimeMilliseconds() - _startTime;
}

void OSystem_Playdate::delayMillis(uint msecs) {
	// Yield frames to the OS while waiting, so the display keeps
	// refreshing and audio/input stay serviced. getMillis() advances
	// in real time regardless of yields.
	const uint32 endTime = getMillis() + msecs;
	do {
		updateSubsystems();
		Playdate::coroutineYield();
	} while (getMillis() < endTime);
}

void OSystem_Playdate::getTimeAndDate(TimeDate &td, bool skipRecord) const {
	unsigned int ms = 0;
	uint32 epoch = _pd->system->getSecondsSinceEpoch(&ms);

	// getSecondsSinceEpoch() is relative to 2000-01-01 00:00 UTC,
	// convertEpochToDateTime() converts exactly that.
	PDDateTime dt;
	_pd->system->convertEpochToDateTime(epoch, &dt);

	td.tm_year = dt.year - 1900;
	td.tm_mon = dt.month - 1;
	td.tm_mday = dt.day;
	td.tm_hour = dt.hour;
	td.tm_min = dt.minute;
	td.tm_sec = dt.second;
	td.tm_wday = dt.weekday % 7; // PDDateTime: mon=1 .. sun=7
}

void OSystem_Playdate::updateSubsystems() {
	if (_timerManager)
		((DefaultTimerManager *)_timerManager)->handler();

	if (_mixerManager)
		((PlaydateMixerManager *)_mixerManager)->update();
}

void OSystem_Playdate::quit() {
	_pd->system->logToConsole("OSystem_Playdate::quit()");
}

void OSystem_Playdate::fatalError() {
	// pd->system->error() halts the program and shows the message
	// on screen (or in the simulator console).
	_pd->system->error("ScummVM: fatal error");
	for (;;)
		;
}

void OSystem_Playdate::logMessage(LogMessageType::Type type, const char *message) {
	_pd->system->logToConsole("%s", message);
}

void OSystem_Playdate::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	// Resources bundled inside the pdx are visible at the root of the
	// pd->file namespace, next to the writable data directory.
	s.addDirectory("themes", "/themes", priority);
	s.addDirectory("engine-data", "/engine-data", priority);
}
