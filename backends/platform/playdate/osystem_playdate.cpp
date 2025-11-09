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

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "backends/platform/playdate/osystem_playdate.h"
#include "backends/platform/playdate/playdate-graphics.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "backends/events/default/default-events.h"
#include "backends/mixer/null/null-mixer.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/fs/posix/posix-fs-factory.h"
#include "common/config-manager.h"
#include "common/scummsys.h"

OSystem_Playdate::OSystem_Playdate(PlaydateAPI *pd)
	: _pd(pd),
	  _startTime(0),
	  _currentButtons(0),
	  _previousButtons(0),
	  _crankAngle(0.0f),
	  _previousCrankAngle(0.0f) {

	// Use POSIX filesystem
	_fsFactory = new POSIXFilesystemFactory();
}

OSystem_Playdate::~OSystem_Playdate() {
}

void OSystem_Playdate::initBackend() {
	// Get start time
	_startTime = _pd->system->getCurrentTimeMilliseconds();

	// Initialize managers
	_timerManager = new DefaultTimerManager();
	_eventManager = new DefaultEventManager(this);
	_savefileManager = new DefaultSaveFileManager();

	// Initialize graphics
	_graphicsManager = new PlaydateGraphicsManager(_pd);

	// Initialize mixer (start with null mixer for now)
	// TODO: Implement proper Playdate audio mixer
	_mixerManager = new NullMixerManager();
	_mixerManager->init();

	// Configure for low-memory device
	ConfMan.registerDefault("gui_scale", 100);
	ConfMan.registerDefault("gui_return_to_launcher_at_exit", false);

	BaseBackend::initBackend();
}

bool OSystem_Playdate::pollEvent(Common::Event &event) {
	// Update button state
	_previousButtons = _currentButtons;
	_pd->system->getButtonState(&_currentButtons, nullptr, nullptr);

	// Check for button press/release events
	PDButtons pressed = _currentButtons & ~_previousButtons;
	PDButtons released = ~_currentButtons & _previousButtons;

	// D-pad mapping
	if (pressed & kButtonUp) {
		event.type = Common::EVENT_KEYDOWN;
		event.kbd.keycode = Common::KEYCODE_UP;
		event.kbd.ascii = 0;
		return true;
	}
	if (released & kButtonUp) {
		event.type = Common::EVENT_KEYUP;
		event.kbd.keycode = Common::KEYCODE_UP;
		event.kbd.ascii = 0;
		return true;
	}

	if (pressed & kButtonDown) {
		event.type = Common::EVENT_KEYDOWN;
		event.kbd.keycode = Common::KEYCODE_DOWN;
		event.kbd.ascii = 0;
		return true;
	}
	if (released & kButtonDown) {
		event.type = Common::EVENT_KEYUP;
		event.kbd.keycode = Common::KEYCODE_DOWN;
		event.kbd.ascii = 0;
		return true;
	}

	if (pressed & kButtonLeft) {
		event.type = Common::EVENT_KEYDOWN;
		event.kbd.keycode = Common::KEYCODE_LEFT;
		event.kbd.ascii = 0;
		return true;
	}
	if (released & kButtonLeft) {
		event.type = Common::EVENT_KEYUP;
		event.kbd.keycode = Common::KEYCODE_LEFT;
		event.kbd.ascii = 0;
		return true;
	}

	if (pressed & kButtonRight) {
		event.type = Common::EVENT_KEYDOWN;
		event.kbd.keycode = Common::KEYCODE_RIGHT;
		event.kbd.ascii = 0;
		return true;
	}
	if (released & kButtonRight) {
		event.type = Common::EVENT_KEYUP;
		event.kbd.keycode = Common::KEYCODE_RIGHT;
		event.kbd.ascii = 0;
		return true;
	}

	// A button = Enter/Select
	if (pressed & kButtonA) {
		event.type = Common::EVENT_KEYDOWN;
		event.kbd.keycode = Common::KEYCODE_RETURN;
		event.kbd.ascii = Common::ASCII_RETURN;
		return true;
	}
	if (released & kButtonA) {
		event.type = Common::EVENT_KEYUP;
		event.kbd.keycode = Common::KEYCODE_RETURN;
		event.kbd.ascii = Common::ASCII_RETURN;
		return true;
	}

	// B button = Escape/Cancel
	if (pressed & kButtonB) {
		event.type = Common::EVENT_KEYDOWN;
		event.kbd.keycode = Common::KEYCODE_ESCAPE;
		event.kbd.ascii = Common::ASCII_ESCAPE;
		return true;
	}
	if (released & kButtonB) {
		event.type = Common::EVENT_KEYUP;
		event.kbd.keycode = Common::KEYCODE_ESCAPE;
		event.kbd.ascii = Common::ASCII_ESCAPE;
		return true;
	}

	// Crank handling - use for scrolling or special actions
	_previousCrankAngle = _crankAngle;
	_crankAngle = _pd->system->getCrankAngle();

	float crankDelta = _crankAngle - _previousCrankAngle;

	// Normalize delta to -180 to 180 range
	if (crankDelta > 180.0f) {
		crankDelta -= 360.0f;
	} else if (crankDelta < -180.0f) {
		crankDelta += 360.0f;
	}

	// If crank rotated significantly, generate mouse wheel event
	if (fabs(crankDelta) > 15.0f) {
		event.type = Common::EVENT_WHEELUP;
		if (crankDelta < 0) {
			event.type = Common::EVENT_WHEELDOWN;
		}
		return true;
	}

	// Update timers
	((DefaultTimerManager *)getTimerManager())->handler();

	// Update mixer
	((NullMixerManager *)_mixerManager)->update(1);

	return false;
}

uint32 OSystem_Playdate::getMillis(bool skipRecord) {
	return _pd->system->getCurrentTimeMilliseconds() - _startTime;
}

void OSystem_Playdate::delayMillis(uint msecs) {
	uint32 endTime = getMillis() + msecs;
	while (getMillis() < endTime) {
		// Busy wait - could be improved with proper sleep
		// Playdate SDK doesn't have a sleep function in the same way
	}
}

void OSystem_Playdate::getTimeAndDate(TimeDate &td, bool skipRecord) const {
	// Get current time in seconds
	unsigned int seconds = _pd->system->getSecondsSinceEpoch(nullptr);

	// Convert to time structure
	// This is a simple implementation - could use proper time conversion
	td.tm_sec = seconds % 60;
	seconds /= 60;
	td.tm_min = seconds % 60;
	seconds /= 60;
	td.tm_hour = seconds % 24;
	seconds /= 24;

	// Days since epoch (1970-01-01)
	// This is simplified and doesn't account for leap years properly
	td.tm_mday = 1 + (seconds % 365);  // Approximate
	td.tm_mon = 0;  // Approximate
	td.tm_year = 70 + (seconds / 365);  // Years since 1900
	td.tm_wday = 0;  // Unknown
}

Common::MutexInternal *OSystem_Playdate::createMutex() {
	// Playdate is single-threaded, so use null mutex
	return new NullMutexInternal();
}

void OSystem_Playdate::logMessage(LogMessageType::Type type, const char *message) {
	// Log to Playdate console
	if (type == LogMessageType::kError) {
		_pd->system->error("[ERROR] %s", message);
	} else {
		_pd->system->logToConsole("%s", message);
	}
}

void OSystem_Playdate::quit() {
	// Playdate apps don't typically quit, but we can set a flag
	// The main loop will handle cleanup
}

void OSystem_Playdate::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	// Add Playdate-specific search paths
	// Engine data should be in the PDX bundle
	s.add("engine-data", new Common::FSDirectory("engine-data"), priority);
	s.add("themes", new Common::FSDirectory("themes"), priority);
}
