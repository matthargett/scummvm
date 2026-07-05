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

/**
 * On-screen word picker for the Playdate, shown in the right margin
 * next to the game.
 *
 * AGI parser games are played without a keyboard by composing commands
 * from this list. The words offered are scoped to the current room:
 * every time a room's logic runs a said() test, the words it checks for
 * are recorded. A said() phrase is "verb noun...", so the picker first
 * offers the room's verbs; once a verb is chosen it offers the nouns
 * that the room actually pairs with that verb, and B returns to the
 * verb list. Selecting a noun (or a verb with no nouns) types the
 * command at the prompt and submits it.
 */
class PlaydateMenu {
public:
	PlaydateMenu(AgiEngine *vm);
	~PlaydateMenu();

	void show();
	void hide();
	bool isVisible() const;

	/** Clears the recorded room vocabulary. Called on room changes. */
	void resetContextWords();

	/** Records a said() phrase (verb id first, then noun ids). */
	void addSaidPhrase(const uint16 *ids, uint count);

	void draw();

	/** Handles an event; returns true if it was consumed by the picker. */
	bool handleEvent(const Common::Event &event);

private:
	enum Mode {
		kModeVerb,
		kModeNoun
	};

	void enterVerbMode();
	void enterNounMode(uint16 verbId, const Common::String &verbWord);
	void select();
	void back();
	void moveSelection(int delta);
	void clampSelection();
	void resetMarquee();
	void injectCommand(const Common::String &command);
	Common::String visibleLabel(int index, bool selected);

	AgiEngine *_vm;
	bool _visible;

	// Recorded room vocabulary. Each phrase is a said() word-group id
	// sequence with the verb first; wildcard ids (anyword / rest-of-line)
	// are dropped.
	Common::Array<Common::Array<uint16> > _phrases;

	Mode _mode;
	uint16 _verbId;            // selected verb (in noun mode)
	Common::String _verbWord;  // its canonical word

	// The list currently shown (verbs or nouns for the chosen verb).
	Common::Array<uint16> _listIds;
	Common::Array<Common::String> _listWords;
	int _selectedIndex;
	int _scrollOffset;

	// Marquee state for the selected long word.
	int _marqueeStart;
	int _marqueeDir;
	uint32 _marqueeNextMs;

	static const int kLineHeight = 10;
	static const int kVisibleLines = 24; // 240 / 10
	static const int kMenuX = 320;
	static const int kMenuWidth = 80;
	static const int kTextX = kMenuX + 2;
	static const int kMaxChars = 9;      // characters that fit in the column
	static const uint32 kMarqueeStepMs = 350;
};

} // End of namespace Agi

#endif /* AGI_PLAYDATE_MENU_H */
