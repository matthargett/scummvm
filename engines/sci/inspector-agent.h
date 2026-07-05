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

#ifndef SCI_INSPECTOR_AGENT_H
#define SCI_INSPECTOR_AGENT_H

#include "common/hashmap.h"
#include "common/inspector/agent.h"
#include "common/inspector/session.h"

#include "sci/engine/vm_types.h"

namespace Sci {

class SciEngine;
class Script;
struct ExecStack;

/**
 * Script-inspector adapter for the SCI p-machine (see
 * common/inspector/DESIGN.md). Every script resource the program counter
 * enters is registered lazily as scummvm-dbg://sci/script-N with a real
 * disassembly listing: the code regions of the script buffer (SCI0/SCI1
 * SCI_OBJ_CODE blocks; the post-dictionary code area for SCI1.1-2.1; the
 * header-declared code block for SCI3) are decoded with a bounds-checked
 * replica of readPMachineInstruction(), so every listing offset is a
 * byte offset the VM's pc can actually hold.
 *
 * The VM hook lives in run_vm()'s pre-instruction debug block (where
 * checkAddressBreakpoint and Console::onFrame already sit), i.e. before
 * the opcode fetch while xs->addr.pc still points at the instruction
 * start. SCI runs one p-machine (kernel calls may re-enter run_vm on the
 * same execution stack), so the whole interpreter is a single inspector
 * thread and call depth equals s->_executionStack.size().
 */
class SciInspectorAgent : public Inspector::Agent {
public:
	explicit SciInspectorAgent(SciEngine *vm);

	/** Create the session (no-op unless inspector_enable is set). */
	void init();
	void shutdown();

	/**
	 * Idempotent lazy registration; called from run_vm() whenever the
	 * execution-stack position changes (i.e. before any instruction of a
	 * newly entered script segment runs). Not gated on armed() so that
	 * pending URL breakpoints can bind while the debugger is idle.
	 */
	void onScriptEntered(Script *scr);

	/** Per-instruction hook; caller checks Inspector::g_session->armed(). */
	void onInstruction();

	/** Read/write watch hooks (read_var/write_var in vm.cpp). */
	void onVariableRead(int type, int index, const reg_t &value);
	void onVariableWrite(int type, int index, const reg_t &value);

	/** Transport pump; called from EventManager::getSciEvent(), which
	 *  every game cycle reaches (kGetEvent polls input each doit cycle,
	 *  and SciEngine::sleep polls it during kWait/throttle waits). */
	void transportTick();

	bool active() const { return _session != nullptr; }

	// --- Inspector::Agent ---
	Common::String engineId() const override { return "sci"; }
	Common::String targetTitle() const override;
	void buildCallFrames(uint32 threadId, Common::Array<Inspector::CallFrameInfo> &frames) override;
	void buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
	                      Inspector::RemoteObjectTable &table, int objRef) override;
	bool evaluate(uint32 threadId, int frameIndex, const Common::String &expression,
	              Inspector::RemoteObjectTable &table, Inspector::DebugValue &result) override;
	Common::String describeThread(uint32 threadId) const override;

private:
	SciEngine *_vm;
	Inspector::Session *_session;
	Common::HashMap<int, int> _scriptHandles; ///< script number -> handle

	int handleForScript(Script *scr);
	int handleForSegment(SegmentId segment);
	const ExecStack *execFrame(int frameIndex) const;

	Inspector::DebugValue valueFor(const reg_t &r) const;
	Common::String frameFunctionName(const ExecStack &call) const;
	void frameScopes(const ExecStack &call, Common::Array<Inspector::ScopeInfo> &scopes) const;

	// Listing construction.
	void buildListing(Script *scr, Inspector::ScriptListing &listing) const;
	void appendCodeRange(Script *scr, uint32 start, uint32 end,
	                     Inspector::ScriptListing &listing) const;
	uint32 decodeInstruction(const byte *buf, uint32 bufSize, uint32 pos,
	                         byte &extOpcode, int16 opparams[4]) const;
	Common::String instructionText(const byte *buf, uint32 pos, uint32 length,
	                               byte extOpcode, const int16 opparams[4]) const;

	// evaluate() helpers.
	bool parseVarRef(const Common::String &name, int &type, int &index) const;
	bool readVarRef(int type, int index, Inspector::DebugValue &result) const;
};

} // End of namespace Sci

#endif
