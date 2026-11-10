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

#ifndef BACKENDS_EVENTS_PLAYDATE_H
#define BACKENDS_EVENTS_PLAYDATE_H

#include "common/events.h"
#include "common/queue.h"
#include "common/str.h"

typedef struct PlaydateAPI PlaydateAPI;

class OSystem_Playdate;

/**
 * Event source translating the Playdate controls to ScummVM events.
 *
 * Two mappings are used:
 *
 * Key mode (default in game): the d-pad generates arrow key events,
 * A generates Return and B generates Escape. This drives AGI ego
 * movement, menus, and the Manhunter crosshair directly.
 *
 * Pointer mode (automatic while the GUI overlay is visible, or toggled
 * in game by holding B): the d-pad moves the mouse cursor with
 * acceleration, A generates left clicks and B generates right clicks.
 *
 * While the word picker takes input, the crank scrolls it, A types the
 * highlighted entry as key presses and B steps back. Otherwise the crank
 * generates mouse wheel events.
 */
class PlaydateEventSource : public Common::EventSource {
public:
	explicit PlaydateEventSource(OSystem_Playdate *system);

	bool pollEvent(Common::Event &event) override;

private:
	void queueKey(Common::KeyCode keycode, uint16 ascii, bool down);
	void queueKeyPress(Common::KeyCode keycode, uint16 ascii);
	void queueMouseButton(Common::EventType type);
	void updatePointer(uint32 buttons, uint32 pushed);
	void handleCrank();
	void typePending();
	bool pointerModeActive() const;
	Common::Point clampMouse(int x, int y) const;

	OSystem_Playdate *_system;
	PlaydateAPI *_pd;

	Common::Queue<Common::Event> _queue;

	// Virtual mouse pointer
	int _mouseX, _mouseY;
	float _mouseSpeed;
	bool _pointerMode;
	uint32 _pointerLastMoveMs; // throttles cursor movement to one step per frame

	// B button hold detection (short press = Escape / right click,
	// long press = toggle pointer mode)
	uint32 _buttonBDownTime;
	bool _buttonBHeld;
	bool _buttonBLongPressFired;

	// Crank accumulator, in degrees, plus the last absolute angle so the
	// per-poll delta is computed from position rather than getCrankChange()
	// (which repeats within a frame and would flood WHEEL events).
	float _crankAccum;
	float _prevCrankAngle;

	// Previous raw button bitmask, so button edges are derived from the
	// instantaneous state across every poll rather than from getButtonState's
	// per-frame pushed/released (which repeat on every call within a frame).
	uint32 _prevButtons;

	// Word picker choice still to be typed, and whether the picker took the
	// current A press (so its release is not sent as a stray key-up)
	Common::String _typeQueue;
	bool _buttonAConsumed;
	uint32 _typeLastMs;
};

#endif
