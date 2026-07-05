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
	// Keep only real words, preserving order; the first survivor is the
	// verb and the rest are nouns. Wildcards (1 = anyword, 9999 =
	// rest-of-line) carry no word to display.
	Common::Array<uint16> phrase;
	for (uint i = 0; i < count; ++i) {
		if (ids[i] != 1 && ids[i] != 9999)
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

void PlaydateMenu::enterNounMode(uint16 verbId, const Common::String &verbWord) {
	_mode = kModeNoun;
	_verbId = verbId;
	_verbWord = verbWord;

	_listIds.clear();
	_listWords.clear();

	// Distinct nouns paired with this verb across the room's phrases.
	for (uint i = 0; i < _phrases.size(); ++i) {
		if (_phrases[i][0] != verbId)
			continue;
		for (uint k = 1; k < _phrases[i].size(); ++k) {
			const uint16 id = _phrases[i][k];
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
	for (uint i = 0; i < command.size(); ++i) {
		_vm->_keyQueue[_vm->_keyQueueEnd++] = command[i];
		_vm->_keyQueueEnd %= KEY_QUEUE_SIZE;
	}
	// Submit the line.
	_vm->_keyQueue[_vm->_keyQueueEnd++] = AGI_KEY_ENTER;
	_vm->_keyQueueEnd %= KEY_QUEUE_SIZE;
}

void PlaydateMenu::select() {
	if (_selectedIndex < 0 || _selectedIndex >= (int)_listWords.size())
		return;

	const uint16 id = _listIds[_selectedIndex];
	const Common::String word = _listWords[_selectedIndex];

	if (_mode == kModeVerb) {
		enterNounMode(id, word);
		// A verb the room never pairs with a noun is a complete command
		// on its own (e.g. "look", "inventory").
		if (_listWords.empty()) {
			injectCommand(word);
			enterVerbMode();
		}
	} else {
		injectCommand(_verbWord + " " + word);
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
