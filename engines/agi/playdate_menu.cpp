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
#include "agi/keyboard.h" // For KEY_QUEUE_SIZE if needed, though it's a macro in header
#include "agi/text.h"
#include "agi/words.h"

namespace Agi {

PlaydateMenu::PlaydateMenu(AgiEngine *vm) : _vm(vm), _visible(true), _initialized(false), _selectedIndex(0), _scrollOffset(0) {
	populateWords();
}

PlaydateMenu::~PlaydateMenu() {
}

void PlaydateMenu::populateWords() {
	if (_initialized || !_vm || !_vm->_words)
		return;

	_vm->_words->collectAllWords(_allWords);
	_selectedIndex = 0;
	_scrollOffset = 0;
	_initialized = true;
}

void PlaydateMenu::rebuildContextWords() {
	_contextWords.clear();

	if (!_vm || !_vm->_words || _contextWordIds.empty())
		return;

	_vm->_words->collectWordsForIds(_contextWordIds, _contextWords);
}

void PlaydateMenu::resetContextWords() {
	_contextWordIds.clear();
	_contextWords.clear();
	_selectedIndex = 0;
	_scrollOffset = 0;
}

void PlaydateMenu::addContextWordIds(const Common::Array<uint16> &ids) {
	if (ids.empty())
		return;

	for (uint i = 0; i < ids.size(); ++i) {
		const uint16 id = ids[i];
		if (id == 0)
			continue;
		bool present = false;
		for (uint j = 0; j < _contextWordIds.size(); ++j) {
			if (_contextWordIds[j] == id) {
				present = true;
				break;
			}
		}
		if (!present)
			_contextWordIds.push_back(id);
	}

	rebuildContextWords();
	clampSelection();
	if (_visible)
		draw();
}

void PlaydateMenu::show() {
	populateWords();
	_visible = true;
	draw();
	if (_vm)
		warning("PlaydateMenu show: total=%u context=%u", (uint)_allWords.size(), (uint)_contextWords.size());
}

void PlaydateMenu::hide() {
	_visible = false;
}

bool PlaydateMenu::isVisible() const {
	return _visible;
}

void PlaydateMenu::draw() {
	if (!_visible)
		return;

	populateWords();

	const Common::Array<Common::String> &words = activeWordList();
	warning("PlaydateMenu draw: words=%u selected=%d scroll=%d", (uint)words.size(), _selectedIndex, _scrollOffset);

	// Clear menu area (Right side: 320,0 to 400,240)
	_vm->_gfx->drawDisplayRectPlaydate(kMenuX, 0, kMenuWidth, 240, 0); // 0 = Black

	int y = 0;
	for (int i = 0; i < kVisibleLines; i++) {
		int wordIndex = _scrollOffset + i;
		if (wordIndex >= (int)words.size())
			break;

		bool selected = (wordIndex == _selectedIndex);
		Common::String word = words[wordIndex];

		// Truncate if too long (approx 10 chars fit in 80px with 8px font)
		if (word.size() > 10) {
			word = word.substr(0, 9) + ".";
		}

		if (selected) {
			// Draw inverted background for selection
			_vm->_gfx->drawDisplayRectPlaydate(kMenuX, y, kMenuWidth, kLineHeight, 15); // 15 = White
			_vm->_gfx->drawPlaydateText(kMenuX + 2, y + 1, word, true);                 // Inverted text
		} else {
			_vm->_gfx->drawPlaydateText(kMenuX + 2, y + 1, word, false); // Normal text
		}

		y += kLineHeight;
	}
}

void PlaydateMenu::handleEvent(const Common::Event &event) {
	if (!_visible)
		return;

	if (_allWords.empty())
		return;

	const Common::Array<Common::String> &words = activeWordList();

	if (event.type == Common::EVENT_WHEELDOWN) {
		// Scroll Down
		if (_selectedIndex < (int)words.size() - 1) {
			_selectedIndex++;
			if (_selectedIndex >= _scrollOffset + kVisibleLines) {
				_scrollOffset++;
			}
			draw();
			warning("PlaydateMenu wheel down sel=%d scroll=%d", _selectedIndex, _scrollOffset);
		}
	} else if (event.type == Common::EVENT_WHEELUP) {
		// Scroll Up
		if (_selectedIndex > 0) {
			_selectedIndex--;
			if (_selectedIndex < _scrollOffset) {
				_scrollOffset--;
			}
			draw();
			warning("PlaydateMenu wheel up sel=%d scroll=%d", _selectedIndex, _scrollOffset);
		}
	} else if (event.type == Common::EVENT_KEYDOWN) {
		if (event.kbd.keycode == Common::KEYCODE_RETURN) {
			// Select word
			if (_selectedIndex >= 0 && _selectedIndex < (int)words.size()) {
				Common::String word = words[_selectedIndex];

				// Inject word into input line
				for (uint i = 0; i < word.size(); i++) {
					_vm->_keyQueue[_vm->_keyQueueEnd++] = word[i];
					_vm->_keyQueueEnd %= KEY_QUEUE_SIZE;
				}
				// Append space
				_vm->_keyQueue[_vm->_keyQueueEnd++] = ' ';
				_vm->_keyQueueEnd %= KEY_QUEUE_SIZE;
				warning("PlaydateMenu selected word '%s'", word.c_str());
			}
		}
	}
}

const Common::Array<Common::String> &PlaydateMenu::activeWordList() const {
	return _contextWords.empty() ? _allWords : _contextWords;
}

void PlaydateMenu::clampSelection() {
	const Common::Array<Common::String> &words = activeWordList();
	if (_selectedIndex >= (int)words.size())
		_selectedIndex = MAX<int>(0, (int)words.size() - 1);
	if (_scrollOffset > _selectedIndex)
		_scrollOffset = _selectedIndex;
	if (_scrollOffset < 0)
		_scrollOffset = 0;
}

} // End of namespace Agi
