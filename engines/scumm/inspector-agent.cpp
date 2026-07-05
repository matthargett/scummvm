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

#include "scumm/inspector-agent.h"

#include "common/config-manager.h"
#include "common/inspector/inspector.h"

#include "scumm/object.h"
#include "scumm/scumm.h"

namespace Scumm {

/**
 * Byte-listing cap. Byte-granularity listings cost one ListingLine per
 * resource byte, so huge v7/v8/HE room resources are truncated here;
 * instructions beyond the cap map onto the trailing (non-statement)
 * marker line, which degrades stepping display for them but never
 * misplaces a breakpoint. 64 KiB covers the complete resources of the
 * classic (v0-v6) games, whose scripts use 16-bit-relative jumps anyway.
 */
enum {
	kMaxListingBytes = 0x10000
};

ScummInspectorAgent::ScummInspectorAgent(ScummEngine *vm) : _vm(vm), _session(nullptr) {
	for (int i = 0; i < NUM_SCRIPT_SLOT; i++)
		_slotHandles[i] = -1;
}

void ScummInspectorAgent::init() {
	_session = Inspector::initSession(this);
}

void ScummInspectorAgent::shutdown() {
	if (_session) {
		Inspector::shutdownSession();
		_session = nullptr;
	}
}

Common::String ScummInspectorAgent::targetTitle() const {
	return ConfMan.getActiveDomainName() + " (SCUMM)";
}

Common::String ScummInspectorAgent::describeThread(uint32 threadId) const {
	if (threadId < NUM_SCRIPT_SLOT && _vm->vm.slot[threadId].status != ssDead)
		return Common::String::format("SCUMM slot %u (script %d)", threadId,
		                              _vm->vm.slot[threadId].number);
	return Common::String::format("SCUMM slot %u", threadId);
}

// Registration key: the identity of the code the PC offsets are relative
// to. Global scripts, inventory objects and flobjects are self-contained
// resources keyed by their number; local/room scripts execute out of the
// room resource and their numbers repeat across rooms, so the room
// resource number is part of the key (and URL).
uint32 ScummInspectorAgent::scriptKeyFor(int slotNr) const {
	const ScriptSlot &s = _vm->vm.slot[slotNr];
	uint32 room = 0;
	if (s.where == WIO_ROOM || s.where == WIO_LOCAL)
		room = (uint32)_vm->_roomResource & 0xFFF;
	return ((uint32)s.where << 28) | (room << 16) | s.number;
}

// Mirror of ScummEngine::getScriptBaseAddress(): find the resource that
// _scriptOrgPointer points into, i.e. the one PC offsets index.
bool ScummInspectorAgent::resolveScriptResource(int slotNr, int &resType, int &resIdx,
                                                Common::String &url) const {
	const ScriptSlot &s = _vm->vm.slot[slotNr];

	switch (s.where) {
	case WIO_INVENTORY: {
		int idx;
		for (idx = 0; idx < _vm->_numInventory; idx++)
			if (_vm->_inventory[idx] == s.number)
				break;
		if (idx >= _vm->_numInventory)
			return false;
		resType = rtInventory;
		resIdx = idx;
		url = Common::String::format("scummvm-dbg://scumm/inventory-%d", s.number);
		return true;
	}

	case WIO_LOCAL:
	case WIO_ROOM:
		resType = (_vm->_game.version == 8) ? rtRoomScripts : rtRoom;
		resIdx = _vm->_roomResource;
		url = Common::String::format("scummvm-dbg://scumm/room%d/%s-%d", resIdx,
		                             (s.where == WIO_LOCAL) ? "local" : "room", s.number);
		return true;

	case WIO_GLOBAL:
		resType = rtScript;
		resIdx = s.number;
		url = Common::String::format("scummvm-dbg://scumm/global-%d", s.number);
		return true;

	case WIO_FLOBJECT: {
		int objIdx = _vm->getObjectIndex(s.number);
		if (objIdx == -1)
			return false;
		resType = rtFlObject;
		resIdx = _vm->_objs[objIdx].fl_object_index;
		url = Common::String::format("scummvm-dbg://scumm/flobject-%d", s.number);
		return true;
	}

	default:
		return false;
	}
}

// Byte-granularity fallback listing (no static SCUMM decoder exists in
// the tree): one statement line per byte of the containing resource, so
// every offset the interpreter's PC can hold matches a listing line
// exactly. The opcode-name annotation comes from the live per-version
// opcode table and is only correct when that byte IS an opcode (true at
// instruction starts, where all hook events happen); elsewhere it is a
// heuristic aid, hence the trailing '?'.
bool ScummInspectorAgent::buildListing(int slotNr, Inspector::ScriptListing &listing) const {
	int resType, resIdx;
	if (!resolveScriptResource(slotNr, resType, resIdx, listing.url))
		return false;

	const byte *data = _vm->getResourceAddress((ResType)resType, resIdx);
	if (!data)
		return false;
	const uint32 size = (uint32)_vm->getResourceSize((ResType)resType, resIdx);
	const uint32 listBytes = MIN<uint32>(size, kMaxListingBytes);

	listing.lines.reserve(listBytes + 1);
	for (uint32 pos = 0; pos < listBytes; pos++) {
		const byte b = data[pos];
		Common::String text = Common::String::format("[%04x] db %02x", pos, b);
		const char *desc = _vm->getOpcodeDesc(b);
		if (desc && *desc)
			text += Common::String::format(" ; %s?", desc);
		listing.lines.push_back(Inspector::ListingLine(pos, text, true));
	}
	if (size > listBytes)
		listing.lines.push_back(Inspector::ListingLine(listBytes,
			Common::String::format("[%04x] ... truncated (%u bytes total)", listBytes, size),
			false));
	return true;
}

int ScummInspectorAgent::ensureScriptRegistered(int slotNr) {
	if (!_session)
		return -1;

	const uint32 key = scriptKeyFor(slotNr);
	Common::HashMap<uint32, int>::const_iterator it = _scriptHandles.find(key);
	if (it != _scriptHandles.end())
		return it->_value;

	Inspector::ScriptListing listing;
	if (!buildListing(slotNr, listing))
		return -1; // resource unavailable; retried on the next activation

	const int handle = _session->registerScript(listing);
	_scriptHandles[key] = handle;
	return handle;
}

void ScummInspectorAgent::onScriptActivated() {
	if (!_session)
		return;
	const byte slotNr = _vm->_currentScript;
	if (slotNr >= NUM_SCRIPT_SLOT) // 0xFF = no script (v0-v2 dead-room case)
		return;
	_slotHandles[slotNr] = ensureScriptRegistered(slotNr);
}

void ScummInspectorAgent::onInstruction() {
	if (!_session)
		return;
	const byte slotNr = _vm->_currentScript;
	if (slotNr >= NUM_SCRIPT_SLOT)
		return;
	const int handle = _slotHandles[slotNr];
	if (handle < 0)
		return;
	// _scriptPointer still points at the instruction start (pre-fetch).
	_session->instructionHook(slotNr, handle,
	                          (uint32)(_vm->_scriptPointer - _vm->_scriptOrgPointer),
	                          (uint32)_vm->vm.numNestedScripts + 1);
}

void ScummInspectorAgent::onVariableWrite(int var, int value) {
	if (!_session || !_session->watchArmed())
		return;
	// Writes outside script execution are engine-internal bookkeeping.
	if (_vm->_currentScript >= NUM_SCRIPT_SLOT)
		return;
	_session->variableAccessHook(_vm->_currentScript,
	                             Common::String::format("g%d", var), true,
	                             Inspector::DebugValue::fromInt(value));
}

void ScummInspectorAgent::transportTick() {
	if (_session)
		Inspector::transportTick();
}

// The canonical frame list for a pause: innermost = the running slot at
// the live PC, then the runScriptNested() chain (vm.nest[]) outward.
// Most SCUMM script starts allocate an independent slot executed later
// by runAllScripts() and are NOT nested calls; only chained/recursive
// starts appear here. Used identically by buildCallFrames(),
// buildScopeObject() and evaluate() so frame indices always agree.
void ScummInspectorAgent::collectFrames(uint32 threadId, Common::Array<FrameRef> &frames) const {
	const byte cur = _vm->_currentScript;

	if (cur >= NUM_SCRIPT_SLOT || threadId != cur) {
		// Pauses always originate from the running slot; a query about
		// another (suspended) slot gets its stored position only.
		if (threadId < NUM_SCRIPT_SLOT && _vm->vm.slot[threadId].status != ssDead) {
			FrameRef f;
			f.slotNr = (int)threadId;
			f.offset = _vm->vm.slot[threadId].offs;
			f.handle = _slotHandles[threadId];
			frames.push_back(f);
		}
		return;
	}

	FrameRef top;
	top.slotNr = cur;
	top.offset = (uint32)(_vm->_scriptPointer - _vm->_scriptOrgPointer);
	top.handle = _slotHandles[cur];
	frames.push_back(top);

	for (int i = (int)_vm->vm.numNestedScripts - 1; i >= 0; i--) {
		const NestedScript &n = _vm->vm.nest[i];
		if (n.number == 0 || n.where == 0xFF)
			continue; // chain level started outside any script
		const ScriptSlot &s = _vm->vm.slot[n.slot];
		// Same liveness test runScriptNested() applies before resuming.
		if (s.number != n.number || s.where != n.where || s.status == ssDead)
			continue;
		FrameRef f;
		f.slotNr = n.slot;
		f.offset = s.offs;
		f.handle = _slotHandles[n.slot];
		if (f.handle < 0)
			continue; // never registered (resource vanished); not presentable
		frames.push_back(f);
	}
}

int ScummInspectorAgent::frameSlot(uint32 threadId, int frameIndex) const {
	if (frameIndex < 0) {
		// Global context (Runtime.evaluate while running).
		return (_vm->_currentScript < NUM_SCRIPT_SLOT) ? _vm->_currentScript : -1;
	}
	Common::Array<FrameRef> frames;
	collectFrames(threadId, frames);
	if ((uint32)frameIndex >= frames.size())
		return -1;
	return frames[frameIndex].slotNr;
}

bool ScummInspectorAgent::hasBitVarScope() const {
	return _vm->_bitVars != nullptr && _vm->_numBitVariables > 0;
}

bool ScummInspectorAgent::hasRoomVarScope() const {
	return _vm->_roomVars != nullptr && _vm->_numRoomVariables > 0;
}

void ScummInspectorAgent::buildCallFrames(uint32 threadId,
                                          Common::Array<Inspector::CallFrameInfo> &frames) {
	Common::Array<FrameRef> refs;
	collectFrames(threadId, refs);

	for (uint32 i = 0; i < refs.size(); i++) {
		const ScriptSlot &s = _vm->vm.slot[refs[i].slotNr];
		Inspector::CallFrameInfo frame;
		frame.functionName = Common::String::format("script-%d (slot %d)", s.number, refs[i].slotNr);
		frame.scriptHandle = refs[i].handle;
		frame.offset = refs[i].offset;

		// Scope layout mirrored by buildScopeObject(): 0 = Locals,
		// 1 = Globals, then bit / room variable banks when present.
		Inspector::ScopeInfo locals;
		locals.type = "local";
		locals.name = Common::String::format("Locals (slot %d)", refs[i].slotNr);
		frame.scopes.push_back(locals);

		Inspector::ScopeInfo globals;
		globals.type = "global";
		globals.name = "Globals";
		frame.scopes.push_back(globals);

		if (hasBitVarScope()) {
			Inspector::ScopeInfo bits;
			bits.type = "global";
			bits.name = "Bit variables (set)";
			frame.scopes.push_back(bits);
		}
		if (hasRoomVarScope()) {
			Inspector::ScopeInfo rooms;
			rooms.type = "global";
			rooms.name = "Room variables";
			frame.scopes.push_back(rooms);
		}

		frames.push_back(frame);
	}
}

void ScummInspectorAgent::buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
                                           Inspector::RemoteObjectTable &table, int objRef) {
	// Invert the scope layout of buildCallFrames().
	int kind = scopeIndex;
	if (kind >= 2 && !hasBitVarScope())
		kind++; // no bit scope registered: index 2 is the room bank

	switch (kind) {
	case 0: { // Locals
		const int slotNr = frameSlot(threadId, frameIndex);
		if (slotNr < 0)
			return;
		for (int i = 0; i <= NUM_SCRIPT_LOCAL; i++)
			table.addProperty(objRef, Common::String::format("local%d", i),
			                  Inspector::DebugValue::fromInt(_vm->vm.localvar[slotNr][i]));
		break;
	}
	case 1: // Globals
		for (int i = 0; i < _vm->_numVariables; i++)
			table.addProperty(objRef, Common::String::format("g%d", i),
			                  Inspector::DebugValue::fromInt(_vm->_scummVars[i]));
		break;
	case 2: // Bit variables: showing 4096+ zero bits is noise, list set ones
		for (int i = 0; i < _vm->_numBitVariables; i++)
			if (_vm->_bitVars[i >> 3] & (1 << (i & 7)))
				table.addProperty(objRef, Common::String::format("bit%d", i),
				                  Inspector::DebugValue::fromBool(true));
		break;
	case 3: // Room variables (HE80+)
		for (int i = 0; i < _vm->_numRoomVariables; i++)
			table.addProperty(objRef, Common::String::format("room%d", i),
			                  Inspector::DebugValue::fromInt(_vm->_roomVars[i]));
		break;
	default:
		break;
	}
}

// Parse "<prefix><decimal>", e.g. parseIndexed("g13", "g") == 13.
// Returns -1 when the expression is not of that shape.
static int parseIndexed(const Common::String &expr, const char *prefix) {
	const uint32 plen = (uint32)strlen(prefix);
	if (expr.size() <= plen || strncmp(expr.c_str(), prefix, plen) != 0)
		return -1;
	for (uint32 i = plen; i < expr.size(); i++)
		if (expr[i] < '0' || expr[i] > '9')
			return -1;
	return atoi(expr.c_str() + plen);
}

bool ScummInspectorAgent::evaluate(uint32 threadId, int frameIndex,
                                   const Common::String &expression,
                                   Inspector::RemoteObjectTable &table,
                                   Inspector::DebugValue &result) {
	// Supported forms: gN / localN / bitN / roomN reads, "gN = K" and
	// "localN = K" writes, and "<lhs> == K" comparisons — enough for
	// watch panes, breakpoint conditions and console pokes.
	Common::String expr = expression;
	expr.trim();

	// Assignment? (a single '=' that is not part of "==")
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
		const int value = atoi(rhs.c_str());

		int idx = parseIndexed(lhs, "g");
		if (idx >= 0 && idx < _vm->_numVariables && idx < 0x1000) {
			// writeVar so engine-side mirrors (talkspeed, subtitles, the
			// watchpoint hook) behave exactly as for a script write.
			_vm->writeVar((uint)idx, value);
			result = Inspector::DebugValue::fromInt(_vm->readVar((uint)idx));
			return true;
		}
		idx = parseIndexed(lhs, "local");
		const int slotNr = frameSlot(threadId, frameIndex);
		if (idx >= 0 && idx <= NUM_SCRIPT_LOCAL && slotNr >= 0) {
			_vm->vm.localvar[slotNr][idx] = value;
			result = Inspector::DebugValue::fromInt(_vm->vm.localvar[slotNr][idx]);
			return true;
		}
		return false;
	}

	// Comparison "<lhs> == K" (breakpoint conditions).
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
		const int64 rhsVal = atoi(rhs.c_str());
		if (lhsVal.type == Inspector::DebugValue::kBool)
			result = Inspector::DebugValue::fromBool((lhsVal.boolVal ? 1 : 0) == rhsVal);
		else
			result = Inspector::DebugValue::fromBool(lhsVal.intVal == rhsVal);
		return true;
	}

	// Bare reads.
	int idx = parseIndexed(expr, "g");
	if (idx >= 0 && idx < _vm->_numVariables && idx < 0x1000) {
		// readVar gives the engine's view (subtitle/talkspeed mirrors).
		result = Inspector::DebugValue::fromInt(_vm->readVar((uint)idx));
		return true;
	}
	idx = parseIndexed(expr, "local");
	if (idx >= 0 && idx <= NUM_SCRIPT_LOCAL) {
		const int slotNr = frameSlot(threadId, frameIndex);
		if (slotNr < 0)
			return false;
		result = Inspector::DebugValue::fromInt(_vm->vm.localvar[slotNr][idx]);
		return true;
	}
	idx = parseIndexed(expr, "bit");
	if (idx >= 0 && idx < _vm->_numBitVariables && _vm->_bitVars) {
		result = Inspector::DebugValue::fromBool((_vm->_bitVars[idx >> 3] & (1 << (idx & 7))) != 0);
		return true;
	}
	idx = parseIndexed(expr, "room");
	if (idx >= 0 && idx < _vm->_numRoomVariables && _vm->_roomVars) {
		result = Inspector::DebugValue::fromInt(_vm->_roomVars[idx]);
		return true;
	}
	return false;
}

} // End of namespace Scumm
