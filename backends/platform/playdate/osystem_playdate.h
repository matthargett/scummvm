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

#ifndef PLATFORM_PLAYDATE_OSYSTEM_PLAYDATE_H
#define PLATFORM_PLAYDATE_OSYSTEM_PLAYDATE_H

#include "backends/modular-backend.h"

typedef struct PlaydateAPI PlaydateAPI;

class PlaydateEventSource;

/**
 * OSystem implementation for the Panic Playdate handheld.
 *
 * The Playdate has a 400x240 1-bit display, a d-pad, two buttons
 * (A and B), an analog crank, and 16 MB of RAM. All hardware access
 * goes through the PlaydateAPI function table provided by the OS.
 */
class OSystem_Playdate final : public ModularMixerBackend, public ModularGraphicsBackend {
public:
	explicit OSystem_Playdate(PlaydateAPI *pd);
	~OSystem_Playdate() override;

	void initBackend() override;

	Common::MutexInternal *createMutex() override;
	uint32 getMillis(bool skipRecord = false) override;
	void delayMillis(uint msecs) override;
	void getTimeAndDate(TimeDate &td, bool skipRecord = false) const override;

	void quit() override;
	void fatalError() override;
	void logMessage(LogMessageType::Type type, const char *message) override;

	void addSysArchivesToSearchSet(Common::SearchSet &s, int priority) override;

	/**
	 * Services the timer manager and the audio ring buffer. Called
	 * from PlaydateEventSource::pollEvent() and delayMillis(), which
	 * are the only places the blocking ScummVM main loop regularly
	 * passes through.
	 */
	void updateSubsystems();

	PlaydateAPI *getPlaydateAPI() const { return _pd; }

private:
	PlaydateAPI *_pd;
	PlaydateEventSource *_eventSource;
	uint32 _startTime;
};

#endif
