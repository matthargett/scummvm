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

#include "agi/playdate_menu.h"
#include "agi/agi.h"
#include "agi/graphics.h"
#include "agi/keyboard.h"
#include "agi/words.h"

namespace Agi {

PlaydateMenu::PlaydateMenu(AgiEngine *vm) :
	_vm(vm), _visible(true), _parserGame(false), _hasRoomLogicPhrase(false),
	_mode(kModeVerb), _verbId(0),
	_charIsNumber(false), _selectedIndex(0), _scrollOffset(0),
	_marqueeStart(0), _marqueeDir(1), _marqueeNextMs(0), _lastCrankMs(0) {
}

PlaydateMenu::~PlaydateMenu() {
}

void PlaydateMenu::show() {
	_visible = true;
}

void PlaydateMenu::hide() {
	_visible = false;
}

bool PlaydateMenu::isVisible() const {
	if (!_visible)
		return false;
	// The on-screen keyboard for a GetString/GetNumber prompt is always shown.
	bool isNumber;
	if (inCharInputLoop(isNumber))
		return true;
	if (!_parserGame)
		return false;
	// The word list auto-hides once the crank has been idle for a while, so
	// exploration gets the whole screen and the picker isn't distracting
	// chrome. Any crank movement brings it straight back (see handleEvent).
	return (_vm->_system->getMillis() - _lastCrankMs) < kCrankIdleHideMs;
}

bool PlaydateMenu::inCharInputLoop(bool &isNumber) const {
	if (!_vm->_game.cycleInnerLoopActive)
		return false;
	switch (_vm->_game.cycleInnerLoopType) {
	case CYCLE_INNERLOOP_GETSTRING:
		isNumber = false;
		return true;
	case CYCLE_INNERLOOP_GETNUMBER:
		isNumber = true;
		return true;
	default:
		return false;
	}
}

bool PlaydateMenu::claimsInput() const {
	if (!_visible)
		return false;
	bool isNumber;
	if (inCharInputLoop(isNumber))
		return true; // on-screen keyboard owns input during text entry
	// Word picker only takes input for parser games at the command prompt,
	// never during message boxes, menus or other inner loops.
	return _parserGame && !_vm->_game.cycleInnerLoopActive &&
	       _vm->promptIsEnabled();
}

void PlaydateMenu::resetContextWords() {
	_phrases.clear();
	_phraseFromLogic0.clear();
	// _hasRoomLogicPhrase is intentionally sticky across rooms: the game's
	// command-structure style does not change room to room.
	enterVerbMode();
}

void PlaydateMenu::addSaidPhrase(const uint16 *ids, uint count, bool fromLogic0) {
	// A said() phrase is "verb noun...". The picker can only offer
	// commands it can compose in full, so a phrase containing a wildcard
	// (1 = anyword, 9999 = rest-of-line) is dropped: anyword consumes a
	// parsed word the keyboard-less picker cannot supply, so offering the
	// verb alone would submit a command that fails the said() test.
	Common::Array<uint16> phrase;
	for (uint i = 0; i < count; ++i) {
		if (ids[i] == 1 || ids[i] == 9999)
			return;
		// Ignore-words (id 0) match nothing and are not stored in the
		// dictionary, so they cannot be shown or typed; skip them.
		if (ids[i] == 0)
			continue;
		phrase.push_back(ids[i]);
	}
	if (phrase.empty())
		return;

	// A said() from a room logic proves this game scopes commands per room, so
	// its logic-0 phrases are global clutter and get hidden (see phraseVisible).
	if (!fromLogic0)
		_hasRoomLogicPhrase = true;

	// Deduplicate identical phrases.
	for (uint i = 0; i < _phrases.size(); ++i) {
		if (_phrases[i].size() != phrase.size())
			continue;
		bool same = true;
		for (uint j = 0; j < phrase.size(); ++j) {
			if (_phrases[i][j] != phrase[j]) {
				same = false;
				break;
			}
		}
		if (same) {
			// Keep the more-visible source: a phrase also seen in a room logic
			// should not stay hidden as a logic-0 duplicate.
			if (!fromLogic0)
				_phraseFromLogic0[i] = false;
			return;
		}
	}

	_phrases.push_back(phrase);
	_phraseFromLogic0.push_back(fromLogic0);

	// Seeing any said() phrase proves this game uses the parser, so the
	// picker becomes active for the rest of the session.
	_parserGame = true;

	// A newly seen verb should appear in the verb list right away. But rebuild
	// without snapping the player's selection back to the top: rooms can keep
	// recording said() phrases for many cycles (idle animations, etc.), and
	// resetting the highlight each time would fight the crank as the player
	// navigates the verb list. Preserve the currently selected verb across the
	// rebuild.
	if (_mode == kModeVerb) {
		const uint16 keepId = (_selectedIndex >= 0 && _selectedIndex < (int)_listIds.size())
		                      ? _listIds[_selectedIndex] : 0xFFFF;
		enterVerbMode();
		if (keepId != 0xFFFF) {
			for (uint i = 0; i < _listIds.size(); ++i) {
				if (_listIds[i] == keepId) {
					_selectedIndex = (int)i;
					break;
				}
			}
			clampSelection();
		}
	}
}

void PlaydateMenu::enterVerbMode() {
	_mode = kModeVerb;
	_verbId = 0;
	_verbWord.clear();

	_listIds.clear();
	_listWords.clear();
	_listCommands.clear();

	// Distinct verbs (first id of each phrase), in first-seen order.
	for (uint i = 0; i < _phrases.size(); ++i) {
		if (!phraseVisible(i))
			continue;
		const uint16 id = _phrases[i][0];
		bool present = false;
		for (uint j = 0; j < _listIds.size(); ++j) {
			if (_listIds[j] == id) {
				present = true;
				break;
			}
		}
		if (present)
			continue;

		Common::String word = _vm->_words->firstWordForId(id);
		if (word.empty())
			continue;
		_listIds.push_back(id);
		_listWords.push_back(word);
	}

	_selectedIndex = 0;
	_scrollOffset = 0;
	resetMarquee();
}

Common::String PlaydateMenu::phraseText(const Common::Array<uint16> &ids, uint from) const {
	// Joins the words of a phrase (from index `from`) into a command
	// string. Returns empty if any id has no dictionary word.
	Common::String out;
	for (uint k = from; k < ids.size(); ++k) {
		Common::String word = _vm->_words->firstWordForId(ids[k]);
		if (word.empty())
			return Common::String();
		if (!out.empty())
			out += ' ';
		out += word;
	}
	return out;
}

// verbWord is taken BY VALUE on purpose. The caller passes _listWords[selected]
// (select() does enterNounMode(_listIds[i], _listWords[i])), and the first thing
// we do is _listWords.clear() - which frees that very element. A const-reference
// parameter would then dangle, and the "verb-only phrase" branch below
// (label = verbWord) would copy a freed String: harmless-looking on some
// allocators, but a null _str / assert-abort on others (e.g. the macOS
// simulator). Copying the argument up front sidesteps the lifetime problem.
void PlaydateMenu::enterNounMode(uint16 verbId, Common::String verbWord) {
	_mode = kModeNoun;
	_verbId = verbId;
	_verbWord = verbWord;

	_listIds.clear();
	_listWords.clear();
	_listCommands.clear();

	// One entry per complete phrase that starts with this verb. The label
	// shows the words after the verb (or the verb itself when the phrase
	// is the verb alone); selecting submits the entire phrase, so
	// multi-word said() tests like said("put","key","in","lock") are
	// satisfied in full.
	for (uint i = 0; i < _phrases.size(); ++i) {
		if (!phraseVisible(i))
			continue;
		if (_phrases[i][0] != verbId)
			continue;

		Common::String command = phraseText(_phrases[i], 0);
		if (command.empty())
			continue; // a word is missing from the dictionary

		bool present = false;
		for (uint j = 0; j < _listCommands.size(); ++j) {
			if (_listCommands[j] == command) {
				present = true;
				break;
			}
		}
		if (present)
			continue;

		Common::String label = phraseText(_phrases[i], 1);
		if (label.empty())
			label = verbWord; // verb-only phrase

		_listCommands.push_back(command);
		_listWords.push_back(label);
		_listIds.push_back(0);
	}

	_selectedIndex = 0;
	_scrollOffset = 0;
	resetMarquee();
}

void PlaydateMenu::enterCharMode(bool isNumber) {
	_mode = kModeChar;
	_charIsNumber = isNumber;

	_listIds.clear();
	_listWords.clear();
	_listCommands.clear();

	if (isNumber) {
		// GetNumber accepts digits only.
		for (char c = '0'; c <= '9'; ++c) {
			_listIds.push_back((uint16)c);
			_listWords.push_back(Common::String(c));
		}
	} else {
		// GetString accepts any printable character, so the keyboard must
		// offer more than letters: names may contain digits, and prompts
		// like the Leisure Suit Larry phone number ("555-8039") need digits
		// and a dash. Letters come first (the common case), then digits,
		// space and a little punctuation.
		for (char c = 'A'; c <= 'Z'; ++c) {
			_listIds.push_back((uint16)c);
			_listWords.push_back(Common::String(c));
		}
		for (char c = '0'; c <= '9'; ++c) {
			_listIds.push_back((uint16)c);
			_listWords.push_back(Common::String(c));
		}
		_listIds.push_back((uint16)' ');
		_listWords.push_back("Spc");
		static const char kPunct[] = { '-', '.', '\'' };
		for (uint i = 0; i < ARRAYSIZE(kPunct); ++i) {
			_listIds.push_back((uint16)kPunct[i]);
			_listWords.push_back(Common::String(kPunct[i]));
		}
	}

	// Editing and submission entries, common to both modes.
	_listIds.push_back(AGI_KEY_BACKSPACE);
	_listWords.push_back("Del");
	_listIds.push_back(AGI_KEY_ENTER);
	_listWords.push_back("Ent");

	_selectedIndex = 0;
	_scrollOffset = 0;
	resetMarquee();
}

void PlaydateMenu::syncCharMode() {
	bool isNumber;
	if (inCharInputLoop(isNumber)) {
		// Enter (or re-enter if GetString→GetNumber changed) char mode.
		if (_mode != kModeChar || _charIsNumber != isNumber)
			enterCharMode(isNumber);
	} else if (_mode == kModeChar) {
		// The text prompt ended; return to the word list.
		enterVerbMode();
	}
}

void PlaydateMenu::resetMarquee() {
	_marqueeStart = 0;
	_marqueeDir = 1;
	_marqueeNextMs = _vm->_system->getMillis() + kMarqueeStepMs;
}

void PlaydateMenu::moveSelection(int delta) {
	if (_listWords.empty())
		return;

	_selectedIndex = CLIP<int>(_selectedIndex + delta, 0, (int)_listWords.size() - 1);
	if (_selectedIndex < _scrollOffset)
		_scrollOffset = _selectedIndex;
	if (_selectedIndex >= _scrollOffset + kVisibleLines)
		_scrollOffset = _selectedIndex - (kVisibleLines - 1);

	resetMarquee();
}

void PlaydateMenu::clampSelection() {
	if (_selectedIndex >= (int)_listWords.size())
		_selectedIndex = MAX<int>(0, (int)_listWords.size() - 1);
	if (_scrollOffset > _selectedIndex)
		_scrollOffset = _selectedIndex;
	if (_scrollOffset < 0)
		_scrollOffset = 0;
}

void PlaydateMenu::injectCommand(const Common::String &command) {
	// The AGI key queue holds only KEY_QUEUE_SIZE-1 keys, far fewer than a
	// multi-word command plus ENTER. Buffer the command and let
	// feedPendingInput() drain it into the queue over as many cycles as it
	// takes, so it can never wrap and corrupt the queue. '\r' marks ENTER.
	_pendingInput += command;
	_pendingInput += '\r';
}

void PlaydateMenu::injectKey(uint16 key) {
	// A single keystroke for the on-screen keyboard. '\r' is the queue's
	// stand-in for ENTER, so map AGI_KEY_ENTER onto it and pass all other
	// keys (letters, digits, space, backspace) through verbatim.
	_pendingInput += (key == AGI_KEY_ENTER) ? '\r' : (char)key;
}

void PlaydateMenu::feedPendingInput() {
	while (!_pendingInput.empty()) {
		const int used = (_vm->_keyQueueEnd - _vm->_keyQueueStart + KEY_QUEUE_SIZE) % KEY_QUEUE_SIZE;
		if (used >= KEY_QUEUE_SIZE - 1)
			break; // queue full; resume next cycle

		const char c = _pendingInput[0];
		_pendingInput.deleteChar(0);
		_vm->_keyQueue[_vm->_keyQueueEnd++] = (c == '\r') ? AGI_KEY_ENTER : (uint16)(byte)c;
		_vm->_keyQueueEnd %= KEY_QUEUE_SIZE;
	}
}

void PlaydateMenu::select() {
	if (_mode == kModeChar) {
		if (_selectedIndex < 0 || _selectedIndex >= (int)_listIds.size())
			return;
		injectKey(_listIds[_selectedIndex]);
		return;
	}
	if (_mode == kModeVerb) {
		if (_selectedIndex < 0 || _selectedIndex >= (int)_listIds.size())
			return;
		enterNounMode(_listIds[_selectedIndex], _listWords[_selectedIndex]);

		// If the verb resolves to a single complete command (its only
		// phrase is the verb alone), submit it directly rather than
		// showing a one-item noun list.
		if (_listCommands.size() == 1 && _listCommands[0] == _verbWord) {
			injectCommand(_listCommands[0]);
			enterVerbMode();
		} else if (_listCommands.empty()) {
			enterVerbMode(); // nothing composable; stay on verbs
		}
	} else {
		if (_selectedIndex < 0 || _selectedIndex >= (int)_listCommands.size())
			return;
		injectCommand(_listCommands[_selectedIndex]);
		enterVerbMode();
	}
}

void PlaydateMenu::back() {
	if (_mode == kModeNoun)
		enterVerbMode();
}

bool PlaydateMenu::handleEvent(const Common::Event &event) {
	if (!_visible)
		return false;

	// Keep the mode in step with the engine: entering a GetString/GetNumber
	// loop turns the picker into an on-screen keyboard, leaving it restores
	// the word list.
	syncCharMode();

	switch (event.type) {
	case Common::EVENT_WHEELDOWN:
	case Common::EVENT_WHEELUP: {
		if (_listWords.empty())
			return false;
		bool isNumber;
		const bool charMode = inCharInputLoop(isNumber);
		const uint32 now = _vm->_system->getMillis();
		// A crank movement always un-hides the word list. The first notch after
		// it has been idle only reveals it (so a stray notch doesn't scroll a
		// list the player can't yet see); further notches then navigate. The
		// on-screen keyboard is always visible, so it navigates immediately.
		const bool wasHidden = !charMode && (now - _lastCrankMs) >= kCrankIdleHideMs;
		_lastCrankMs = now;
		if (wasHidden)
			return true;
		moveSelection(event.type == Common::EVENT_WHEELDOWN ? 1 : -1);
		return true;
	}
	case Common::EVENT_KEYDOWN:
		// A/B act only while the list is actually shown. While it is auto-hidden
		// they fall through to the game (e.g. A = Enter dismisses a message box).
		if (!isVisible())
			return false;
		if (event.kbd.keycode == Common::KEYCODE_RETURN && !_listWords.empty()) {
			select();
			return true;
		}
		if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
			// Escape only acts when it has somewhere to go back to, so
			// that in the verb list it can still reach the game.
			if (_mode == kModeNoun) {
				back();
				return true;
			}
		}
		return false;
	default:
		return false;
	}
}

Common::String PlaydateMenu::visibleLabel(int index, bool selected) {
	const Common::String &word = _listWords[index];
	if ((int)word.size() <= kMaxChars)
		return word;

	if (!selected) {
		// Ellipsize the tail of unselected long words.
		return Common::String(word.c_str(), kMaxChars - 1) + "\x85"; // ellipsis glyph
	}

	// The selected long word marquees so it can be read in full: a
	// kMaxChars window slides across it and back.
	const uint32 now = _vm->_system->getMillis();
	const int maxStart = (int)word.size() - kMaxChars;
	if (now >= _marqueeNextMs) {
		_marqueeNextMs = now + kMarqueeStepMs;
		_marqueeStart += _marqueeDir;
		if (_marqueeStart >= maxStart) {
			_marqueeStart = maxStart;
			_marqueeDir = -1;
		} else if (_marqueeStart <= 0) {
			_marqueeStart = 0;
			_marqueeDir = 1;
		}
	}
	const int start = CLIP<int>(_marqueeStart, 0, maxStart);
	return Common::String(word.c_str() + start, kMaxChars);
}

void PlaydateMenu::draw() {
	if (!_visible)
		return;

	// Match the picker's contents to the current input mode before drawing.
	syncCharMode();

	GfxMgr *gfx = _vm->_gfx;

	// Clear the menu column.
	gfx->drawDisplayRectPlaydate(kMenuX, 0, kMenuWidth, 240, 0);

	int y = 0;
	for (int i = 0; i < kVisibleLines; ++i) {
		const int index = _scrollOffset + i;
		if (index >= (int)_listWords.size())
			break;

		const bool selected = (index == _selectedIndex);
		const Common::String label = visibleLabel(index, selected);

		if (selected) {
			gfx->drawDisplayRectPlaydate(kMenuX, y, kMenuWidth, kLineHeight, 1);
			gfx->drawPlaydateText(kTextX, y + 1, label, true);
		} else {
			gfx->drawPlaydateText(kTextX, y + 1, label, false);
		}

		y += kLineHeight;
	}
}

} // End of namespace Agi
