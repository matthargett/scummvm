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

#include "backends/platform/playdate/playdate-picker.h"

#include "common/tokenizer.h"
#include "common/util.h"
#include "graphics/fonts/dosfont.h"

namespace Playdate {

static const int kLineHeight = 10;
static const int kGlyphSize = 8;
static const int kTextMargin = 3;
static const uint32 kMarqueeStepMs = 350;

// The games' vocabularies include these; they are kept out of a list that
// is on screen all the time.
bool isProfanity(const Common::String &text) {
	static const char *const kAnywhere[] = { "fuck", "shit" };
	static const char *const kWords[] = {
		"ass", "asshole", "bitch", "cock", "cocksucker", "cunt", "dick",
		"dickhead", "fag", "faggot", "piss", "pussy", "slut", "tits", "whore",
		"whores"
	};
	static const char *const kPhrases[] = { "blow job", "blow me", "jerk off", "suck me" };

	Common::String lower(text);
	lower.toLowercase();
	lower.trim();

	for (uint i = 0; i < ARRAYSIZE(kAnywhere); i++) {
		if (lower.contains(kAnywhere[i]))
			return true;
	}
	for (uint i = 0; i < ARRAYSIZE(kPhrases); i++) {
		if (lower.contains(kPhrases[i]))
			return true;
	}

	Common::StringTokenizer tokenizer(lower, " ");
	while (!tokenizer.empty()) {
		const Common::String word = tokenizer.nextToken();
		for (uint i = 0; i < ARRAYSIZE(kWords); i++) {
			if (word == kWords[i])
				return true;
		}
	}
	return false;
}

void setPickerSource(PickerSource *source) {
	WordPicker::instance().setSource(source);
}

// Never destroyed: the device build has no support for static destructors
static WordPicker *s_picker = nullptr;

WordPicker &WordPicker::instance() {
	if (!s_picker)
		s_picker = new WordPicker();
	return *s_picker;
}

WordPicker::WordPicker()
	: _source(nullptr), _revision(0), _list(0), _sourceSize(0), _selected(0), _scroll(0), _accepting(false),
	  _dirty(true), _marqueeStart(0), _marqueeDir(1), _marqueeNextMs(0) {
}

void WordPicker::setSource(PickerSource *source) {
	_source = source;
	_words.clear();
	_indices.clear();
	_sourceSize = ~0u; // adopt the new source's list whatever its revision
	_selected = _scroll = 0;
	_dirty = true;
	if (_source)
		sync(false);
}

bool WordPicker::visible() {
	return _source && _source->pickerVisible();
}

bool WordPicker::acceptsInput() {
	return visible() && _source->pickerAcceptsInput();
}

// Adopts the source's current list. A list the source rebuilt on its own (new
// words turning up) keeps the highlighted entry; one that changed because of
// a selection starts again from the top.
void WordPicker::sync(bool keepLabel) {
	const Common::StringArray &words = _source->pickerWords();
	const uint revision = _source->pickerRevision();
	if (revision == _revision && words.size() == _sourceSize)
		return;

	const uint list = _source->pickerList();
	Common::String previous;
	if (keepLabel && list == _list && _selected < (int)_words.size())
		previous = _words[_selected];

	_revision = revision;
	_list = list;
	_sourceSize = words.size();
	_words.clear();
	_indices.clear();
	for (uint i = 0; i < words.size(); i++) {
		if (isProfanity(words[i]))
			continue;
		_words.push_back(words[i]);
		_indices.push_back(i);
	}
	_selected = 0;
	_scroll = 0;
	if (!previous.empty()) {
		for (uint i = 0; i < _words.size(); i++) {
			if (_words[i] == previous) {
				_selected = i;
				break;
			}
		}
	}
	_marqueeStart = 0;
	_marqueeDir = 1;
	_dirty = true;
}

void WordPicker::move(int delta) {
	if (!_source)
		return;
	sync(true);
	if (_words.empty())
		return;

	const int selected = CLIP<int>(_selected + delta, 0, (int)_words.size() - 1);
	if (selected == _selected)
		return;
	_selected = selected;
	_marqueeStart = 0;
	_marqueeDir = 1;
	_marqueeNextMs = 0;
	_dirty = true;
}

bool WordPicker::select(Common::String &typed) {
	if (!_source)
		return false;
	sync(true);
	if (_words.empty())
		return false;

	typed = _source->pickerSelect(_indices[_selected]);
	sync(false);
	return true;
}

bool WordPicker::back() {
	if (!_source || !_source->pickerBack())
		return false;
	sync(false);
	return true;
}

Common::String WordPicker::label(uint index, int maxChars, uint32 nowMs) {
	const Common::String &word = _words[index];
	if ((int)word.size() <= maxChars)
		return word;

	if ((int)index != _selected)
		return Common::String(word.c_str(), maxChars);

	const int maxStart = (int)word.size() - maxChars;
	if (_marqueeNextMs == 0) {
		_marqueeNextMs = nowMs + kMarqueeStepMs;
	} else if (nowMs >= _marqueeNextMs) {
		_marqueeNextMs = nowMs + kMarqueeStepMs;
		_marqueeStart += _marqueeDir;
		if (_marqueeStart >= maxStart) {
			_marqueeStart = maxStart;
			_marqueeDir = -1;
		} else if (_marqueeStart <= 0) {
			_marqueeStart = 0;
			_marqueeDir = 1;
		}
	}
	return Common::String(word.c_str() + CLIP<int>(_marqueeStart, 0, maxStart), maxChars);
}

static inline void setPixel(uint8 *frame, int rowBytes, int x, int y, bool white) {
	uint8 *p = frame + y * rowBytes + (x >> 3);
	const uint8 bit = 0x80 >> (x & 7);
	if (white)
		*p |= bit;
	else
		*p &= ~bit;
}

bool WordPicker::draw(uint8 *frame, int rowBytes, int x, int width, int height, bool force, uint32 nowMs) {
	if (!_source)
		return false;

	sync(true);

	const bool accepting = _source->pickerAcceptsInput();
	if (accepting != _accepting) {
		_accepting = accepting;
		_dirty = true;
	}

	const int maxChars = (width - kTextMargin - 1) / kGlyphSize;
	const int visibleLines = height / kLineHeight;

	if (_selected < _scroll)
		_scroll = _selected;
	if (_selected >= _scroll + visibleLines)
		_scroll = _selected - (visibleLines - 1);

	// A long selected entry keeps scrolling, so it has to be redrawn on a timer
	const bool marquee = _accepting && _selected < (int)_words.size() &&
	                     (int)_words[_selected].size() > maxChars;
	if (marquee && nowMs >= _marqueeNextMs)
		_dirty = true;

	if (!force && !_dirty)
		return false;
	_dirty = false;

	for (int y = 0; y < height; y++)
		for (int px = x; px < x + width; px++)
			setPixel(frame, rowBytes, px, y, false);

	// Dotted rule between the game and the list
	for (int y = 0; y < height; y += 2)
		setPixel(frame, rowBytes, x, y, true);

	for (int line = 0; line < visibleLines; line++) {
		const uint index = _scroll + line;
		if (index >= _words.size())
			break;

		const int top = line * kLineHeight;
		const bool highlight = _accepting && (int)index == _selected;
		if (highlight) {
			for (int y = top; y < top + kLineHeight; y++)
				for (int px = x + 1; px < x + width; px++)
					setPixel(frame, rowBytes, px, y, true);
		}

		const Common::String text = label(index, maxChars, nowMs);
		for (uint i = 0; i < text.size(); i++) {
			const uint8 *glyph = Graphics::DosFont::fontData_PCBIOS + (byte)text[i] * kGlyphSize;
			const int gx = x + kTextMargin + i * kGlyphSize;
			for (int row = 0; row < kGlyphSize; row++) {
				for (int col = 0; col < kGlyphSize; col++) {
					if (glyph[row] & (0x80 >> col))
						setPixel(frame, rowBytes, gx + col, top + 1 + row, !highlight);
				}
			}
		}
	}

	return true;
}

} // End of namespace Playdate
