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
	_vm(vm), _visible(true), _parserGame(false), _mode(kModeVerb), _verbId(0),
	_selectedIndex(0), _scrollOffset(0),
	_marqueeStart(0), _marqueeDir(1), _marqueeNextMs(0) {
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
	// Only parser games show the picker (and reserve space for it).
	return _visible && _parserGame;
}

void PlaydateMenu::resetContextWords() {
	_phrases.clear();
	enterVerbMode();
}

void PlaydateMenu::addSaidPhrase(const uint16 *ids, uint count) {
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
		if (same)
			return;
	}

	_phrases.push_back(phrase);

	// Seeing any said() phrase proves this game uses the parser, so the
	// picker becomes active for the rest of the session.
	_parserGame = true;

	// A newly seen verb should appear in the verb list right away.
	if (_mode == kModeVerb)
		enterVerbMode();
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

void PlaydateMenu::enterNounMode(uint16 verbId, const Common::String &verbWord) {
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

	switch (event.type) {
	case Common::EVENT_WHEELDOWN:
		if (_listWords.empty())
			return false;
		moveSelection(1);
		return true;
	case Common::EVENT_WHEELUP:
		if (_listWords.empty())
			return false;
		moveSelection(-1);
		return true;
	case Common::EVENT_KEYDOWN:
		// Only claim keys when there is something to act on; title
		// screens and cutscenes wait for ENTER themselves.
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
