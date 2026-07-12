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

#include "backends/events/playdate/playdate-events.h"

#include "backends/graphics/playdate/playdate-graphics.h"
#include "backends/platform/playdate/osystem_playdate.h"

#include "backends/platform/playdate/playdate-sdk.h"

// Degrees of crank rotation per mouse wheel notch
static const float kCrankDegreesPerNotch = 30.0f;

// Milliseconds the B button must be held to toggle pointer mode
static const uint32 kLongPressMs = 500;

// Virtual mouse acceleration
static const float kMouseSpeedMin = 1.0f;
static const float kMouseSpeedMax = 5.0f;
static const float kMouseAccel = 0.25f;

PlaydateEventSource::PlaydateEventSource(OSystem_Playdate *system)
	: _system(system),
	  _pd(system->getPlaydateAPI()),
	  _mouseX(LCD_COLUMNS / 2),
	  _mouseY(LCD_ROWS / 2),
	  _mouseSpeed(kMouseSpeedMin),
	  _pointerMode(false),
	  _buttonBDownTime(0),
	  _buttonBHeld(false),
	  _buttonBLongPressFired(false),
	  _crankAccum(0.0f) {
}

bool PlaydateEventSource::pollEvent(Common::Event &event) {
	_system->updateSubsystems();

	if (!_queue.empty()) {
		event = _queue.pop();
		return true;
	}

	PDButtons current, pushed, released;
	_pd->system->getButtonState(&current, &pushed, &released);

	const bool pointer = pointerModeActive();

	if (pointer) {
		updatePointer(current, pushed);

		if (pushed & kButtonA)
			queueMouseButton(Common::EVENT_LBUTTONDOWN);
		if (released & kButtonA)
			queueMouseButton(Common::EVENT_LBUTTONUP);
	} else {
		static const struct {
			PDButtons button;
			Common::KeyCode keycode;
		} dpadMap[] = {
			{ kButtonUp,    Common::KEYCODE_UP },
			{ kButtonDown,  Common::KEYCODE_DOWN },
			{ kButtonLeft,  Common::KEYCODE_LEFT },
			{ kButtonRight, Common::KEYCODE_RIGHT },
		};

		for (uint i = 0; i < ARRAYSIZE(dpadMap); i++) {
			if (pushed & dpadMap[i].button)
				queueKey(dpadMap[i].keycode, 0, true);
			if (released & dpadMap[i].button)
				queueKey(dpadMap[i].keycode, 0, false);
		}

		if (pushed & kButtonA)
			queueKey(Common::KEYCODE_RETURN, Common::ASCII_RETURN, true);
		if (released & kButtonA)
			queueKey(Common::KEYCODE_RETURN, Common::ASCII_RETURN, false);
	}

	// B button: short press acts, long press toggles pointer mode
	if (pushed & kButtonB) {
		_buttonBHeld = true;
		_buttonBLongPressFired = false;
		_buttonBDownTime = _system->getMillis();
	}
	if (_buttonBHeld && !_buttonBLongPressFired &&
	    (_system->getMillis() - _buttonBDownTime) >= kLongPressMs) {
		_buttonBLongPressFired = true;
		_pointerMode = !_pointerMode;
	}
	if (released & kButtonB) {
		if (_buttonBHeld && !_buttonBLongPressFired) {
			if (pointer && !_system->getGraphicsManager()->isOverlayVisible()) {
				// In-game pointer mode: right click
				queueMouseButton(Common::EVENT_RBUTTONDOWN);
				queueMouseButton(Common::EVENT_RBUTTONUP);
			} else {
				queueKeyPress(Common::KEYCODE_ESCAPE, Common::ASCII_ESCAPE);
			}
		}
		_buttonBHeld = false;
		_buttonBLongPressFired = false;
	}

	handleCrank();

	if (!_queue.empty()) {
		event = _queue.pop();
		return true;
	}
	return false;
}

bool PlaydateEventSource::pointerModeActive() const {
	// Key mode is the default everywhere: the launcher and ScummVM's
	// dialogs are all keyboard navigable, and the targeted AGI games
	// are keyboard driven. Pointer mode is opt-in (hold B), for the
	// point-and-click SCI games and the occasional mouse-only widget.
	return _pointerMode;
}

Common::Point PlaydateEventSource::clampMouse(int x, int y) const {
	GraphicsManager *gfx = _system->getGraphicsManager();
	int16 w, h;
	if (gfx->isOverlayVisible()) {
		w = gfx->getOverlayWidth();
		h = gfx->getOverlayHeight();
	} else {
		w = gfx->getWidth();
		h = gfx->getHeight();
	}
	return Common::Point(CLIP<int>(x, 0, w - 1), CLIP<int>(y, 0, h - 1));
}

void PlaydateEventSource::updatePointer(uint32 buttons, uint32 pushed) {
	int dx = 0, dy = 0;
	if (buttons & kButtonLeft)
		dx--;
	if (buttons & kButtonRight)
		dx++;
	if (buttons & kButtonUp)
		dy--;
	if (buttons & kButtonDown)
		dy++;

	if (dx == 0 && dy == 0) {
		_mouseSpeed = kMouseSpeedMin;
		return;
	}

	_mouseSpeed = MIN(_mouseSpeed + kMouseAccel, kMouseSpeedMax);

	const int step = (int)_mouseSpeed;
	Common::Point p = clampMouse(_mouseX + dx * step, _mouseY + dy * step);
	if (p.x == _mouseX && p.y == _mouseY)
		return;

	_mouseX = p.x;
	_mouseY = p.y;

	((PlaydateGraphicsManager *)_system->getGraphicsManager())->warpMouse(_mouseX, _mouseY);

	Common::Event event;
	event.type = Common::EVENT_MOUSEMOVE;
	event.mouse = p;
	_queue.push(event);
}

void PlaydateEventSource::handleCrank() {
	_crankAccum += _pd->system->getCrankChange();

	while (_crankAccum >= kCrankDegreesPerNotch) {
		_crankAccum -= kCrankDegreesPerNotch;
		Common::Event event;
		event.type = Common::EVENT_WHEELDOWN;
		event.mouse = Common::Point(_mouseX, _mouseY);
		_queue.push(event);
	}
	while (_crankAccum <= -kCrankDegreesPerNotch) {
		_crankAccum += kCrankDegreesPerNotch;
		Common::Event event;
		event.type = Common::EVENT_WHEELUP;
		event.mouse = Common::Point(_mouseX, _mouseY);
		_queue.push(event);
	}
}

void PlaydateEventSource::queueKey(Common::KeyCode keycode, uint16 ascii, bool down) {
	Common::Event event;
	event.type = down ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
	event.kbd = Common::KeyState(keycode, ascii);
	_queue.push(event);
}

void PlaydateEventSource::queueKeyPress(Common::KeyCode keycode, uint16 ascii) {
	queueKey(keycode, ascii, true);
	queueKey(keycode, ascii, false);
}

void PlaydateEventSource::queueMouseButton(Common::EventType type) {
	Common::Event event;
	event.type = type;
	event.mouse = Common::Point(_mouseX, _mouseY);
	_queue.push(event);
}
