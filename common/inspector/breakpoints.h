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

#ifndef COMMON_INSPECTOR_BREAKPOINTS_H
#define COMMON_INSPECTOR_BREAKPOINTS_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/inspector/registry.h"

namespace Inspector {

/**
 * Breakpoint store with the deferred-resolution semantics CDP clients
 * depend on (DESIGN.md ledger #2, #6, #7, #8):
 *
 *  - a breakpoint set before its script is parsed is accepted with an
 *    empty locations list, kept, and bound when a matching script
 *    appears (the caller then emits Debugger.breakpointResolved);
 *  - matching is by exact `url` OR by `urlRegex` (js-debug exclusively
 *    sends regexes for file paths);
 *  - one breakpoint may bind into several scripts; all locations are
 *    reported and a hit anywhere reports the breakpoint id;
 *  - setting a second breakpoint at the same (url|urlRegex, line, column)
 *    is an error, mirroring V8.
 *
 * Lines snap forward to the next statement line of the listing, the
 * equivalent of V8 snapping to the next break location.
 */
class BreakpointStore {
public:
	struct Location {
		int scriptHandle;
		int line;       ///< resolved statement line (0-based)
		uint32 offset;  ///< bytecode offset of that line

		Location() : scriptHandle(-1), line(-1), offset(0) {}
	};

	struct Breakpoint {
		Common::String id;
		Common::String url;      ///< exact-match spec ("" if regex-based)
		Common::String urlRegex; ///< regex spec ("" if url-based)
		int requestedLine;
		int requestedColumn;     ///< informational; columns are always 0
		Common::String condition;
		Common::Array<Location> locations;
	};

	BreakpointStore();
	~BreakpointStore();

	/**
	 * Debugger.setBreakpointByUrl. Exactly one of @p url / @p urlRegex
	 * must be non-empty. Resolves against every already-registered script
	 * in @p registry.
	 * @return the new breakpoint, or nullptr with @p error set (duplicate
	 *         location, bad spec, invalid regex).
	 */
	const Breakpoint *setByUrl(const Common::String &url,
	                           const Common::String &urlRegex,
	                           int line, int column,
	                           const Common::String &condition,
	                           const ScriptRegistry &registry,
	                           Common::String &error);

	/** Debugger.removeBreakpoint. */
	bool remove(const Common::String &breakpointId);

	/** Drop every breakpoint (client detached). */
	void clear();

	/**
	 * Bind existing breakpoints to a newly registered script.
	 * @param newlyResolved receives (breakpointId, Location) pairs for
	 *        which the caller must emit Debugger.breakpointResolved.
	 */
	struct Resolution {
		Common::String breakpointId;
		Location location;
	};
	void bindScript(int scriptHandle, const ScriptRegistry &registry,
	                Common::Array<Resolution> &newlyResolved);

	/** Debugger.setBreakpointsActive. */
	void setActive(bool active) { _active = active; }
	bool active() const { return _active; }

	/**
	 * Hot-path hit test: breakpoint ids (if any) at (script, offset).
	 * Returns false immediately when inactive or nothing is set there.
	 */
	bool hitTest(int scriptHandle, uint32 offset,
	             Common::Array<const Breakpoint *> &hits) const;

	/** True if any breakpoint has a resolved location (arms the VM hook). */
	bool anyResolved() const { return _resolvedCount > 0; }

	const Breakpoint *findById(const Common::String &id) const;
	uint32 count() const { return _breakpoints.size(); }

private:
	Common::Array<Breakpoint *> _breakpoints;
	int _nextId;
	bool _active;
	uint32 _resolvedCount;

	// (scriptHandle, offset) -> indices into _breakpoints, for the hot path.
	typedef Common::HashMap<uint64, Common::Array<uint32> > HitMap;
	HitMap _hitMap;

	static uint64 hitKey(int scriptHandle, uint32 offset) {
		return ((uint64)(uint32)scriptHandle << 32) | offset;
	}

	bool resolveAgainst(Breakpoint &bp, int scriptHandle,
	                    const ScriptRegistry &registry, Location &out) const;
	void recordLocation(uint32 bpIndex, const Location &loc);
};

} // End of namespace Inspector

#endif
