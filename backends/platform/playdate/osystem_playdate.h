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

#ifndef BACKENDS_PLATFORM_PLAYDATE_OSYSTEM_PLAYDATE_H
#define BACKENDS_PLATFORM_PLAYDATE_OSYSTEM_PLAYDATE_H

#include "backends/modular-backend.h"
#include "backends/platform/playdate/playdate-graphics.h"
#include "pd_api.h"

/**
 * OSystem implementation for Panic Playdate handheld console.
 *
 * Features:
 * - 400x240 1-bit monochrome display
 * - D-pad, A, B buttons + analog crank
 * - 44.1kHz stereo audio
 * - ARM Cortex-M7 processor
 */
class OSystem_Playdate : public ModularMixerBackend, public ModularGraphicsBackend, Common::EventSource {
public:
	OSystem_Playdate(PlaydateAPI *pd);
	virtual ~OSystem_Playdate();

	// Backend initialization
	void initBackend() override;

	// Events
	bool pollEvent(Common::Event &event) override;

	// Timing
	uint32 getMillis(bool skipRecord = false) override;
	void delayMillis(uint msecs) override;
	void getTimeAndDate(TimeDate &td, bool skipRecord = false) const override;

	// Mutexes
	Common::MutexInternal *createMutex() override;

	// Logging
	void logMessage(LogMessageType::Type type, const char *message) override;

	// Quit
	void quit() override;

	// File system
	void addSysArchivesToSearchSet(Common::SearchSet &s, int priority) override;

	// Get Playdate API pointer
	PlaydateAPI *getPlaydateAPI() { return _pd; }

private:
	PlaydateAPI *_pd;
	uint32 _startTime;

	// Button state tracking
	PDButtons _currentButtons;
	PDButtons _previousButtons;

	// Crank state tracking
	float _crankAngle;
	float _previousCrankAngle;

	// Helper methods
	void processButtonEvent(Common::Event &event, PDButtons button, Common::KeyCode key);
	void processCrankEvent(Common::Event &event);
};

#endif
