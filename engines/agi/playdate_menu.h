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

#ifndef AGI_PLAYDATE_MENU_H
#define AGI_PLAYDATE_MENU_H

#include "common/array.h"
#include "common/events.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Agi {

class AgiEngine;

class PlaydateMenu {
public:
	PlaydateMenu(AgiEngine *vm);
	~PlaydateMenu();

	void show();
	void hide();
	bool isVisible() const;
	void resetContextWords();
	void addContextWordIds(const Common::Array<uint16> &ids);
	void draw();
	void handleEvent(const Common::Event &event);

private:
	void populateWords();
	void rebuildContextWords();
	const Common::Array<Common::String> &activeWordList() const;
	void clampSelection();

	AgiEngine *_vm;
	bool _visible;
	bool _initialized;
	Common::Array<Common::String> _allWords;
	Common::Array<uint16> _contextWordIds;
	Common::Array<Common::String> _contextWords;
	int _selectedIndex;
	int _scrollOffset;

	// UI Constants
	static const int kLineHeight = 10;
	static const int kVisibleLines = 24; // 240 / 10
	static const int kMenuX = 320;
	static const int kMenuWidth = 80;
};

} // End of namespace Agi

#endif /* AGI_PLAYDATE_MENU_H */
