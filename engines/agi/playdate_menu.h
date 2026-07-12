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

	/**
	 * True when the picker should receive input this cycle: either a
	 * parser game with the prompt active, or any game currently inside a
	 * GetString/GetNumber inner loop (where the picker turns into an
	 * on-screen keyboard). Used by the keyboard handler to decide whether
	 * to route events to the picker.
	 */
	bool claimsInput() const;

	/**
	 * True once the game has been seen to use the parser (any said()
	 * test). Menu- and pointer-driven games (Donald Duck's Playground,
	 * Mixed-Up Mother Goose, ...) never set this, so the picker stays
	 * hidden and the game keeps the full width of the screen.
	 */
	bool isParserGame() const { return _parserGame; }

	/** Clears the recorded room vocabulary. Called on room changes. */
	void resetContextWords();

	/** Records a said() phrase (verb id first, then noun ids). */
	void addSaidPhrase(const uint16 *ids, uint count);

	void draw();

	/** Feeds any buffered picker command into the AGI key queue as space
	 *  allows. Called once per cycle so a long command never overflows
	 *  the queue. */
	void feedPendingInput();

	/** Handles an event; returns true if it was consumed by the picker. */
	bool handleEvent(const Common::Event &event);

private:
	enum Mode {
		kModeVerb,
		kModeNoun,
		kModeChar // on-screen keyboard during GetString/GetNumber
	};

	void enterVerbMode();
	void enterNounMode(uint16 verbId, const Common::String &verbWord);
	void enterCharMode(bool isNumber);
	void select();
	void back();
	void moveSelection(int delta);
	void clampSelection();
	void resetMarquee();
	void injectCommand(const Common::String &command);
	void injectKey(uint16 key);
	Common::String phraseText(const Common::Array<uint16> &ids, uint from) const;
	Common::String visibleLabel(int index, bool selected);

	/** True while the engine is inside a GetString/GetNumber inner loop.
	 *  Sets isNumber for GetNumber (digits only). */
	bool inCharInputLoop(bool &isNumber) const;

	/** Enters/leaves char mode to match the current inner-loop state. */
	void syncCharMode();

	AgiEngine *_vm;
	bool _visible;
	bool _parserGame; // sticky: set once any said() phrase is recorded

	// Recorded room vocabulary. Each phrase is the full said() word-group
	// id sequence with the verb first. Only phrases the picker can fully
	// compose are kept: those containing a wildcard (anyword / rest-of-
	// line), which need a word the keyboard-less picker cannot supply, are
	// dropped, so every offered command satisfies its said() test.
	Common::Array<Common::Array<uint16> > _phrases;

	Mode _mode;
	uint16 _verbId;            // selected verb (in noun mode)
	Common::String _verbWord;  // its canonical word
	bool _charIsNumber;        // char mode: digits-only (GetNumber)

	// The list currently shown. In verb mode _listIds holds verb ids; in
	// noun mode _listCommands holds the full command each entry submits.
	Common::Array<uint16> _listIds;
	Common::Array<Common::String> _listWords;    // display labels
	Common::Array<Common::String> _listCommands; // full commands (noun mode)
	int _selectedIndex;
	int _scrollOffset;

	// Command waiting to be fed into the AGI key queue, terminated by a
	// carriage return that stands in for ENTER.
	Common::String _pendingInput;

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
