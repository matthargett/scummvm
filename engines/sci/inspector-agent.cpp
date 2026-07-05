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

#include "sci/inspector-agent.h"

#include "common/config-manager.h"
#include "common/endian.h"
#include "common/inspector/inspector.h"
#include "common/util.h"

#include "sci/sci.h"
#include "sci/debug.h"
#include "sci/engine/kernel.h"
#include "sci/engine/script.h"
#include "sci/engine/scriptdebug.h" // for opcodeNames[]
#include "sci/engine/seg_manager.h"
#include "sci/engine/state.h"
#include "sci/engine/vm.h"
#include "sci/util.h"

namespace Sci {

static const char *const s_varBankNames[4] = { "global", "local", "temp", "param" };

SciInspectorAgent::SciInspectorAgent(SciEngine *vm) : _vm(vm), _session(nullptr) {
}

void SciInspectorAgent::init() {
	_session = Inspector::initSession(this);
}

void SciInspectorAgent::shutdown() {
	if (_session) {
		Inspector::shutdownSession();
		_session = nullptr;
	}
}

Common::String SciInspectorAgent::targetTitle() const {
	return ConfMan.getActiveDomainName() + " (SCI)";
}

Common::String SciInspectorAgent::describeThread(uint32 threadId) const {
	return "SCI p-machine";
}

void SciInspectorAgent::transportTick() {
	if (_session)
		Inspector::transportTick();
}

// --- Script registration -------------------------------------------------

int SciInspectorAgent::handleForScript(Script *scr) {
	const int scriptNr = scr->getScriptNumber();
	Common::HashMap<int, int>::iterator it = _scriptHandles.find(scriptNr);
	if (it != _scriptHandles.end())
		return it->_value;

	Inspector::ScriptListing listing;
	buildListing(scr, listing);
	// A failed registration (-1) is cached too, so a broken script is
	// only attempted once. Script numbers are stable across segment
	// eviction/reload and across kRestartGame16, so the handle map
	// survives both (the rebuilt buffer content is identical).
	const int handle = _session->registerScript(listing);
	_scriptHandles[scriptNr] = handle;
	return handle;
}

int SciInspectorAgent::handleForSegment(SegmentId segment) {
	Script *scr = _vm->getEngineState()->_segMan->getScriptIfLoaded(segment);
	if (!scr)
		return -1;
	return handleForScript(scr);
}

void SciInspectorAgent::onScriptEntered(Script *scr) {
	if (!_session || !scr)
		return;
	handleForScript(scr);
}

// --- VM hooks ------------------------------------------------------------

void SciInspectorAgent::onInstruction() {
	if (!_session)
		return;
	EngineState *s = _vm->getEngineState();
	const reg_t pc = s->xs->addr.pc;
	const int handle = handleForSegment(pc.getSegment());
	if (handle < 0)
		return;
	_session->instructionHook(1, handle, pc.getOffset(), s->_executionStack.size());
}

void SciInspectorAgent::onVariableRead(int type, int index, const reg_t &value) {
	if (!_session || !_session->watchArmed())
		return;
	_session->variableAccessHook(1, Common::String::format("%s%d", s_varBankNames[type & 3], index),
	                             false, valueFor(value));
}

void SciInspectorAgent::onVariableWrite(int type, int index, const reg_t &value) {
	if (!_session || !_session->watchArmed())
		return;
	_session->variableAccessHook(1, Common::String::format("%s%d", s_varBankNames[type & 3], index),
	                             true, valueFor(value));
}

// --- Listing construction ------------------------------------------------

// Bounds-checked replica of readPMachineInstruction() (vm.cpp): computes
// the same byte length and operand values, but returns 0 on opcodes with
// invalid formats or operands running past @p bufSize instead of calling
// error(), so it can be pointed at arbitrary buffer contents.
uint32 SciInspectorAgent::decodeInstruction(const byte *buf, uint32 bufSize, uint32 pos,
                                            byte &extOpcode, int16 opparams[4]) const {
	if (pos >= bufSize)
		return 0;

	uint32 offset = pos;
	extOpcode = buf[offset++];
	const byte opcode = extOpcode >> 1;
	memset(opparams, 0, 4 * sizeof(int16));

	const opcode_format *fmt = _vm->_opcode_formats[opcode];
	for (int i = 0; i < 3 && fmt[i]; ++i) {
		switch (fmt[i]) {
		case Script_Byte:
			if (offset + 1 > bufSize)
				return 0;
			opparams[i] = buf[offset++];
			break;
		case Script_SByte:
			if (offset + 1 > bufSize)
				return 0;
			opparams[i] = (int8)buf[offset++];
			break;

		case Script_Word:
			if (offset + 2 > bufSize)
				return 0;
			opparams[i] = READ_SCI11ENDIAN_UINT16(buf + offset);
			offset += 2;
			break;
		case Script_SWord:
			if (offset + 2 > bufSize)
				return 0;
			opparams[i] = (int16)READ_SCI11ENDIAN_UINT16(buf + offset);
			offset += 2;
			break;

		case Script_Variable:
		case Script_Property:
		case Script_Local:
		case Script_Temp:
		case Script_Global:
		case Script_Param:
		case Script_Offset:
			if (extOpcode & 1) {
				if (offset + 1 > bufSize)
					return 0;
				opparams[i] = buf[offset++];
			} else {
				if (offset + 2 > bufSize)
					return 0;
				opparams[i] = READ_SCI11ENDIAN_UINT16(buf + offset);
				offset += 2;
			}
			break;

		case Script_SVariable:
		case Script_SRelative:
			if (extOpcode & 1) {
				if (offset + 1 > bufSize)
					return 0;
				opparams[i] = (int8)buf[offset++];
			} else {
				if (offset + 2 > bufSize)
					return 0;
				opparams[i] = (int16)READ_SCI11ENDIAN_UINT16(buf + offset);
				offset += 2;
			}
			break;

		case Script_None:
		case Script_End:
			break;

		case Script_Invalid:
		default:
			return 0; // dummy opcode: this is data, not an instruction
		}
	}

	// Mirror readPMachineInstruction()'s special case: a pushSelf with
	// the low bit set is the debug opcode op_file carrying an inline
	// NUL-terminated file name (not in fan-made games, bug #5113).
	if (opcode == op_pushSelf) {
		if (!(extOpcode & 1) || _vm->getGameId() == GID_FANMADE) {
			// genuine pushSelf: no extra bytes
		} else {
			while (offset < bufSize && buf[offset])
				offset++;
			if (offset >= bufSize)
				return 0; // unterminated name: treat as data
			offset++; // consume the NUL
		}
	}

	return offset - pos;
}

Common::String SciInspectorAgent::instructionText(const byte *buf, uint32 pos, uint32 length,
                                                  byte extOpcode, const int16 opparams[4]) const {
	const byte opcode = extOpcode >> 1;
	Common::String text = Common::String::format("[%04x] ", pos);
#ifndef REDUCE_MEMORY_USAGE
	text += opcodeNames[opcode];
#else
	text += Common::String::format("op%02x", opcode);
#endif

	if (opcode == op_pushSelf && (extOpcode & 1) && _vm->getGameId() != GID_FANMADE) {
		// op_file: decodeInstruction() verified the NUL terminator.
		return text + Common::String::format(" \"%s\"", (const char *)buf + pos + 1);
	}

	Kernel *kernel = _vm->getKernel();
	const opcode_format *fmt = _vm->_opcode_formats[opcode];
	for (int i = 0; i < 3 && fmt[i]; ++i) {
		const int16 v = opparams[i];
		switch (fmt[i]) {
		case Script_SByte:
		case Script_SWord:
			text += Common::String::format(" %d", v);
			break;
		case Script_Byte:
			text += Common::String::format(" %02x", (uint16)v & 0xff);
			break;
		case Script_Word:
		case Script_Offset:
			text += Common::String::format(" %04x", (uint16)v);
			break;
		case Script_SRelative:
			// Branch/call/lofs target as an absolute offset (the VM adds
			// the displacement after advancing past the instruction).
			text += Common::String::format(" [%04x]", (uint32)(pos + length + v) & kOffsetMask);
			break;
		case Script_None:
		case Script_End:
			break;
		default:
			// Variable-class operands (global/local/temp/param/property):
			// decimal index; the kernel-call number gets its name.
			if (opcode == op_callk && i == 0 && (uint16)v < kernel->getKernelNamesSize())
				text += Common::String::format(" %s", kernel->getKernelName((uint16)v).c_str());
			else
				text += Common::String::format(" %d", v);
			break;
		}
	}

	// Selector-name hint for pushi, mirroring the console disassembler.
	if (opcode == op_pushi && opparams[0] >= 0 && (uint)opparams[0] < kernel->getSelectorNamesSize())
		text += Common::String::format("\t; %s", kernel->getSelectorName(opparams[0]).c_str());

	return text;
}

void SciInspectorAgent::appendCodeRange(Script *scr, uint32 start, uint32 end,
                                        Inspector::ScriptListing &listing) const {
	const byte *buf = scr->getBuf();
	if (end > scr->getBufSize())
		end = scr->getBufSize();

	uint32 pos = start;
	while (pos < end) {
		byte extOpcode;
		int16 opparams[4];
		const uint32 length = decodeInstruction(buf, end, pos, extOpcode, opparams);
		if (length == 0) {
			// Undecodable byte (data inside a nominal code range, or a
			// truncated tail): emit a raw byte line so offset coverage
			// stays complete, then try to re-sync on the next byte. A
			// stray line can never be stepped onto because only real
			// instruction starts produce hook events.
			listing.lines.push_back(Inspector::ListingLine(
				pos, Common::String::format("[%04x] db %02x", pos, buf[pos]), true));
			pos++;
			continue;
		}
		listing.lines.push_back(Inspector::ListingLine(
			pos, instructionText(buf, pos, length, extOpcode, opparams), true));
		pos += length;
	}
}

void SciInspectorAgent::buildListing(Script *scr, Inspector::ScriptListing &listing) const {
	listing.url = Common::String::format("scummvm-dbg://sci/script-%d", scr->getScriptNumber());
	listing.originalName = Common::String::format("script %d", scr->getScriptNumber());

	// Code never lives in the appended SCI1.1-2.1 heap or the SCI0-early
	// locals area, so the sweep is bounded by getScriptSize(), not
	// getBufSize().
	const byte *buf = scr->getBuf();
	const uint32 scriptSize = scr->getScriptSize();
	const SciVersion version = getSciVersion();

	if (version < SCI_VERSION_1_1) {
		// SCI0/SCI1: block-structured buffer. Decode SCI_OBJ_CODE blocks
		// only; objects, said specs, strings and export tables are
		// sibling blocks that never execute. Mirrors the walk in
		// Script::identifyOffsets()/findBlockSCI0() (headers are always
		// little-endian).
		uint32 pos = (version == SCI_VERSION_0_EARLY) ? 2 : 0;
		while (pos + 4 <= scriptSize) {
			const uint16 blockType = READ_LE_UINT16(buf + pos);
			if (blockType == 0)
				break; // end of block table
			const uint32 blockSize = READ_LE_UINT16(buf + pos + 2);
			if (blockSize < 4 || pos + blockSize > scriptSize)
				break; // malformed block table: stop cleanly
			if (blockType == SCI_OBJ_CODE)
				appendCodeRange(scr, pos + 4, pos + blockSize, listing);
			pos += blockSize;
		}
	} else if (version == SCI_VERSION_3) {
		// SCI3: the header declares the code block (uint32 at 0) and the
		// string area that follows it (uint32 at 4); the relocation
		// table comes after the strings.
		if (scriptSize >= 8) {
			const uint32 codeOffset = READ_LE_UINT32(buf);
			const uint32 stringOffset = READ_LE_UINT32(buf + 4);
			if (codeOffset < stringOffset && stringOffset <= scriptSize)
				appendCodeRange(scr, codeOffset, stringOffset, listing);
		}
	} else {
		// SCI1.1-2.1: code sits between the property/method dictionaries
		// (whose end Script::identifyOffsets() records) and the end of
		// the script part; the relocation table lives in the heap part,
		// past getScriptSize().
		const uint32 codeOffset = scr->getCodeBlockOffset();
		if (codeOffset > 0 && codeOffset < scriptSize)
			appendCodeRange(scr, codeOffset, scriptSize, listing);
	}

	// Safety net for unexpected structures: fall back to a linear decode
	// of the whole script area so an executing pc always has covering
	// listing lines (offsets stay ascending either way).
	if (listing.lines.empty() && scriptSize > 0)
		appendCodeRange(scr, 0, scriptSize, listing);
}

// --- Call frames and scopes ----------------------------------------------

const ExecStack *SciInspectorAgent::execFrame(int frameIndex) const {
	const Common::List<ExecStack> &stack = _vm->getEngineState()->_executionStack;
	if (frameIndex < 0)
		return nullptr;
	int fromFront = (int)stack.size() - 1 - frameIndex; // innermost-first index
	if (fromFront < 0)
		return nullptr;
	Common::List<ExecStack>::const_iterator it = stack.begin();
	while (fromFront--)
		++it;
	return &*it;
}

Common::String SciInspectorAgent::frameFunctionName(const ExecStack &call) const {
	SegManager *segMan = _vm->getEngineState()->_segMan;
	Kernel *kernel = _vm->getKernel();

	switch (call.type) {
	case EXEC_STACK_TYPE_CALL: {
		Script *scr = segMan->getScriptIfLoaded(call.addr.pc.getSegment());
		const int scriptNr = scr ? scr->getScriptNumber() : -1;
		if (call.debugSelector != -1)
			return Common::String::format("%s::%s", segMan->getObjectName(call.sendp),
			                              kernel->getSelectorName(call.debugSelector).c_str());
		if (call.debugExportId != -1)
			return Common::String::format("script-%d:export%d", scriptNr, call.debugExportId);
		if (call.debugLocalCallOffset != -1)
			return Common::String::format("script-%d:call_%04x", scriptNr, call.debugLocalCallOffset);
		return Common::String::format("script-%d", scriptNr);
	}

	case EXEC_STACK_TYPE_KERNEL:
		if (call.debugKernelSubFunction == -1)
			return Common::String::format("k%s", kernel->getKernelName(call.debugKernelFunction).c_str());
		return Common::String::format("k%s",
		                              kernel->getKernelName(call.debugKernelFunction, call.debugKernelSubFunction).c_str());

	case EXEC_STACK_TYPE_VARSELECTOR:
		return Common::String::format("%s::%s [var%s]", segMan->getObjectName(call.sendp),
		                              kernel->getSelectorName(call.debugSelector).c_str(),
		                              call.argc ? "write" : "read");

	default:
		return "<unknown frame>";
	}
}

void SciInspectorAgent::frameScopes(const ExecStack &call,
                                    Common::Array<Inspector::ScopeInfo> &scopes) const {
	// Layout must stay in sync with buildScopeObject(): CALL frames get
	// [Locals, Temps, Params, Registers, Globals], kernel/varselector
	// frames [Params, Registers, Globals].
	Inspector::ScopeInfo scope;
	if (call.type == EXEC_STACK_TYPE_CALL) {
		scope.type = "local";
		scope.name = "Locals";
		scopes.push_back(scope);
		scope.type = "local";
		scope.name = "Temps";
		scopes.push_back(scope);
	}
	scope.type = "local";
	scope.name = "Params";
	scopes.push_back(scope);
	scope.type = "local";
	scope.name = "Registers";
	scopes.push_back(scope);
	scope.type = "global";
	scope.name = "Globals";
	scopes.push_back(scope);
}

void SciInspectorAgent::buildCallFrames(uint32 threadId,
                                        Common::Array<Inspector::CallFrameInfo> &frames) {
	EngineState *s = _vm->getEngineState();

	// _executionStack grows caller-first; CDP wants innermost first.
	Common::Array<const ExecStack *> stack;
	for (Common::List<ExecStack>::const_iterator it = s->_executionStack.begin();
	     it != s->_executionStack.end(); ++it)
		stack.push_back(&*it);

	for (int i = (int)stack.size() - 1; i >= 0; i--) {
		const ExecStack &call = *stack[i];
		Inspector::CallFrameInfo frame;
		frame.functionName = frameFunctionName(call);
		if (call.type == EXEC_STACK_TYPE_CALL) {
			// For the innermost frame addr.pc is the live pc (run_vm
			// updates it in place); outer frames hold their resume pc.
			frame.scriptHandle = handleForSegment(call.addr.pc.getSegment());
			frame.offset = call.addr.pc.getOffset();
		} // kernel/varselector frames keep the default handle -1/offset 0
		frameScopes(call, frame.scopes);
		frames.push_back(frame);
	}
}

Inspector::DebugValue SciInspectorAgent::valueFor(const reg_t &r) const {
	if (r.getSegment() == 0)
		return Inspector::DebugValue::fromInt(r.toSint16());
	if (r.getSegment() == kUninitializedSegment)
		return Inspector::DebugValue::fromString(
			Common::String::format("%04x:%04x (uninitialized)", PRINT_REG(r)));

	SegManager *segMan = _vm->getEngineState()->_segMan;
	if (segMan->isObject(r))
		return Inspector::DebugValue::fromString(
			Common::String::format("%04x:%04x (%s)", PRINT_REG(r), segMan->getObjectName(r)));
	return Inspector::DebugValue::fromString(Common::String::format("%04x:%04x", PRINT_REG(r)));
}

void SciInspectorAgent::buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
                                         Inspector::RemoteObjectTable &table, int objRef) {
	EngineState *s = _vm->getEngineState();
	const ExecStack *call = execFrame(frameIndex);
	if (!call)
		return;

	enum ScopeKind { kScopeLocals, kScopeTemps, kScopeParams, kScopeRegisters, kScopeGlobals };
	static const ScopeKind callLayout[] = { kScopeLocals, kScopeTemps, kScopeParams, kScopeRegisters, kScopeGlobals };
	static const ScopeKind otherLayout[] = { kScopeParams, kScopeRegisters, kScopeGlobals };

	ScopeKind kind;
	if (call->type == EXEC_STACK_TYPE_CALL) {
		if (scopeIndex < 0 || scopeIndex >= ARRAYSIZE(callLayout))
			return;
		kind = callLayout[scopeIndex];
	} else {
		if (scopeIndex < 0 || scopeIndex >= ARRAYSIZE(otherLayout))
			return;
		kind = otherLayout[scopeIndex];
	}

	switch (kind) {
	case kScopeLocals: {
		Script *local = s->_segMan->getScriptIfLoaded(call->local_segment);
		if (!local)
			return;
		const reg_t *locals = local->getLocalsBegin();
		const int count = local->getLocalsCount();
		for (int i = 0; locals && i < count; i++)
			table.addProperty(objRef, Common::String::format("local%d", i), valueFor(locals[i]));
		break;
	}

	case kScopeTemps: {
		const reg_t *fp = call->fp;
		if (!fp || fp < s->stack_base || fp + call->tempCount > s->stack_top)
			return;
		for (int i = 0; i < call->tempCount; i++)
			table.addProperty(objRef, Common::String::format("temp%d", i), valueFor(fp[i]));
		break;
	}

	case kScopeParams: {
		const reg_t *argp = call->variables_argp;
		if (!argp || argp < s->stack_base || argp + call->argc + 1 > s->stack_top)
			return;
		// param0 is the argument count by p-machine convention.
		for (int i = 0; i <= call->argc; i++)
			table.addProperty(objRef, Common::String::format("param%d", i), valueFor(argp[i]));
		break;
	}

	case kScopeRegisters:
		table.addProperty(objRef, "acc", valueFor(s->r_acc));
		table.addProperty(objRef, "prev", valueFor(s->r_prev));
		table.addProperty(objRef, "rest", Inspector::DebugValue::fromInt(s->r_rest));
		if (call->type == EXEC_STACK_TYPE_CALL)
			table.addProperty(objRef, "pc", Inspector::DebugValue::fromString(
				Common::String::format("%04x:%04x", PRINT_REG(call->addr.pc))));
		break;

	case kScopeGlobals: {
		const reg_t *globals = s->variables[VAR_GLOBAL];
		const int count = s->variablesMax[VAR_GLOBAL];
		for (int i = 0; globals && i < count; i++)
			table.addProperty(objRef, Common::String::format("global%d", i), valueFor(globals[i]));
		break;
	}

	default:
		break;
	}
}

// --- evaluate() ------------------------------------------------------------

bool SciInspectorAgent::parseVarRef(const Common::String &name, int &type, int &index) const {
	for (int bank = 0; bank < 4; bank++) {
		const uint len = strlen(s_varBankNames[bank]);
		if (name.size() > len && name.hasPrefix(s_varBankNames[bank]) &&
		    Common::isDigit(name[len])) {
			type = bank;
			index = atoi(name.c_str() + len);
			return true;
		}
	}
	return false;
}

bool SciInspectorAgent::readVarRef(int type, int index, Inspector::DebugValue &result) const {
	EngineState *s = _vm->getEngineState();
	if (!s)
		return false;
	// Globals are bound once at initGlobals(); the other banks follow the
	// current innermost frame and are only valid while the VM is running
	// a call (paused hooks and breakpoint conditions included).
	if (type != VAR_GLOBAL && s->_executionStack.empty())
		return false;
	if (!s->variables[type] || index < 0 || index >= s->variablesMax[type])
		return false;
	result = valueFor(s->variables[type][index]);
	return true;
}

bool SciInspectorAgent::evaluate(uint32 threadId, int frameIndex,
                                 const Common::String &expression,
                                 Inspector::RemoteObjectTable &table,
                                 Inspector::DebugValue &result) {
	// Supported forms: global<N>/local<N>/temp<N>/param<N>/acc/prev reads,
	// '<name> = <int>' pokes (stored as segment-0 integers) and
	// '<name> == <int>' comparisons for breakpoint conditions. The
	// local/temp/param banks always resolve against the innermost frame.
	EngineState *s = _vm->getEngineState();
	if (!s)
		return false;

	Common::String expr = expression;
	expr.trim();

	// Assignment? (single '=', not '==')
	uint32 eq = 0;
	bool hasAssign = false;
	for (uint32 i = 0; i < expr.size(); i++) {
		if (expr[i] == '=' && (i + 1 >= expr.size() || expr[i + 1] != '=')) {
			eq = i;
			hasAssign = true;
			break;
		}
		if (expr[i] == '=' && i + 1 < expr.size() && expr[i + 1] == '=')
			break; // comparison, not assignment
	}
	if (hasAssign) {
		Common::String lhs(expr.c_str(), eq);
		Common::String rhs(expr.c_str() + eq + 1);
		lhs.trim();
		rhs.trim();
		const reg_t value = make_reg(0, (uint16)atoi(rhs.c_str()));
		if (lhs == "acc") {
			s->r_acc = value;
			result = valueFor(s->r_acc);
			return true;
		}
		int type, index;
		if (!parseVarRef(lhs, type, index))
			return false;
		if (type != VAR_GLOBAL && s->_executionStack.empty())
			return false;
		if (!s->variables[type] || index < 0 || index >= s->variablesMax[type])
			return false;
		s->variables[type][index] = value;
		result = valueFor(s->variables[type][index]);
		return true;
	}

	// Comparison "name == K" (breakpoint conditions).
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
		result = Inspector::DebugValue::fromBool(lhsVal.type == Inspector::DebugValue::kInt &&
		                                         lhsVal.intVal == atoi(rhs.c_str()));
		return true;
	}

	if (expr == "acc") {
		result = valueFor(s->r_acc);
		return true;
	}
	if (expr == "prev") {
		result = valueFor(s->r_prev);
		return true;
	}

	int type, index;
	if (parseVarRef(expr, type, index))
		return readVarRef(type, index, result);
	return false;
}

} // End of namespace Sci
