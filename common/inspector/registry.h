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

#ifndef COMMON_INSPECTOR_REGISTRY_H
#define COMMON_INSPECTOR_REGISTRY_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/formats/json.h"
#include "common/str.h"

namespace Inspector {

/**
 * The script model: every engine script resource is presented to CDP
 * clients as one "script" whose source text is a disassembly listing,
 * one instruction per line — the textual variant of how V8 presents
 * WebAssembly modules. Locations are (listing line, column 0); the raw
 * bytecode offset of each line is kept server-side. When the engine can
 * produce an original-language listing (SCI/AGI's LISP-ish script text),
 * a Source Map v3 data: URL maps listing lines back to it.
 */
struct ListingLine {
	uint32 offset;          ///< bytecode offset of this instruction
	Common::String text;    ///< disassembly text (without trailing newline)
	bool isStatement;       ///< statement start: a valid step/breakpoint target
	int originalLine;       ///< 0-based line in the original source, -1 = none

	ListingLine() : offset(0), isStatement(true), originalLine(-1) {}
	ListingLine(uint32 ofs, const Common::String &t, bool stmt = true, int orig = -1) :
		offset(ofs), text(t), isStatement(stmt), originalLine(orig) {}
};

struct ScriptListing {
	Common::String url;             ///< stable synthetic URL (scummvm-dbg://...)
	Common::Array<ListingLine> lines;
	Common::String originalSource;  ///< optional decompiled source ("" = none)
	Common::String originalName;    ///< display name of the original source
};

class ScriptRegistry {
public:
	ScriptRegistry();

	/**
	 * Register a script. Line offsets must be ascending; returns -1 if
	 * they are not (broken adapter). Re-registering the same URL yields a
	 * fresh script with a fresh scriptId, mirroring V8's behaviour when
	 * the same resource is parsed again.
	 * @return script handle (>= 0)
	 */
	int addScript(const ScriptListing &listing);

	int scriptCount() const { return (int)_scripts.size(); }
	bool validHandle(int handle) const {
		return handle >= 0 && (uint32)handle < _scripts.size();
	}

	/** CDP scriptId (a string by spec — V8 uses numeric strings). */
	Common::String scriptIdFor(int handle) const;
	/** Reverse lookup; -1 if unknown. */
	int handleForScriptId(const Common::String &scriptId) const;

	const Common::String &url(int handle) const;

	/** Full listing text for Debugger.getScriptSource. */
	Common::String sourceText(int handle) const;

	/** Number of listing lines. */
	int lineCount(int handle) const;

	/** 0-based listing line whose instruction covers @p offset (the last
	 *  line with lineOffset <= offset); -1 if offset precedes all lines. */
	int lineForOffset(int handle, uint32 offset) const;

	/** Bytecode offset of a listing line; false if out of range. */
	bool offsetForLine(int handle, int line, uint32 &offset) const;

	/** First statement line at or after @p line; -1 if none. */
	int snapToStatement(int handle, int line) const;

	/** Listing line of the statement covering @p offset (walks back from
	 *  the instruction's line to the nearest statement start). */
	int statementForOffset(int handle, uint32 offset) const;

	bool isStatementLine(int handle, int line) const;

	/** Source map data: URL, or "" when no original source was supplied. */
	Common::String sourceMapURL(int handle) const;

	/** Ready-made params object for a Debugger.scriptParsed event
	 *  (caller owns the result). @p executionContextId per ledger #4. */
	Common::JSONValue *scriptParsedParams(int handle, int executionContextId) const;

private:
	struct Record {
		ScriptListing listing;
		Common::String sourceMapURL; ///< built once at registration
	};

	Common::Array<Record> _scripts;
};

} // End of namespace Inspector

#endif
