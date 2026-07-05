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

#include "common/inspector/regex.h"

namespace Inspector {

/**
 * Compiler: recursive descent producing a Thompson-style NFA over _nodes.
 * Fragments carry a start node and a patch list of dangling out-edges
 * (encoded nodeIndex * 2 + field, field 0 = next, 1 = alt).
 */
struct Regex::Parser {
	Regex &re;
	const Common::String &pattern;
	uint32 pos;
	bool ok;

	struct Frag {
		int start;
		Common::Array<uint32> out;
	};

	Parser(Regex &r, const Common::String &p) : re(r), pattern(p), pos(0), ok(true) {}

	bool atEnd() const { return pos >= pattern.size(); }
	char peek() const { return atEnd() ? 0 : pattern[pos]; }
	char take() { return atEnd() ? 0 : pattern[pos++]; }

	int newNode(NodeType type) {
		Node n;
		n.type = type;
		re._nodes.push_back(n);
		return (int)re._nodes.size() - 1;
	}

	void patch(const Common::Array<uint32> &out, int target) {
		for (uint32 i = 0; i < out.size(); i++) {
			Node &n = re._nodes[out[i] >> 1];
			if (out[i] & 1)
				n.alt = target;
			else
				n.next = target;
		}
	}

	Frag epsilonFrag() {
		Frag f;
		f.start = newNode(kNodeJump);
		f.out.push_back((uint32)f.start << 1);
		return f;
	}

	Frag parseAlternation() {
		Frag first = parseConcat();
		if (!ok)
			return first;
		if (peek() != '|')
			return first;

		Frag result = first;
		while (ok && peek() == '|') {
			take();
			Frag branch = parseConcat();
			if (!ok)
				return result;
			int split = newNode(kNodeSplit);
			re._nodes[split].next = result.start;
			re._nodes[split].alt = branch.start;
			Frag merged;
			merged.start = split;
			merged.out = result.out;
			for (uint32 i = 0; i < branch.out.size(); i++)
				merged.out.push_back(branch.out[i]);
			result = merged;
		}
		return result;
	}

	Frag parseConcat() {
		Frag result;
		result.start = -1;
		while (ok && !atEnd() && peek() != '|' && peek() != ')') {
			Frag piece = parseRepeat();
			if (!ok)
				return result;
			if (result.start < 0) {
				result = piece;
			} else {
				patch(result.out, piece.start);
				result.out = piece.out;
			}
		}
		if (result.start < 0)
			return epsilonFrag();
		return result;
	}

	Frag parseRepeat() {
		Frag atom = parseAtom();
		if (!ok)
			return atom;
		char q = peek();
		if (q != '*' && q != '+' && q != '?')
			return atom;
		take();

		Frag result;
		if (q == '*') {
			// split -> atom -> back to split; exit through split.alt
			int split = newNode(kNodeSplit);
			re._nodes[split].next = atom.start; // greedy: body first
			patch(atom.out, split);
			result.start = split;
			result.out.push_back(((uint32)split << 1) | 1);
		} else if (q == '+') {
			int split = newNode(kNodeSplit);
			re._nodes[split].next = atom.start;
			patch(atom.out, split);
			result.start = atom.start;
			result.out.push_back(((uint32)split << 1) | 1);
		} else { // '?'
			int split = newNode(kNodeSplit);
			re._nodes[split].next = atom.start;
			result.start = split;
			result.out = atom.out;
			result.out.push_back(((uint32)split << 1) | 1);
		}
		return result;
	}

	Frag parseAtom() {
		Frag f;
		f.start = -1;
		if (atEnd()) {
			ok = false;
			return f;
		}
		char c = take();
		switch (c) {
		case '(': {
			// Both capturing and (?:...) groups compile identically; only
			// boolean matching is offered.
			if (peek() == '?') {
				take();
				if (take() != ':') {
					ok = false;
					return f;
				}
			}
			f = parseAlternation();
			if (!ok || take() != ')') {
				ok = false;
				return f;
			}
			return f;
		}
		case '[':
			return parseClass();
		case '.': {
			int n = newNode(kNodeAny);
			f.start = n;
			f.out.push_back((uint32)n << 1);
			return f;
		}
		case '^': {
			int n = newNode(kNodeAnchorBOL);
			f.start = n;
			f.out.push_back((uint32)n << 1);
			return f;
		}
		case '$': {
			int n = newNode(kNodeAnchorEOL);
			f.start = n;
			f.out.push_back((uint32)n << 1);
			return f;
		}
		case '\\':
			return parseEscape();
		case '*':
		case '+':
		case '?':
		case ')':
		case '|':
			// Quantifier without an atom / stray metacharacter.
			ok = false;
			return f;
		default: {
			int n = newNode(kNodeChar);
			re._nodes[n].ch = (byte)c;
			f.start = n;
			f.out.push_back((uint32)n << 1);
			return f;
		}
		}
	}

	// Add the members of a \d \w \s style shorthand to a bitmap.
	static bool addShorthand(char c, bool bitmap[256]) {
		switch (c) {
		case 'd':
			for (int i = '0'; i <= '9'; i++)
				bitmap[i] = true;
			return true;
		case 'w':
			for (int i = '0'; i <= '9'; i++)
				bitmap[i] = true;
			for (int i = 'a'; i <= 'z'; i++)
				bitmap[i] = true;
			for (int i = 'A'; i <= 'Z'; i++)
				bitmap[i] = true;
			bitmap[(byte)'_'] = true;
			return true;
		case 's':
			bitmap[(byte)' '] = true;
			bitmap[(byte)'\t'] = true;
			bitmap[(byte)'\n'] = true;
			bitmap[(byte)'\r'] = true;
			bitmap[(byte)'\f'] = true;
			bitmap[(byte)'\v'] = true;
			return true;
		default:
			return false;
		}
	}

	static char controlEscape(char c) {
		switch (c) {
		case 'n': return '\n';
		case 'r': return '\r';
		case 't': return '\t';
		case 'f': return '\f';
		case 'v': return '\v';
		case '0': return '\0';
		default:  return c; // identity escape (\. \/ \\ \( ...)
		}
	}

	Frag parseEscape() {
		Frag f;
		f.start = -1;
		if (atEnd()) {
			ok = false;
			return f;
		}
		char c = take();
		char lower = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
		if (lower == 'd' || lower == 'w' || lower == 's') {
			CharClass cls;
			addShorthand(lower, cls.bitmap);
			cls.negated = (c >= 'A' && c <= 'Z');
			re._classes.push_back(cls);
			int n = newNode(kNodeClass);
			re._nodes[n].classIndex = re._classes.size() - 1;
			f.start = n;
			f.out.push_back((uint32)n << 1);
			return f;
		}
		int n = newNode(kNodeChar);
		re._nodes[n].ch = (byte)controlEscape(c);
		f.start = n;
		f.out.push_back((uint32)n << 1);
		return f;
	}

	Frag parseClass() {
		Frag f;
		f.start = -1;
		CharClass cls;
		if (peek() == '^') {
			take();
			cls.negated = true;
		}
		bool first = true;
		while (true) {
			if (atEnd()) {
				ok = false; // unterminated class
				return f;
			}
			char c = take();
			if (c == ']' && !first)
				break;
			first = false;
			byte lo;
			if (c == '\\') {
				if (atEnd()) {
					ok = false;
					return f;
				}
				char e = take();
				char lower = (e >= 'A' && e <= 'Z') ? (char)(e - 'A' + 'a') : e;
				if (lower == 'd' || lower == 'w' || lower == 's') {
					if (e >= 'A' && e <= 'Z') {
						// Negated shorthand inside a class: complement it in.
						bool tmp[256];
						memset(tmp, 0, sizeof(tmp));
						addShorthand(lower, tmp);
						for (int i = 0; i < 256; i++)
							if (!tmp[i])
								cls.bitmap[i] = true;
					} else {
						addShorthand(lower, cls.bitmap);
					}
					continue;
				}
				lo = (byte)controlEscape(e);
			} else {
				lo = (byte)c;
			}
			// Range?
			if (peek() == '-' && pos + 1 < pattern.size() && pattern[pos + 1] != ']') {
				take(); // '-'
				char hc = take();
				byte hi;
				if (hc == '\\') {
					if (atEnd()) {
						ok = false;
						return f;
					}
					hi = (byte)controlEscape(take());
				} else {
					hi = (byte)hc;
				}
				if (hi < lo) {
					ok = false;
					return f;
				}
				for (int i = lo; i <= hi; i++)
					cls.bitmap[i] = true;
			} else {
				cls.bitmap[lo] = true;
			}
		}
		re._classes.push_back(cls);
		int n = newNode(kNodeClass);
		re._nodes[n].classIndex = re._classes.size() - 1;
		f.start = n;
		f.out.push_back((uint32)n << 1);
		return f;
	}
};

Regex::Regex(const Common::String &pattern) : _start(-1), _valid(false) {
	Parser p(*this, pattern);
	Parser::Frag body = p.parseAlternation();
	if (!p.ok || !p.atEnd()) {
		// Broken pattern: never match.
		_nodes.clear();
		_classes.clear();
		return;
	}

	// Unanchored search: prepend an implicit lazy ".*" that also crosses
	// newlines. Trying the pattern first at each position keeps '^'
	// working (kNodeAnchorBOL checks for position 0).
	int accept = p.newNode(kNodeAccept);
	p.patch(body.out, accept);

	int advance = p.newNode(kNodeChar); // placeholder, retyped below
	_nodes[advance].type = kNodeAnyIncludingNewline;
	int searchSplit = p.newNode(kNodeSplit);
	_nodes[searchSplit].next = body.start;   // try a match here first
	_nodes[searchSplit].alt = advance;       // otherwise consume one byte...
	_nodes[advance].next = searchSplit;      // ...and retry

	_start = searchSplit;
	_valid = true;
}

bool Regex::test(const Common::String &subject) const {
	if (!_valid)
		return false;
	uint32 steps = 0;
	return matchFrom(subject, 0, steps);
}

bool Regex::matchFrom(const Common::String &subject, uint32 startPos, uint32 &steps) const {
	const uint32 len = subject.size();
	const uint32 stride = len + 1;

	// visited[(node * stride) + pos]: this NFA state was already explored
	// and did not lead to acceptance (if it had, we would have returned).
	// This makes the DFS polynomial and immunizes it against zero-width
	// loops such as (?:a*)*.
	Common::Array<bool> visited;
	visited.resize(_nodes.size() * stride);
	for (uint32 i = 0; i < visited.size(); i++)
		visited[i] = false;

	Common::Array<uint64> stack;
	stack.push_back(((uint64)_start << 32) | startPos);

	while (!stack.empty()) {
		if (++steps > kMaxSteps)
			return false; // degrade to no-match, never hang the debuggee
		uint64 state = stack.back();
		stack.pop_back();
		int node = (int)(state >> 32);
		uint32 pos = (uint32)state;

		uint32 key = (uint32)node * stride + pos;
		if (visited[key])
			continue;
		visited[key] = true;

		const Node &n = _nodes[node];
		switch (n.type) {
		case kNodeAccept:
			return true;
		case kNodeChar:
			if (pos < len && (byte)subject[pos] == n.ch)
				stack.push_back(((uint64)n.next << 32) | (pos + 1));
			break;
		case kNodeAny:
			if (pos < len && subject[pos] != '\n')
				stack.push_back(((uint64)n.next << 32) | (pos + 1));
			break;
		case kNodeAnyIncludingNewline:
			if (pos < len)
				stack.push_back(((uint64)n.next << 32) | (pos + 1));
			break;
		case kNodeClass:
			if (pos < len && _classes[n.classIndex].contains((byte)subject[pos]))
				stack.push_back(((uint64)n.next << 32) | (pos + 1));
			break;
		case kNodeAnchorBOL:
			if (pos == 0)
				stack.push_back(((uint64)n.next << 32) | pos);
			break;
		case kNodeAnchorEOL:
			if (pos == len)
				stack.push_back(((uint64)n.next << 32) | pos);
			break;
		case kNodeJump:
			stack.push_back(((uint64)n.next << 32) | pos);
			break;
		case kNodeSplit:
			// Push alt first so next (the greedy branch) pops first.
			stack.push_back(((uint64)n.alt << 32) | pos);
			stack.push_back(((uint64)n.next << 32) | pos);
			break;
		default:
			break;
		}
	}
	return false;
}

} // End of namespace Inspector
