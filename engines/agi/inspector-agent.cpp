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

#include "agi/inspector-agent.h"

#include "agi/agi.h"
#include "common/config-manager.h"
#include "common/inspector/inspector.h"

namespace Agi {

AgiInspectorAgent::AgiInspectorAgent(AgiEngine *vm) : _vm(vm), _session(nullptr) {
}

void AgiInspectorAgent::init() {
	_session = Inspector::initSession(this);
}

void AgiInspectorAgent::shutdown() {
	if (_session) {
		Inspector::shutdownSession();
		_session = nullptr;
	}
}

Common::String AgiInspectorAgent::targetTitle() const {
	return ConfMan.getActiveDomainName() + " (AGI)";
}

Common::String AgiInspectorAgent::describeThread(uint32 threadId) const {
	return "AGI interpreter";
}

int AgiInspectorAgent::handleFor(int16 logicNr) const {
	Common::HashMap<int16, int>::const_iterator it = _logicHandles.find(logicNr);
	if (it == _logicHandles.end())
		return -1;
	return it->_value;
}

// Skip a test clause: after an 0xFF "if" opcode, test tokens run until
// the closing 0xFF, followed by a 2-byte LE jump size. Token sizes must
// mirror AgiEngine::skipInstruction() exactly, or listing offsets drift
// off the interpreter's real cIP values.
uint32 AgiInspectorAgent::testClauseEnd(const uint8 *data, uint32 size, uint32 pos) const {
	const AgiOpCodeEntry *tests = _vm->getOpCodesCondTable();
	while (pos < size) {
		uint8 op = data[pos++];
		if (op == 0xFF)
			return (pos + 2 <= size) ? pos + 2 : size; // closing 0xFF + jump word
		if (op == 0xFC || op == 0xFD)
			continue; // OR toggle / NOT prefix carry no arguments
		if (op == 0x0E && _vm->getVersion() >= 0x2000) {
			// said: one count byte, then count 16-bit word numbers
			if (pos >= size)
				return size;
			pos += (uint32)data[pos] * 2 + 1;
		} else {
			pos += tests[op].parameterSize;
		}
	}
	return size;
}

void AgiInspectorAgent::buildListing(int16 logicNr, Inspector::ScriptListing &listing) const {
	const AgiLogic &logic = _vm->_game.logics[logicNr];
	const AgiOpCodeEntry *ops = _vm->getOpCodesTable();
	listing.url = Common::String::format("scummvm-dbg://agi/logic-%d", logicNr);

	const uint8 *data = logic.data;
	const uint32 size = (uint32)logic.size;
	// Bytecode starts after the 2-byte size header (decodeLogic: sIP = 2).
	uint32 pos = 2;
	while (pos < size) {
		const uint32 start = pos;
		const uint8 op = data[pos++];
		Common::String text;
		if (op == 0xFF) {
			const uint32 end = testClauseEnd(data, size, pos);
			text = Common::String::format("[%04x] if (", start);
			for (uint32 i = pos; i < end && i + 2 < end; i++)
				text += Common::String::format("%02x ", data[i]);
			text += ")";
			pos = end;
		} else if (op == 0xFE) {
			if (pos + 2 <= size) {
				int16 jump = (int16)READ_LE_UINT16(data + pos);
				pos += 2;
				text = Common::String::format("[%04x] goto %04x", start, pos + jump);
			} else {
				pos = size;
				text = Common::String::format("[%04x] goto <truncated>", start);
			}
		} else if (op == 0x00) {
			text = Common::String::format("[%04x] return", start);
		} else {
			text = Common::String::format("[%04x] %s(", start, ops[op].name);
			const uint32 argCount = ops[op].parameterSize;
			for (uint32 i = 0; i < argCount && pos + i < size; i++) {
				if (i)
					text += ", ";
				text += Common::String::format("%d", data[pos + i]);
			}
			text += ")";
			pos += argCount;
		}
		// Every AGI instruction is a statement (one action per line).
		listing.lines.push_back(Inspector::ListingLine(start, text, true));
	}
}

void AgiInspectorAgent::ensureLogicRegistered(int16 logicNr) {
	if (!_session || _logicHandles.contains(logicNr))
		return;
	if (!_vm->_game.logics[logicNr].data)
		return; // not loaded yet
	Inspector::ScriptListing listing;
	buildListing(logicNr, listing);
	_logicHandles[logicNr] = _session->registerScript(listing);
}

void AgiInspectorAgent::onInstruction() {
	if (!_session)
		return;
	const int16 logicNr = _vm->_game.curLogicNr;
	int handle = handleFor(logicNr);
	if (handle < 0)
		return;
	_session->instructionHook(1, handle, (uint32)_vm->_game._curLogic->cIP,
	                          _vm->_game.execStack.size());
}

void AgiInspectorAgent::onVariableRead(int16 varNr, byte value) {
	if (!_session || !_session->watchArmed())
		return;
	_session->variableAccessHook(1, Common::String::format("v%d", varNr), false,
	                             Inspector::DebugValue::fromInt(value));
}

void AgiInspectorAgent::onVariableWrite(int16 varNr, byte newValue) {
	if (!_session || !_session->watchArmed())
		return;
	_session->variableAccessHook(1, Common::String::format("v%d", varNr), true,
	                             Inspector::DebugValue::fromInt(newValue));
}

void AgiInspectorAgent::transportTick() {
	if (_session)
		Inspector::transportTick();
}

void AgiInspectorAgent::buildCallFrames(uint32 threadId,
                                        Common::Array<Inspector::CallFrameInfo> &frames) {
	// _game.execStack grows caller-first; CDP wants innermost first.
	const Common::Array<ScriptPos> &stack = _vm->_game.execStack;
	for (int i = (int)stack.size() - 1; i >= 0; i--) {
		Inspector::CallFrameInfo frame;
		frame.functionName = Common::String::format("logic-%d", stack[i].script);
		frame.scriptHandle = handleFor((int16)stack[i].script);
		frame.offset = (uint32)stack[i].curIP;
		Inspector::ScopeInfo vars;
		vars.type = "global";
		vars.name = "Variables";
		frame.scopes.push_back(vars);
		Inspector::ScopeInfo flags;
		flags.type = "global";
		flags.name = "Flags (set)";
		frame.scopes.push_back(flags);
		Inspector::ScopeInfo strings;
		strings.type = "global";
		strings.name = "Strings";
		frame.scopes.push_back(strings);
		frames.push_back(frame);
	}
}

void AgiInspectorAgent::buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
                                         Inspector::RemoteObjectTable &table, int objRef) {
	if (scopeIndex == 0) {
		for (int i = 0; i < MAX_VARS; i++)
			table.addProperty(objRef, Common::String::format("v%d", i),
			                  Inspector::DebugValue::fromInt(_vm->getVar((int16)i)));
	} else if (scopeIndex == 1) {
		// 256 flags is noise; show only the set ones. (MAX_FLAGS counts
		// the packed bytes, 8 flags each.)
		for (int i = 0; i < MAX_FLAGS * 8; i++)
			if (_vm->getFlag((int16)i))
				table.addProperty(objRef, Common::String::format("f%d", i),
				                  Inspector::DebugValue::fromBool(true));
	} else {
		for (int i = 0; i < MAX_STRINGS + 1; i++)
			if (_vm->_game.strings[i][0])
				table.addProperty(objRef, Common::String::format("s%d", i),
				                  Inspector::DebugValue::fromString(_vm->_game.strings[i]));
	}
}

bool AgiInspectorAgent::evaluate(uint32 threadId, int frameIndex,
                                 const Common::String &expression,
                                 Inspector::RemoteObjectTable &table,
                                 Inspector::DebugValue &result) {
	// Supported forms: vN / fN / sN reads and "vN = <int>" writes —
	// enough for watch panes, breakpoint conditions and console pokes.
	Common::String expr = expression;
	expr.trim();

	// Assignment?
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
		if (lhs.size() >= 2 && lhs[0] == 'v') {
			int varNr = atoi(lhs.c_str() + 1);
			int value = atoi(rhs.c_str());
			if (varNr >= 0 && varNr < MAX_VARS) {
				_vm->setVar((int16)varNr, (byte)value);
				result = Inspector::DebugValue::fromInt(_vm->getVar((int16)varNr));
				return true;
			}
		}
		return false;
	}

	// Comparison "vN == K" (breakpoint conditions).
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
		result = Inspector::DebugValue::fromBool(lhsVal.intVal == atoi(rhs.c_str()));
		return true;
	}

	if (expr.size() >= 2 && expr[0] == 'v') {
		int varNr = atoi(expr.c_str() + 1);
		if (varNr >= 0 && varNr < MAX_VARS) {
			result = Inspector::DebugValue::fromInt(_vm->getVar((int16)varNr));
			return true;
		}
	}
	if (expr.size() >= 2 && expr[0] == 'f') {
		int flagNr = atoi(expr.c_str() + 1);
		if (flagNr >= 0 && flagNr < MAX_FLAGS * 8) {
			result = Inspector::DebugValue::fromBool(_vm->getFlag((int16)flagNr));
			return true;
		}
	}
	if (expr.size() >= 2 && expr[0] == 's') {
		int strNr = atoi(expr.c_str() + 1);
		if (strNr >= 0 && strNr <= MAX_STRINGS) {
			result = Inspector::DebugValue::fromString(_vm->_game.strings[strNr]);
			return true;
		}
	}
	return false;
}

} // End of namespace Agi
