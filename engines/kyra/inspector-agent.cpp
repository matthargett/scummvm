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

#include "kyra/inspector-agent.h"

#include "kyra/kyra_v1.h"

#include "common/config-manager.h"
#include "common/inspector/inspector.h"
#include "common/util.h"

namespace Kyra {

// KyraEngine_v1::_flagsTable is uint8[100], bit-addressed (8 flags/byte).
enum {
	kGameFlagCount = 100 * 8
};

// Mirror of the OPCODE table names in EMCInterpreter's constructor
// (kyra/script/script.cpp); the table itself is a protected member, so
// the 19 fixed names are replicated here.
static const char *const kEMCOpcodeNames[19] = {
	"op_jmp",          // 0x00
	"op_setRetValue",
	"op_pushRetOrPos",
	"op_push",
	"op_push",         // 0x04
	"op_pushReg",
	"op_pushBPNeg",
	"op_pushBPAdd",
	"op_popRetOrPos",  // 0x08
	"op_popReg",
	"op_popBPNeg",
	"op_popBPAdd",
	"op_addSP",        // 0x0C
	"op_subSP",
	"op_sysCall",
	"op_ifNotJmp",
	"op_negate",       // 0x10
	"op_eval",
	"op_setRetAndJmp"
};

KyraInspectorAgent::KyraInspectorAgent(KyraEngine_v1 *vm) :
	_vm(vm), _session(nullptr), _currentState(nullptr), _currentThreadId(0), _stateLive(false) {
}

void KyraInspectorAgent::init() {
	_session = Inspector::initSession(this);
}

void KyraInspectorAgent::shutdown() {
	if (_session) {
		Inspector::shutdownSession();
		_session = nullptr;
	}
}

Common::String KyraInspectorAgent::targetTitle() const {
	return ConfMan.getActiveDomainName() + " (KYRA)";
}

Common::String KyraInspectorAgent::describeThread(uint32 threadId) const {
	const EMCState *s = liveState(threadId);
	if (s && s->dataPtr)
		return Common::String::format("EMC %s (state %08x)", s->dataPtr->filename, threadId);
	return Common::String::format("emc-state-%08x", threadId);
}

// Fold an EMCState address into a 32-bit inspector thread id. Stable
// while the activation object lives; a stack-allocated EMCState that
// dies and a later one at the same address share an id ("stable-ish",
// see the class comment). Never returns 0.
uint32 KyraInspectorAgent::threadIdFor(const EMCState *script) {
	const uint64 p = (uint64)(uintptr)script;
	const uint32 id = (uint32)(p ^ (p >> 32));
	return id ? id : 1;
}

int KyraInspectorAgent::handleFor(const EMCData *data) const {
	HandleMap::const_iterator it = _scriptHandles.find(data);
	if (it == _scriptHandles.end())
		return -1;
	return it->_value;
}

// Depth of the in-band call chain: init() plants bp == kStackSize+1 as
// the outermost sentinel; each op_pushRetOrPos(1) frame saved the
// caller's bp at stack[bp-2]. Bail out on corrupt chains.
uint32 KyraInspectorAgent::callDepthOf(const EMCState *script) const {
	uint32 depth = 1;
	uint16 bp = script->bp;
	while (depth < 32) {
		if (bp == EMCState::kStackSize + 1)
			break; // sentinel: outermost activation
		if (bp < 2 || bp > EMCState::kStackSize)
			break; // corrupt chain
		bp = (uint16)script->stack[bp - 2];
		depth++;
	}
	return depth;
}

// EMCInterpreter::start() begins execution at ordr[n]+1 instead of
// ordr[n] for everything but the plain Kyra 1 versions; mirror that so
// function entry offsets in the listing match real PC values.
bool KyraInspectorAgent::functionEntryIsOffsetByOne() const {
	if (_vm->game() != GI_KYRA1)
		return true;
	return _vm->gameFlags().platform == Common::kPlatformFMTowns ||
	       _vm->gameFlags().platform == Common::kPlatformPC98 ||
	       _vm->gameFlags().lang == Common::KO_KOR;
}

// Static disassembly of the DATA chunk. The word stream is decoded
// exactly like EMCInterpreter::run() (opcode/parameter packing including
// the two-word 0x2000 form); every line's offset is the BYTE offset of
// the instruction's first word. Function entries from the ORDR table are
// decode resync points, so a mis-decoded two-word instruction can never
// swallow a function's first word; words that don't decode (padding,
// truncated parameters) become "dw" lines. All lines are statements —
// only real instruction starts ever generate hook events, so stepping
// still lands correctly (documented byte-granularity fallback).
void KyraInspectorAgent::buildListing(const EMCData *data, Inspector::ScriptListing &listing) const {
	listing.url = Common::String::format("scummvm-dbg://kyra/%s", data->filename);

	const uint16 *code = data->data;
	const uint32 wordCount = data->dataSize / 2;
	const uint32 funcCount = data->ordrSize / 2;
	const bool entryPlusOne = functionEntryIsOffsetByOne();

	bool *isResync = new bool[wordCount]();
	typedef Common::HashMap<uint32, Common::String> LabelMap;
	LabelMap labels;

	for (uint32 func = 0; func < funcCount; func++) {
		const uint16 ofs = data->ordr[func];
		if (ofs == 0xFFFF)
			continue;
		if (ofs < wordCount) {
			isResync[ofs] = true;
			if (labels.contains(ofs))
				labels[ofs] += Common::String::format(", %u", func);
			else
				labels[ofs] = Common::String::format("%u", func);
		}
		const uint32 entry = ofs + (entryPlusOne ? 1 : 0);
		if (entry < wordCount)
			isResync[entry] = true;
	}

	for (uint32 pos = 0; pos < wordCount;) {
		const uint32 byteOfs = pos * 2;

		LabelMap::const_iterator label = labels.find(pos);
		if (label != labels.end())
			listing.lines.push_back(Inspector::ListingLine(byteOfs,
				Common::String::format("; -------- function %s --------", label->_value.c_str()), false));

		// Decode mirror of EMCInterpreter::run().
		const uint16 codeWord = code[pos];
		int opcode;
		int16 parameter;
		uint32 size = 1;

		if (codeWord & 0x8000) {
			opcode = 0;
			parameter = (int16)(codeWord & 0x7FFF);
		} else {
			opcode = (codeWord >> 8) & 0x1F;
			if (codeWord & 0x4000) {
				parameter = (int8)codeWord;
			} else if (codeWord & 0x2000) {
				if (pos + 1 < wordCount && !isResync[pos + 1]) {
					parameter = (int16)code[pos + 1];
					size = 2;
				} else {
					// The parameter word would cross a function boundary
					// (or the chunk end): not really an instruction.
					opcode = -1;
					parameter = 0;
				}
			} else {
				parameter = 0;
			}
		}

		Common::String text;
		if (opcode < 0 || opcode > 18)
			text = Common::String::format("[%04x] dw 0x%04x", byteOfs, codeWord);
		else if (opcode == 0x00) // op_jmp: parameter is a WORD index
			text = Common::String::format("[%04x] op_jmp 0x%04x", byteOfs, (uint32)((uint16)parameter & 0x7FFF) * 2);
		else if (opcode == 0x0F) // op_ifNotJmp: dito
			text = Common::String::format("[%04x] op_ifNotJmp 0x%04x", byteOfs, (uint32)((uint16)parameter & 0x7FFF) * 2);
		else
			text = Common::String::format("[%04x] %s %d", byteOfs, kEMCOpcodeNames[opcode], parameter);

		listing.lines.push_back(Inspector::ListingLine(byteOfs, text, true));
		pos += size;
	}

	delete[] isResync;
}

void KyraInspectorAgent::onScriptLoaded(const EMCData *data) {
	if (!_session || !data || !data->data || !data->dataSize)
		return;
	Inspector::ScriptListing listing;
	buildListing(data, listing);
	// Reusing the same EMCData for another file simply overwrites the
	// mapping; the reloaded script gets a fresh handle (V8 re-parse model).
	_scriptHandles.setVal(data, _session->registerScript(listing));
}

void KyraInspectorAgent::onScriptUnloaded(const EMCData *data) {
	if (!data)
		return;
	_scriptHandles.erase(data);
	if (_currentState && _currentState->dataPtr == data) {
		_currentState = nullptr;
		_currentThreadId = 0;
		_stateLive = false;
	}
}

// Open the liveness window: @p script is executing inside
// EMCInterpreter::run() right now and stays dereferenceable until the
// matching onInstructionDone().
void KyraInspectorAgent::publish(EMCState *script) {
	_currentState = script;
	_currentThreadId = threadIdFor(script);
	_stateLive = true;
}

void KyraInspectorAgent::onInstruction(EMCState *script, uint32 byteOffset) {
	if (!_session)
		return;

	int handle = handleFor(script->dataPtr);
	if (handle < 0) {
		// Normally registered by EMCInterpreter::load(); lazy fallback in
		// case a script data block reached execution by another path.
		onScriptLoaded(script->dataPtr);
		handle = handleFor(script->dataPtr);
		if (handle < 0)
			return;
	}

	// Publish the activation first: the session may block inside
	// instructionHook() and pull frames/scopes from _currentState.
	publish(script);
	_session->instructionHook(_currentThreadId, handle, byteOffset, callDepthOf(script));
}

void KyraInspectorAgent::onInstructionDone(EMCState *script) {
	if (_currentState == script)
		_stateLive = false;
}

void KyraInspectorAgent::onRegisterRead(EMCState *script, int reg, int16 value) {
	if (!_session || !_session->watchArmed())
		return;
	publish(script); // we are inside script's opcode handler
	_session->variableAccessHook(_currentThreadId, Common::String::format("r%d", reg),
	                             false, Inspector::DebugValue::fromInt(value));
}

void KyraInspectorAgent::onRegisterWrite(EMCState *script, int reg, int16 value) {
	if (!_session || !_session->watchArmed())
		return;
	publish(script); // we are inside script's opcode handler
	_session->variableAccessHook(_currentThreadId, Common::String::format("r%d", reg),
	                             true, Inspector::DebugValue::fromInt(value));
}

void KyraInspectorAgent::onGameFlagAccess(int flag, bool isWrite, bool value) {
	if (!_session || !_session->watchArmed())
		return;
	_session->variableAccessHook(currentThreadId(), Common::String::format("f%d", flag),
	                             isWrite, Inspector::DebugValue::fromBool(value));
}

void KyraInspectorAgent::transportTick() {
	if (_session)
		Inspector::transportTick();
}

void KyraInspectorAgent::buildCallFrames(uint32 threadId,
                                         Common::Array<Inspector::CallFrameInfo> &frames) {
	const EMCState *s = liveState(threadId);
	if (!s || !s->dataPtr)
		return;

	const char *filename = s->dataPtr->filename;
	const int handle = handleFor(s->dataPtr);

	Common::Array<uint32> offsets;

	// Innermost frame: ip still points at the paused instruction.
	if (s->ip)
		offsets.push_back((uint32)((const byte *)s->ip - (const byte *)s->dataPtr->data));
	else
		offsets.push_back(0);

	// Outer frames from the in-band bp chain: stack[bp-1] holds the
	// return position pushed by op_pushRetOrPos(1) as "ip - data + 1" —
	// a WORD index (uint16* difference); convert to byte offsets.
	uint16 bp = s->bp;
	for (int guard = 0; guard < 32; guard++) {
		if (bp == EMCState::kStackSize + 1)
			break; // sentinel: outermost activation
		if (bp < 2 || bp > EMCState::kStackSize)
			break; // corrupt chain
		offsets.push_back((uint32)((uint16)s->stack[bp - 1]) * 2);
		bp = (uint16)s->stack[bp - 2];
	}

	for (uint32 i = 0; i < offsets.size(); i++) {
		Inspector::CallFrameInfo frame;
		frame.functionName = Common::String::format("%s+0x%04x", filename, offsets[i]);
		frame.scriptHandle = handle;
		frame.offset = offsets[i];

		Inspector::ScopeInfo regs;
		regs.type = "local";
		regs.name = "Registers";
		frame.scopes.push_back(regs);
		Inspector::ScopeInfo stack;
		stack.type = "local";
		stack.name = "Stack";
		frame.scopes.push_back(stack);
		Inspector::ScopeInfo flags;
		flags.type = "global";
		flags.name = "Game flags (set)";
		frame.scopes.push_back(flags);

		frames.push_back(frame);
	}
}

void KyraInspectorAgent::buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
                                          Inspector::RemoteObjectTable &table, int objRef) {
	if (scopeIndex == 2) {
		// 800 bit flags is noise; show only the set ones. Reads go
		// through queryGameFlag() like the script opcodes do.
		for (int flag = 0; flag < kGameFlagCount; flag++)
			if (_vm->queryGameFlag(flag))
				table.addProperty(objRef, Common::String::format("f%d", flag),
				                  Inspector::DebugValue::fromBool(true));
		return;
	}

	const EMCState *s = liveState(threadId);
	if (!s)
		return;

	if (scopeIndex == 0) {
		for (int i = 0; i < ARRAYSIZE(s->regs); i++)
			table.addProperty(objRef, Common::String::format("r%d", i),
			                  Inspector::DebugValue::fromInt(s->regs[i]));
		table.addProperty(objRef, "retValue", Inspector::DebugValue::fromInt(s->retValue));
	} else if (scopeIndex == 1) {
		// All frames share one VM stack; show sp/bp plus a bounded
		// window from the top of the stack (it grows downward).
		table.addProperty(objRef, "sp", Inspector::DebugValue::fromInt(s->sp));
		table.addProperty(objRef, "bp", Inspector::DebugValue::fromInt(s->bp));
		const int last = MIN<int>((int)s->sp + 15, EMCState::kStackLastEntry);
		for (int i = s->sp; i >= 0 && i <= last; i++)
			table.addProperty(objRef, Common::String::format("stack[%d]", i),
			                  Inspector::DebugValue::fromInt(s->stack[i]));
	}
}

bool KyraInspectorAgent::evaluate(uint32 threadId, int frameIndex,
                                  const Common::String &expression,
                                  Inspector::RemoteObjectTable &table,
                                  Inspector::DebugValue &result) {
	// Supported forms: rN / fN / retValue / sp / bp / stack[N] reads,
	// "rN = <int>" and "fN = 0|1" writes, and "<lhs> == <int>"
	// comparisons — enough for watch panes, breakpoint conditions and
	// console pokes.
	Common::String expr = expression;
	expr.trim();
	if (expr.empty())
		return false;

	// Registers/stack come from the currently executing activation
	// (whatever thread asked): meaningful while paused or while inside a
	// sysCall-driven pump, null once the activation may be gone.
	const EMCState *s = _stateLive ? _currentState : nullptr;

	// Assignment? (single '=', not '==')
	uint32 eq = 0;
	bool hasAssign = false;
	for (uint32 i = 0; i < expr.size(); i++) {
		if (expr[i] == '=') {
			if (i + 1 < expr.size() && expr[i + 1] == '=')
				break; // comparison, not assignment
			eq = i;
			hasAssign = true;
			break;
		}
	}
	if (hasAssign) {
		Common::String lhs(expr.c_str(), eq);
		Common::String rhs(expr.c_str() + eq + 1);
		lhs.trim();
		rhs.trim();
		const int value = atoi(rhs.c_str());
		if (lhs.size() >= 2 && lhs[0] == 'r' && Common::isDigit(lhs[1]) && s) {
			const int reg = atoi(lhs.c_str() + 1);
			if (reg >= 0 && reg < ARRAYSIZE(s->regs)) {
				// The paused activation is alive (the VM blocks inside
				// the hook), so poking its registers is safe.
				const_cast<EMCState *>(s)->regs[reg] = (int16)value;
				result = Inspector::DebugValue::fromInt(s->regs[reg]);
				return true;
			}
		}
		if (lhs.size() >= 2 && lhs[0] == 'f' && Common::isDigit(lhs[1])) {
			const int flag = atoi(lhs.c_str() + 1);
			if (flag >= 0 && flag < kGameFlagCount) {
				if (value)
					_vm->setGameFlag(flag);
				else
					_vm->resetGameFlag(flag);
				result = Inspector::DebugValue::fromBool(_vm->queryGameFlag(flag) != 0);
				return true;
			}
		}
		return false;
	}

	// Comparison "<lhs> == <int>" (breakpoint conditions).
	if (expr.contains("==")) {
		uint32 opPos = 0;
		for (uint32 i = 0; i + 1 < expr.size(); i++) {
			if (expr[i] == '=' && expr[i + 1] == '=') {
				opPos = i;
				break;
			}
		}
		Common::String lhs(expr.c_str(), opPos);
		Common::String rhs(expr.c_str() + opPos + 2);
		lhs.trim();
		rhs.trim();
		Inspector::DebugValue lhsVal;
		if (!evaluate(threadId, frameIndex, lhs, table, lhsVal))
			return false;
		const int64 lv = (lhsVal.type == Inspector::DebugValue::kBool) ? (lhsVal.boolVal ? 1 : 0) : lhsVal.intVal;
		result = Inspector::DebugValue::fromBool(lv == atoi(rhs.c_str()));
		return true;
	}

	// Named VM state.
	if (expr == "retValue" && s) {
		result = Inspector::DebugValue::fromInt(s->retValue);
		return true;
	}
	if (expr == "sp" && s) {
		result = Inspector::DebugValue::fromInt(s->sp);
		return true;
	}
	if (expr == "bp" && s) {
		result = Inspector::DebugValue::fromInt(s->bp);
		return true;
	}

	if (expr.size() >= 2 && expr[0] == 'r' && Common::isDigit(expr[1]) && s) {
		const int reg = atoi(expr.c_str() + 1);
		if (reg >= 0 && reg < ARRAYSIZE(s->regs)) {
			result = Inspector::DebugValue::fromInt(s->regs[reg]);
			return true;
		}
	}
	if (expr.size() >= 2 && expr[0] == 'f' && Common::isDigit(expr[1])) {
		const int flag = atoi(expr.c_str() + 1);
		if (flag >= 0 && flag < kGameFlagCount) {
			result = Inspector::DebugValue::fromBool(_vm->queryGameFlag(flag) != 0);
			return true;
		}
	}
	if (expr.hasPrefix("stack[") && expr.lastChar() == ']' && s) {
		const int idx = atoi(expr.c_str() + 6);
		if (idx >= 0 && idx < EMCState::kStackSize) {
			result = Inspector::DebugValue::fromInt(s->stack[idx]);
			return true;
		}
	}
	return false;
}

} // End of namespace Kyra
