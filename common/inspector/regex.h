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

#ifndef COMMON_INSPECTOR_REGEX_H
#define COMMON_INSPECTOR_REGEX_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/str.h"

namespace Inspector {

/**
 * A small backtracking regular-expression matcher for the ECMAScript
 * subset that CDP clients actually send in Debugger.setBreakpointByUrl's
 * `urlRegex` parameter.
 *
 * VS Code's js-debug converts every on-disk breakpoint path into a
 * case-insensitivity-expanded regex (e.g. `[fF][oO][oO]`), with
 * non-capturing groups, alternation between percent-encoded and literal
 * forms, escaped metacharacters and a `($|\?)` suffix — a server that only
 * supports exact `url` matching never binds js-debug breakpoints.
 *
 * Supported syntax: literals, `.`, escapes (`\/ \. \\ \d \D \w \W \s \S
 * \n \r \t` and escaped metacharacters), character classes `[...]` with
 * ranges and negation, groups `(...)` and `(?:...)` (both non-capturing —
 * only boolean matching is offered), alternation `|`, quantifiers `* + ?`
 * (greedy), and anchors `^ $`.
 *
 * Backtracking is bounded (kMaxSteps) so a pathological pattern degrades
 * to "no match" instead of hanging the debuggee.
 */
class Regex {
public:
	explicit Regex(const Common::String &pattern);

	/** True if the pattern compiled. Broken patterns never match. */
	bool valid() const { return _valid; }

	/** Unanchored search, like ECMAScript RegExp.prototype.test(). */
	bool test(const Common::String &subject) const;

private:
	enum NodeType {
		kNodeChar,                ///< match one literal byte
		kNodeAny,                 ///< '.'
		kNodeAnyIncludingNewline, ///< implicit search-loop advance
		kNodeClass,               ///< [...] character class
		kNodeSplit,               ///< alternation/quantifier branch point
		kNodeJump,                ///< epsilon transition
		kNodeAnchorBOL,           ///< '^'
		kNodeAnchorEOL,           ///< '$'
		kNodeAccept
	};

	struct Node {
		NodeType type;
		byte ch;                 ///< kNodeChar
		uint32 classIndex;       ///< kNodeClass -> _classes
		int next;                ///< primary successor
		int alt;                 ///< kNodeSplit only

		Node() : type(kNodeChar), ch(0), classIndex(0), next(-1), alt(-1) {}
	};

	struct CharClass {
		bool negated;
		bool bitmap[256];

		CharClass() : negated(false) {
			memset(bitmap, 0, sizeof(bitmap));
		}
		bool contains(byte c) const {
			return bitmap[c] != negated;
		}
	};

	Common::Array<Node> _nodes;
	Common::Array<CharClass> _classes;
	int _start;
	bool _valid;

	// Compilation (recursive descent over the pattern).
	struct Parser;
	friend struct Parser;

	bool matchFrom(const Common::String &subject, uint32 pos, uint32 &steps) const;

	static const uint32 kMaxSteps = 1000000;
};

} // End of namespace Inspector

#endif
