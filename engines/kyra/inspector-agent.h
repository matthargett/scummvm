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

#ifndef KYRA_INSPECTOR_AGENT_H
#define KYRA_INSPECTOR_AGENT_H

#include "common/hashmap.h"
#include "common/inspector/agent.h"
#include "common/inspector/session.h"

#include "kyra/script/script.h"

namespace Kyra {

class KyraEngine_v1;

/**
 * Script-inspector adapter for the KYRA EMC interpreter (see
 * common/inspector/DESIGN.md). Every EMC script file is presented as one
 * script (scummvm-dbg://kyra/<filename>, e.g. .../_STARTUP.EMC) whose
 * source is a static disassembly of the DATA chunk word stream; listing
 * line offsets are BYTE offsets into EMCData::data, exactly the value
 * EMCInterpreter::run() computes for its bounds check, so breakpoints
 * resolved by the core map 1:1 onto the interpreter's instruction
 * pointer.
 *
 * The VM hook lives in EMCInterpreter::run() between the bounds check
 * and the opcode fetch — the single choke point every EMC instruction of
 * every KYRA game passes through, with ip still pointing at the
 * instruction start.
 *
 * Thread identity: many EMCState activations interleave (scene scripts,
 * click scripts, temporary on-stack states, ...), all driven from one
 * engine thread. Each EMCState is mapped to an inspector thread by
 * folding its address into a 32-bit id — stable while the activation
 * lives, but an id can be reused when a stack-allocated EMCState dies
 * and a later one lands on the same address. Stepping only relies on the
 * call-depth chain of the *initiating* activation, so this is benign.
 *
 * Call depth is read from the interpreter's real in-band call frames:
 * op_pushRetOrPos(param==1) pushes the return position (in code WORDS)
 * at stack[bp-1] and the caller's bp at stack[bp-2]; init() plants
 * bp == kStackSize+1 as the outermost sentinel. Depth = 1 + chain links.
 */
class KyraInspectorAgent : public Inspector::Agent {
public:
	explicit KyraInspectorAgent(KyraEngine_v1 *vm);

	/** Create the session (no-op unless inspector_enable is set). */
	void init();
	void shutdown();

	bool active() const { return _session != nullptr; }

	/** Registration hooks, called from EMCInterpreter::load()/unload(). */
	void onScriptLoaded(const EMCData *data);
	void onScriptUnloaded(const EMCData *data);

	/** Per-instruction hook; caller checks Inspector::g_session->armed().
	 *  @p byteOffset is ip relative to dataPtr->data in bytes. */
	void onInstruction(EMCState *script, uint32 byteOffset);

	/** Called at the end of EMCInterpreter::run(): closes the liveness
	 *  window on the published activation. Many EMCStates live on the
	 *  engine's native stack, so the adapter only dereferences
	 *  _currentState while the interpreter is provably inside run() for
	 *  it (breakpoint pauses, watch pauses and sysCall-driven pumps all
	 *  happen within that window). */
	void onInstructionDone(EMCState *script);

	/** Watch hooks (EMCInterpreter::op_pushReg/op_popReg, game flags). */
	void onRegisterRead(EMCState *script, int reg, int16 value);
	void onRegisterWrite(EMCState *script, int reg, int16 value);
	void onGameFlagAccess(int flag, bool isWrite, bool value);

	/** Per-frame transport pump, called from KyraEngine_v1::updateInput(). */
	void transportTick();

	// --- Inspector::Agent ---
	Common::String engineId() const override { return "kyra"; }
	Common::String targetTitle() const override;
	void buildCallFrames(uint32 threadId, Common::Array<Inspector::CallFrameInfo> &frames) override;
	void buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
	                      Inspector::RemoteObjectTable &table, int objRef) override;
	bool evaluate(uint32 threadId, int frameIndex, const Common::String &expression,
	              Inspector::RemoteObjectTable &table, Inspector::DebugValue &result) override;
	Common::String describeThread(uint32 threadId) const override;

private:
	struct EMCDataPtrHash {
		uint operator()(const EMCData *ptr) const {
			const uint64 p = (uint64)(uintptr)ptr;
			return (uint)((p >> 4) ^ (p >> 20));
		}
	};
	typedef Common::HashMap<const EMCData *, int, EMCDataPtrHash> HandleMap;

	KyraEngine_v1 *_vm;
	Inspector::Session *_session;

	/** Live script data -> registry handle. Keyed on the EMCData address;
	 *  load()/unload() keep it in sync when the engine reuses the same
	 *  EMCData struct for another file (a reload gets a fresh handle). */
	HandleMap _scriptHandles;

	/** Activation of the most recent hook. Dereferenced only while
	 *  _stateLive, i.e. while EMCInterpreter::run() is executing that
	 *  state (the window between publish() and onInstructionDone());
	 *  outside it the pointer may name a dead stack-allocated EMCState
	 *  and is used solely as a thread-id token. */
	EMCState *_currentState;
	uint32 _currentThreadId;
	bool _stateLive;

	static uint32 threadIdFor(const EMCState *script);
	uint32 currentThreadId() const { return _currentThreadId ? _currentThreadId : 1; }
	void publish(EMCState *script);
	const EMCState *liveState(uint32 threadId) const {
		return (_stateLive && threadId == _currentThreadId) ? _currentState : nullptr;
	}
	int handleFor(const EMCData *data) const;
	uint32 callDepthOf(const EMCState *script) const;
	bool functionEntryIsOffsetByOne() const;
	void buildListing(const EMCData *data, Inspector::ScriptListing &listing) const;
};

} // End of namespace Kyra

#endif
