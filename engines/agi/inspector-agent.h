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

#ifndef AGI_INSPECTOR_AGENT_H
#define AGI_INSPECTOR_AGENT_H

#include "common/hashmap.h"
#include "common/inspector/agent.h"
#include "common/inspector/session.h"

namespace Agi {

class AgiEngine;

/**
 * Script-inspector adapter for the AGI LOGIC interpreter (see
 * common/inspector/DESIGN.md). Presents each LOGIC resource as one
 * script (scummvm-dbg://agi/logic-N) whose source is a disassembly
 * listing built from the engine's live opcode tables; every listing
 * offset is a raw cIP value, so breakpoints resolved by the core map
 * 1:1 onto the interpreter's instruction pointer.
 *
 * The VM hook lives at the top of AgiEngine::runLogic()'s dispatch loop
 * (one call per executed action instruction, before the opcode fetch,
 * when cIP still points at the instruction start). AGI runs its logics
 * nested-synchronously on one thread, so the whole interpreter is a
 * single inspector thread and call depth equals _game.execStack.size().
 */
class AgiInspectorAgent : public Inspector::Agent {
public:
	explicit AgiInspectorAgent(AgiEngine *vm);

	/** Create the session (no-op unless inspector_enable is set). */
	void init();
	void shutdown();

	/** Idempotent lazy registration, called on runLogic() entry. */
	void ensureLogicRegistered(int16 logicNr);

	/** Per-instruction hook; caller checks Inspector::g_session first. */
	void onInstruction();

	/** Read/write watch hooks (AgiEngine::getVar/setVar). */
	void onVariableRead(int16 varNr, byte value);
	void onVariableWrite(int16 varNr, byte newValue);

	/** Per-cycle transport pump, called from runLogic(0) entry. */
	void transportTick();

	bool active() const { return _session != nullptr; }

	// --- Inspector::Agent ---
	Common::String engineId() const override { return "agi"; }
	Common::String targetTitle() const override;
	void buildCallFrames(uint32 threadId, Common::Array<Inspector::CallFrameInfo> &frames) override;
	void buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
	                      Inspector::RemoteObjectTable &table, int objRef) override;
	bool evaluate(uint32 threadId, int frameIndex, const Common::String &expression,
	              Inspector::RemoteObjectTable &table, Inspector::DebugValue &result) override;
	Common::String describeThread(uint32 threadId) const override;

private:
	AgiEngine *_vm;
	Inspector::Session *_session;
	Common::HashMap<int16, int> _logicHandles; ///< logicNr -> script handle

	int handleFor(int16 logicNr) const;
	void buildListing(int16 logicNr, Inspector::ScriptListing &listing) const;
	uint32 testClauseEnd(const uint8 *data, uint32 size, uint32 pos) const;
};

} // End of namespace Agi

#endif
