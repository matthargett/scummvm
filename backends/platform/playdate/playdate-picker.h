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

#ifndef PLATFORM_PLAYDATE_PICKER_H
#define PLATFORM_PLAYDATE_PICKER_H

#include "common/scummsys.h"
#include "common/str.h"
#include "common/str-array.h"

namespace Playdate {

/**
 * Supplies the entries of the crank word picker. Implemented by an engine
 * and registered with setPickerSource(); the backend draws the list beside
 * the game screen and types the player's choices as key events.
 */
class PickerSource {
public:
	virtual ~PickerSource() {}

	/** Whether the list is shown at all. */
	virtual bool pickerVisible() = 0;
	/** Whether the crank and A/B drive the list right now, rather than the game. */
	virtual bool pickerAcceptsInput() = 0;
	/** Entries to show. pickerRevision() must change whenever they do. */
	virtual const Common::StringArray &pickerWords() = 0;
	virtual uint pickerRevision() = 0;
	/** The highlighted entry is kept while the words of one list change, and reset for another list. */
	virtual uint pickerList() = 0;
	/** Entry chosen with A. Returns the text to type: '\r' is Return, '\b' Backspace. */
	virtual Common::String pickerSelect(uint index) = 0;
	/** B pressed. Returns false if there was nothing to go back to. */
	virtual bool pickerBack() = 0;
};

/** Registers the active source; nullptr removes it. */
void setPickerSource(PickerSource *source);

/** The list UI. Used by the Playdate graphics manager and event source only. */
class WordPicker {
public:
	static WordPicker &instance();

	void setSource(PickerSource *source);

	bool visible();
	bool acceptsInput();

	void move(int delta);
	/** Returns false if there was nothing to select. */
	bool select(Common::String &typed);
	bool back();

	/**
	 * Draws the list into columns [x, x + width) of a 1-bit LCD frame.
	 * Returns false if nothing changed since the last call and force is unset.
	 */
	bool draw(uint8 *frame, int rowBytes, int x, int width, int height, bool force, uint32 nowMs);

private:
	WordPicker();

	void sync(bool keepLabel);
	Common::String label(uint index, int maxChars, uint32 nowMs);

	PickerSource *_source;
	uint _revision;
	uint _list;
	Common::StringArray _words;
	int _selected;
	int _scroll;
	bool _accepting;
	bool _dirty;

	// The selected entry scrolls sideways when it is too long for the column
	int _marqueeStart;
	int _marqueeDir;
	uint32 _marqueeNextMs;
};

} // End of namespace Playdate

#endif
