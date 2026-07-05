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

#ifndef SCUMM_INSPECTOR_AGENT_H
#define SCUMM_INSPECTOR_AGENT_H

#include "common/hashmap.h"
#include "common/inspector/agent.h"
#include "common/inspector/session.h"

#include "scumm/script.h"

namespace Scumm {

class ScummEngine;

/**
 * Script-inspector adapter for the SCUMM bytecode interpreter (see
 * common/inspector/DESIGN.md). SCUMM has no static disassembler in the
 * tree (descumm lives in scummvm-tools), so scripts are presented with
 * the documented byte-granularity fallback listing: one line per byte
 * offset of the containing resource ("[%04x] db %02x"), every line a
 * statement. Since only real instruction starts generate hook events,
 * breakpoints and steps still land exactly on instruction boundaries.
 * Each line is additionally annotated with the current opcode table's
 * name for that byte value — a heuristic aid that is only meaningful
 * when the byte actually is an opcode (i.e. at instruction starts).
 *
 * Script identity follows the containing resource that the program
 * counter (_scriptPointer - _scriptOrgPointer) is an offset into, as
 * derived by ScummEngine::getScriptBaseAddress() from the slot's
 * 'where' type:
 *
 *   WIO_GLOBAL     scummvm-dbg://scumm/global-N          (rtScript N)
 *   WIO_LOCAL      scummvm-dbg://scumm/roomR/local-N     (room resource)
 *   WIO_ROOM       scummvm-dbg://scumm/roomR/room-N      (room resource)
 *   WIO_INVENTORY  scummvm-dbg://scumm/inventory-N       (rtInventory)
 *   WIO_FLOBJECT   scummvm-dbg://scumm/flobject-N        (rtFlObject)
 *
 * Local/room script numbers are only unique per room, so the room
 * resource number is part of both the registration key and the URL.
 *
 * The VM hook sits at the top of the ScummEngine::executeScript()
 * dispatch loop, before the opcode fetch, when the PC still points at
 * the instruction start. threadId is the script slot number (SCUMM
 * multiplexes up to 80 concurrent scripts) and the call depth mirrors
 * vm.numNestedScripts (nested-synchronous starts via runScriptNested).
 */
class ScummInspectorAgent : public Inspector::Agent {
public:
	explicit ScummInspectorAgent(ScummEngine *vm);

	/** Create the session (no-op unless inspector_enable is set). */
	void init();
	void shutdown();

	/** Idempotent lazy registration of the current slot's script,
	 *  called on executeScript() entry. */
	void onScriptActivated();

	/** Per-instruction hook; caller checks Inspector::g_session first. */
	void onInstruction();

	/** Write watch hook (plain-global branch of writeVar). */
	void onVariableWrite(int var, int value);

	/** Per-frame transport pump, called from scummLoop(). */
	void transportTick();

	bool active() const { return _session != nullptr; }

	// --- Inspector::Agent ---
	Common::String engineId() const override { return "scumm"; }
	Common::String targetTitle() const override;
	void buildCallFrames(uint32 threadId, Common::Array<Inspector::CallFrameInfo> &frames) override;
	void buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
	                      Inspector::RemoteObjectTable &table, int objRef) override;
	bool evaluate(uint32 threadId, int frameIndex, const Common::String &expression,
	              Inspector::RemoteObjectTable &table, Inspector::DebugValue &result) override;
	Common::String describeThread(uint32 threadId) const override;

private:
	/** One resolved frame of the current nesting chain. */
	struct FrameRef {
		int slotNr;
		uint32 offset;
		int handle;

		FrameRef() : slotNr(-1), offset(0), handle(-1) {}
	};

	ScummEngine *_vm;
	Inspector::Session *_session;
	Common::HashMap<uint32, int> _scriptHandles; ///< (where,room,number) -> handle
	int _slotHandles[NUM_SCRIPT_SLOT];           ///< handle per activated slot

	uint32 scriptKeyFor(int slotNr) const;
	bool resolveScriptResource(int slotNr, int &resType, int &resIdx,
	                           Common::String &url) const;
	bool buildListing(int slotNr, Inspector::ScriptListing &listing) const;
	int ensureScriptRegistered(int slotNr);

	void collectFrames(uint32 threadId, Common::Array<FrameRef> &frames) const;
	int frameSlot(uint32 threadId, int frameIndex) const;
	bool hasBitVarScope() const;
	bool hasRoomVarScope() const;
};

} // End of namespace Scumm

#endif
