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

#ifndef COMMON_INSPECTOR_AGENT_H
#define COMMON_INSPECTOR_AGENT_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/str.h"
#include "common/inspector/remoteobject.h"

namespace Inspector {

/**
 * The engine-facing side of the script inspector: one Agent subclass per
 * engine translates between the engine's script VM (script resources,
 * program counters, call stacks, variable banks) and the protocol core.
 *
 * The contract is deliberately small — see DESIGN.md for the adapter
 * table with the exact VM hook site per engine. Engines:
 *
 *  1. register their scripts (Session::registerScript) with a
 *     disassembly listing when a script resource is loaded;
 *  2. call Session::instructionHook() from the VM dispatch loop, guarded
 *     by Session::armed() so the disabled cost is one predictable branch;
 *  3. implement the pull callbacks below, which are only invoked while
 *     the VM is paused inside instructionHook().
 */

/** One scope of one call frame (CDP Scope.type vocabulary: "global",
 *  "local", "closure", ... — game VMs mostly use global/local plus
 *  engine-specific register banks presented as "local"). */
struct ScopeInfo {
	Common::String type;  ///< CDP scope type string
	Common::String name;  ///< display name ("Globals", "Locals (slot 3)", ...)
};

/** One call frame, innermost first. */
struct CallFrameInfo {
	Common::String functionName; ///< display name ("script 201", "room11:entry")
	int scriptHandle;            ///< handle from Session::registerScript
	uint32 offset;               ///< current bytecode offset in that script
	Common::Array<ScopeInfo> scopes;

	CallFrameInfo() : scriptHandle(-1), offset(0) {}
};

class Agent {
public:
	virtual ~Agent() {}

	/** Short engine identifier used in URLs and the debuggerId ("scumm"). */
	virtual Common::String engineId() const = 0;

	/** Human-readable target title for the discovery endpoint. */
	virtual Common::String targetTitle() const = 0;

	/**
	 * Build the call stack of @p threadId, innermost frame first.
	 * Called only while paused.
	 */
	virtual void buildCallFrames(uint32 threadId, Common::Array<CallFrameInfo> &frames) = 0;

	/**
	 * Materialize the variables of one scope into @p objRef of @p table
	 * (table.addProperty(objRef, name, value) per variable). Called
	 * lazily when the client expands the scope in the variables pane.
	 * @param frameIndex index into the frames from buildCallFrames()
	 * @param scopeIndex index into that frame's scopes
	 */
	virtual void buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
	                              RemoteObjectTable &table, int objRef) = 0;

	/**
	 * Evaluate @p expression in the context of a frame (frameIndex < 0:
	 * global context, e.g. Runtime.evaluate while running). Engines
	 * typically support bare variable names ("V13", "flag7") and simple
	 * assignments. Return false if the expression is not understood —
	 * the session then reports undefined, never a protocol error.
	 *
	 * Also used for breakpoint condition strings; an unevaluable
	 * condition is treated as met (the breakpoint pauses) so a typo'd
	 * condition fails loudly rather than silently never firing.
	 */
	virtual bool evaluate(uint32 threadId, int frameIndex, const Common::String &expression,
	                      RemoteObjectTable &table, DebugValue &result) {
		return false;
	}

	/** Optional human-readable thread description ("SCUMM slot 3 (script 201)"). */
	virtual Common::String describeThread(uint32 threadId) const {
		return Common::String::format("thread-%u", threadId);
	}
};

} // End of namespace Inspector

#endif
