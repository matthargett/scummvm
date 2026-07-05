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

#include "engines/wintermute/inspector-agent.h"

#include "engines/wintermute/base/base_engine.h"
#include "engines/wintermute/base/base_game.h"
#include "engines/wintermute/base/base_scriptable.h"
#include "engines/wintermute/base/scriptables/script.h"
#include "engines/wintermute/base/scriptables/script_engine.h"
#include "engines/wintermute/base/scriptables/script_stack.h"
#include "engines/wintermute/base/scriptables/script_value.h"

#ifdef ENABLE_FOXTAIL
#include "engines/wintermute/base/scriptables/script_opcodes.h"
#endif

#include "common/config-manager.h"
#include "common/endian.h"
#include "common/inspector/inspector.h"
#include "common/util.h"

namespace Wintermute {

WintermuteInspectorAgent *WintermuteInspectorAgent::g_agent = nullptr;

// What one instruction reads after its opcode dword; must mirror exactly
// what ScScript::executeInstruction() consumes per opcode via
// getDWORD()/getFloat()/getString(), or listing offsets drift off the
// interpreter's real _iP values.
enum OperandKind {
	kOpNone,   ///< no operand
	kOpInt,    ///< one int32 immediate
	kOpSymbol, ///< one uint32 symbol-table index
	kOpTarget, ///< one uint32 bytecode offset (call/jump target)
	kOpFloat,  ///< one 8-byte little-endian double
	kOpString, ///< inline NUL-terminated string
	kOpLine    ///< II_DBG_LINE: one uint32 1-based source line
};

struct OpSpec {
	const char *name;
	OperandKind operand;
};

static const OpSpec kOpSpecs[] = {
	{ "var",                  kOpSymbol }, // II_DEF_VAR
	{ "global",               kOpSymbol }, // II_DEF_GLOB_VAR
	{ "ret",                  kOpNone   }, // II_RET
	{ "ret_event",            kOpNone   }, // II_RET_EVENT
	{ "call",                 kOpTarget }, // II_CALL
	{ "call_by_exp",          kOpNone   }, // II_CALL_BY_EXP
	{ "external_call",        kOpSymbol }, // II_EXTERNAL_CALL
	{ "scope",                kOpNone   }, // II_SCOPE
	{ "correct_stack",        kOpInt    }, // II_CORRECT_STACK
	{ "create_object",        kOpNone   }, // II_CREATE_OBJECT
	{ "pop_empty",            kOpNone   }, // II_POP_EMPTY
	{ "push_var",             kOpSymbol }, // II_PUSH_VAR
	{ "push_var_ref",         kOpSymbol }, // II_PUSH_VAR_REF
	{ "pop_var",              kOpSymbol }, // II_POP_VAR
	{ "push_var_this",        kOpNone   }, // II_PUSH_VAR_THIS
	{ "push_int",             kOpInt    }, // II_PUSH_INT
	{ "push_bool",            kOpInt    }, // II_PUSH_BOOL
	{ "push_float",           kOpFloat  }, // II_PUSH_FLOAT
	{ "push_string",          kOpString }, // II_PUSH_STRING
	{ "push_null",            kOpNone   }, // II_PUSH_NULL
	{ "push_this_from_stack", kOpNone   }, // II_PUSH_THIS_FROM_STACK
	{ "push_this",            kOpSymbol }, // II_PUSH_THIS
	{ "pop_this",             kOpNone   }, // II_POP_THIS
	{ "push_by_exp",          kOpNone   }, // II_PUSH_BY_EXP
	{ "pop_by_exp",           kOpNone   }, // II_POP_BY_EXP
	{ "jmp",                  kOpTarget }, // II_JMP
	{ "jmp_false",            kOpTarget }, // II_JMP_FALSE
	{ "add",                  kOpNone   }, // II_ADD
	{ "sub",                  kOpNone   }, // II_SUB
	{ "mul",                  kOpNone   }, // II_MUL
	{ "div",                  kOpNone   }, // II_DIV
	{ "mod",                  kOpNone   }, // II_MODULO
	{ "not",                  kOpNone   }, // II_NOT
	{ "and",                  kOpNone   }, // II_AND
	{ "or",                   kOpNone   }, // II_OR
	{ "cmp_eq",               kOpNone   }, // II_CMP_EQ
	{ "cmp_ne",               kOpNone   }, // II_CMP_NE
	{ "cmp_lt",               kOpNone   }, // II_CMP_L
	{ "cmp_gt",               kOpNone   }, // II_CMP_G
	{ "cmp_le",               kOpNone   }, // II_CMP_LE
	{ "cmp_ge",               kOpNone   }, // II_CMP_GE
	{ "cmp_strict_eq",        kOpNone   }, // II_CMP_STRICT_EQ
	{ "cmp_strict_ne",        kOpNone   }, // II_CMP_STRICT_NE
	{ "dbg_line",             kOpLine   }, // II_DBG_LINE
	{ "pop_reg1",             kOpNone   }, // II_POP_REG1
	{ "push_reg1",            kOpNone   }, // II_PUSH_REG1
	{ "const",                kOpSymbol }  // II_DEF_CONST_VAR
};

typedef Common::HashMap<uint32, Common::String> CodeLabelMap;

static void addCodeLabel(CodeLabelMap &labels, uint32 pos, const char *kind, const char *name) {
	if (pos == 0 || !name) {
		return; // 0 is the tables' "not found" sentinel, never real code
	}
	Common::String entry = Common::String::format("%s %s", kind, name);
	CodeLabelMap::iterator it = labels.find(pos);
	if (it != labels.end()) {
		it->_value += Common::String::format(" & %s", entry.c_str());
	} else {
		labels[pos] = entry;
	}
}

static bool debugValuesEqual(const Inspector::DebugValue &a, const Inspector::DebugValue &b) {
	const bool aNum = (a.type == Inspector::DebugValue::kInt || a.type == Inspector::DebugValue::kDouble);
	const bool bNum = (b.type == Inspector::DebugValue::kInt || b.type == Inspector::DebugValue::kDouble);
	if (aNum && bNum) {
		const double av = (a.type == Inspector::DebugValue::kInt) ? (double)a.intVal : a.doubleVal;
		const double bv = (b.type == Inspector::DebugValue::kInt) ? (double)b.intVal : b.doubleVal;
		return av == bv;
	}
	if (a.type != b.type) {
		return false;
	}
	switch (a.type) {
	case Inspector::DebugValue::kBool:
		return a.boolVal == b.boolVal;
	case Inspector::DebugValue::kString:
		return a.stringVal == b.stringVal;
	case Inspector::DebugValue::kNull:
	case Inspector::DebugValue::kUndefined:
		return true;
	default:
		return false;
	}
}

WintermuteInspectorAgent::WintermuteInspectorAgent(BaseGame *game) :
		_gameRef(game), _session(nullptr), _nextThreadId(1),
		_lastScript(nullptr), _lastThreadId(0), _lastHandle(-1),
		_opcodesType(OPCODES_UNCHANGED) {
#ifdef ENABLE_FOXTAIL
	// Same decision ScScript::initOpcodesType() makes; the remap is
	// game-global, so computing it once here is enough.
	_opcodesType = BaseEngine::instance().isFoxTail(FOXTAIL_1_2_896, FOXTAIL_1_2_896) ? OPCODES_FOXTAIL_1_2_896 :
	               BaseEngine::instance().isFoxTail(FOXTAIL_1_2_902, FOXTAIL_LATEST_VERSION) ? OPCODES_FOXTAIL_1_2_902 :
	               OPCODES_UNCHANGED;
#endif
}

WintermuteInspectorAgent::~WintermuteInspectorAgent() {
	if (g_agent == this) {
		g_agent = nullptr;
	}
}

void WintermuteInspectorAgent::init() {
	_session = Inspector::initSession(this);
	if (_session) {
		g_agent = this;
	}
}

void WintermuteInspectorAgent::shutdown() {
	if (g_agent == this) {
		g_agent = nullptr;
	}
	if (_session) {
		Inspector::shutdownSession();
		_session = nullptr;
	}
}

Common::String WintermuteInspectorAgent::targetTitle() const {
	return ConfMan.getActiveDomainName() + " (Wintermute)";
}

uint32 WintermuteInspectorAgent::decodeOpcode(uint32 inst) const {
#ifdef ENABLE_FOXTAIL
	if (_opcodesType != OPCODES_UNCHANGED) {
		if (inst > 46) {
			return (uint32)-1;
		}
		switch (_opcodesType) {
		case OPCODES_FOXTAIL_1_2_896:
			return foxtail_1_2_896_mapping[inst];
		case OPCODES_FOXTAIL_1_2_902:
			return foxtail_1_2_902_mapping[inst];
		default:
			break;
		}
	}
#endif
	return inst;
}

Common::String WintermuteInspectorAgent::urlForFilename(const char *filename) const {
	Common::String path;
	const char *src = (filename && filename[0]) ? filename : "unnamed.script";
	for (const char *p = src; *p; p++) {
		char c = (*p == '\\') ? '/' : *p;
		if (c == '/' && (path.empty() || path.lastChar() == '/')) {
			continue; // canonical: no leading or doubled slashes
		}
		path += c;
	}
	return Common::String::format("scummvm-dbg://wintermute/%s", path.c_str());
}

// Build a disassembly listing of the compiled buffer. The code block
// runs from header.codeStart to the first table behind it (the WME
// compiler appends the string/function/... tables after the code).
// II_DBG_LINE markers become the statement lines; a script compiled
// without debug info (no markers at all) degrades to instruction-
// granularity statements so breakpoints/stepping still work.
void WintermuteInspectorAgent::buildListing(ScScript *script, Inspector::ScriptListing &listing) const {
	const byte *data = script->_buffer;
	const uint32 size = script->_bufferSize;
	const ScScript::TScriptHeader &header = script->_header;

	listing.url = urlForFilename(script->_filename);

	if (!data || header.codeStart >= size) {
		return; // degenerate: empty listing (still registrable)
	}

	uint32 codeEnd = size;
	uint32 tableOfs[5];
	uint32 numTables = 0;
	tableOfs[numTables++] = header.funcTable;
	tableOfs[numTables++] = header.symbolTable;
	tableOfs[numTables++] = header.eventTable;
	tableOfs[numTables++] = header.methodTable;
	if (header.version >= 0x0101) {
		tableOfs[numTables++] = header.externalsTable;
	}
	for (uint32 i = 0; i < numTables; i++) {
		if (tableOfs[i] > header.codeStart && tableOfs[i] < codeEnd) {
			codeEnd = tableOfs[i];
		}
	}

	// Function/event/method entry points become label lines (and decorate
	// call targets). Extra lines sharing an instruction's offset are fine:
	// the registry tolerates equal offsets and maps an offset to the last
	// line holding it, i.e. the instruction itself.
	CodeLabelMap labels;
	for (uint32 i = 0; i < script->_numFunctions; i++) {
		addCodeLabel(labels, script->_functions[i].pos, "function", script->_functions[i].name);
	}
	for (uint32 i = 0; i < script->_numEvents; i++) {
		addCodeLabel(labels, script->_events[i].pos, "event", script->_events[i].name);
	}
	for (uint32 i = 0; i < script->_numMethods; i++) {
		addCodeLabel(labels, script->_methods[i].pos, "method", script->_methods[i].name);
	}

	listing.lines.push_back(Inspector::ListingLine(header.codeStart,
		Common::String::format("; %s (compiled WME script, code 0x%04x..0x%04x)",
		                       script->_filename ? script->_filename : "unnamed",
		                       header.codeStart, codeEnd),
		false));

	Common::Array<uint32> instrLines; // listing indices of real instructions
	bool sawDbgLine = false;
	uint32 pos = header.codeStart;

	while (pos + sizeof(uint32) <= codeEnd) {
		const uint32 start = pos;
		uint32 inst = READ_LE_UINT32(data + pos);
		pos += sizeof(uint32);
		inst = decodeOpcode(inst);

		CodeLabelMap::const_iterator label = labels.find(start);
		if (label != labels.end()) {
			listing.lines.push_back(Inspector::ListingLine(start,
				Common::String::format("%s:", label->_value.c_str()), false));
		}

		if (inst >= ARRAYSIZE(kOpSpecs)) {
			// Not a known opcode: either the code block ended early or the
			// decode desynced. Stop rather than emit offsets _iP never holds.
			listing.lines.push_back(Inspector::ListingLine(start,
				Common::String::format("[%04x] .dword 0x%08x ; unknown opcode, decode stopped", start, inst),
				false));
			break;
		}

		const OpSpec &spec = kOpSpecs[inst];
		Common::String text = Common::String::format("[%04x] %s", start, spec.name);
		bool isStatement = false;
		int originalLine = -1;
		bool truncated = false;

		switch (spec.operand) {
		case kOpNone:
			break;

		case kOpInt:
		case kOpSymbol:
		case kOpTarget:
		case kOpLine: {
			if (pos + sizeof(uint32) > codeEnd) {
				truncated = true;
				break;
			}
			const uint32 dw = READ_LE_UINT32(data + pos);
			pos += sizeof(uint32);
			if (spec.operand == kOpSymbol) {
				if (script->_symbols && dw < script->_numSymbols && script->_symbols[dw]) {
					text += Common::String::format(" %s", script->_symbols[dw]);
				} else {
					text += Common::String::format(" sym#%u", dw);
				}
			} else if (spec.operand == kOpTarget) {
				text += Common::String::format(" %04x", dw);
				CodeLabelMap::const_iterator target = labels.find(dw);
				if (target != labels.end()) {
					text += Common::String::format(" ; -> %s", target->_value.c_str());
				}
			} else if (spec.operand == kOpLine) {
				// Statement marker: the compiler emits one per source line.
				text = Common::String::format("[%04x] // line %u", start, dw);
				isStatement = true;
				originalLine = (dw > 0) ? (int)(dw - 1) : 0;
				sawDbgLine = true;
			} else {
				text += Common::String::format(" %d", (int32)dw);
			}
			break;
		}

		case kOpFloat: {
			if (pos + 8 > codeEnd) {
				truncated = true;
				break;
			}
			const uint64 bits = READ_LE_UINT64(data + pos);
			pos += 8;
			double value;
			memcpy(&value, &bits, sizeof(value));
			text += Common::String::format(" %g", value);
			break;
		}

		case kOpString: {
			uint32 end = pos;
			while (end < codeEnd && data[end] != 0) {
				end++;
			}
			if (end >= codeEnd) {
				truncated = true;
				break;
			}
			const uint32 kMaxShow = 60;
			text += " \"";
			for (uint32 i = pos; i < end && i - pos < kMaxShow; i++) {
				const byte c = data[i];
				text += (c < 32 || c >= 127) ? '?' : (char)c; // keep the listing ASCII-clean
			}
			if (end - pos > kMaxShow) {
				text += "...";
			}
			text += '"';
			pos = end + 1;
			break;
		}
		}

		if (truncated) {
			listing.lines.push_back(Inspector::ListingLine(start,
				Common::String::format("[%04x] %s <truncated>", start, spec.name), false));
			break;
		}

		instrLines.push_back(listing.lines.size());
		listing.lines.push_back(Inspector::ListingLine(start, text, isStatement, originalLine));
	}

	if (!sawDbgLine) {
		// Compiled without debug info: make every instruction a statement
		// so stepping and breakpoints keep working (at opcode granularity).
		for (uint32 i = 0; i < instrLines.size(); i++) {
			listing.lines[instrLines[i]].isStatement = true;
		}
	}
}

bool WintermuteInspectorAgent::ensureTracked(ScScript *script, ThreadInfo &info) {
	if (!script || !script->_buffer || !script->_filename) {
		return false;
	}
	LiveMap::const_iterator it = _liveScripts.find((uintptr)script);
	if (it != _liveScripts.end()) {
		info = it->_value;
		return true;
	}

	// Threads share their parent's compiled file, so the listing is
	// registered once per filename; each ScScript still gets its own
	// inspector thread id.
	Common::String filename(script->_filename);
	int handle = -1;
	HandleMap::const_iterator hit = _scriptHandles.find(filename);
	if (hit != _scriptHandles.end()) {
		handle = hit->_value;
	} else {
		Inspector::ScriptListing listing;
		buildListing(script, listing);
		handle = _session->registerScript(listing);
		_scriptHandles[filename] = handle; // cache failures too (no per-frame retry)
	}

	info.threadId = _nextThreadId++;
	info.handle = handle;
	_liveScripts[(uintptr)script] = info;
	_threads[info.threadId] = script;
	return true;
}

ScScript *WintermuteInspectorAgent::scriptForThread(uint32 threadId) const {
	ThreadMap::const_iterator it = _threads.find(threadId);
	return (it == _threads.end()) ? nullptr : it->_value;
}

void WintermuteInspectorAgent::transportTick() {
	if (!_session) {
		return;
	}
	Inspector::transportTick();

	// Lazy registration sweep: make every live script visible to a client
	// before it ever arms a breakpoint (Debugger.enable replays these).
	if (_gameRef && _gameRef->_scEngine) {
		ScEngine *engine = _gameRef->_scEngine;
		for (int32 i = 0; i < engine->_scripts.getSize(); i++) {
			ThreadInfo info;
			ensureTracked(engine->_scripts[i], info);
		}
	}
}

void WintermuteInspectorAgent::onInstruction(ScScript *script) {
	if (!_session || !script) {
		return;
	}
	if (script != _lastScript) {
		ThreadInfo info;
		if (!ensureTracked(script, info)) {
			return;
		}
		_lastScript = script;
		_lastThreadId = info.threadId;
		_lastHandle = info.handle;
	}
	if (_lastHandle < 0) {
		return;
	}
	// _callStack->_sP is -1 with no call outstanding, so depth >= 1.
	const uint32 depth = script->_callStack ? (uint32)(script->_callStack->_sP + 2) : 1;
	_session->instructionHook(_lastThreadId, _lastHandle, script->_iP, depth);

	if (_session->watchArmed()) {
		checkVariableAccess(script);
	}
}

// GameScript.setWatchpoint support: peek at the instruction about to
// execute; II_POP_VAR is the VM's only named-variable write and
// II_PUSH_VAR its only named read.
void WintermuteInspectorAgent::checkVariableAccess(ScScript *script) {
	const byte *data = script->_buffer;
	const uint32 ip = script->_iP;
	if (!data || ip + sizeof(uint32) > script->_bufferSize) {
		return;
	}
	const uint32 inst = decodeOpcode(READ_LE_UINT32(data + ip));
	if (inst != II_POP_VAR && inst != II_PUSH_VAR) {
		return;
	}
	if (ip + 2 * sizeof(uint32) > script->_bufferSize) {
		return;
	}
	const uint32 symbol = READ_LE_UINT32(data + ip + sizeof(uint32));
	if (!script->_symbols || symbol >= script->_numSymbols || !script->_symbols[symbol]) {
		return;
	}
	const Common::String name(script->_symbols[symbol]);
	if (inst == II_POP_VAR) {
		ScValue *newVal = script->_stack ? script->_stack->getTop() : nullptr;
		_session->variableAccessHook(_lastThreadId, name, true, debugValueOf(newVal));
	} else {
		ScValue *var = findVariable(script, 0, name.c_str());
		_session->variableAccessHook(_lastThreadId, name, false,
		                             var ? debugValueOf(var) : Inspector::DebugValue::undefined());
	}
}

void WintermuteInspectorAgent::onScriptCleanup(ScScript *script) {
	LiveMap::const_iterator it = _liveScripts.find((uintptr)script);
	if (it == _liveScripts.end()) {
		return;
	}
	_threads.erase(it->_value.threadId);
	_liveScripts.erase((uintptr)script);
	if (_lastScript == script) {
		_lastScript = nullptr;
		_lastThreadId = 0;
		_lastHandle = -1;
	}
}

void WintermuteInspectorAgent::onRuntimeError(ScScript *script, const Common::String &message) {
	if (!_session || !script) {
		return;
	}
	ThreadInfo info;
	if (!ensureTracked(script, info) || info.handle < 0) {
		return;
	}
	_session->reportException(info.threadId, info.handle, script->_iP, message);
}

Common::String WintermuteInspectorAgent::describeThread(uint32 threadId) const {
	ScScript *script = scriptForThread(threadId);
	if (!script) {
		return Common::String::format("thread-%u", threadId);
	}
	const char *filename = script->_filename ? script->_filename : "unnamed";
	if (script->_thread && script->_threadEvent) {
		return Common::String::format("%s:%s (%s thread)", filename, script->_threadEvent,
		                              script->_methodThread ? "method" : "event");
	}
	return Common::String(filename);
}

// The function/event/method whose entry point is the closest one at or
// before @p offset. The tables use pos 0 as "not found", and real code
// never starts at 0 (the 32-byte header comes first).
Common::String WintermuteInspectorAgent::segmentNameAt(ScScript *script, uint32 offset) const {
	const char *best = nullptr;
	const char *kind = "";
	uint32 bestPos = 0;
	for (uint32 i = 0; i < script->_numFunctions; i++) {
		const uint32 pos = script->_functions[i].pos;
		if (pos != 0 && pos <= offset && pos >= bestPos) {
			best = script->_functions[i].name;
			kind = "";
			bestPos = pos;
		}
	}
	for (uint32 i = 0; i < script->_numEvents; i++) {
		const uint32 pos = script->_events[i].pos;
		if (pos != 0 && pos <= offset && pos >= bestPos) {
			best = script->_events[i].name;
			kind = "on ";
			bestPos = pos;
		}
	}
	for (uint32 i = 0; i < script->_numMethods; i++) {
		const uint32 pos = script->_methods[i].pos;
		if (pos != 0 && pos <= offset && pos >= bestPos) {
			best = script->_methods[i].name;
			kind = "";
			bestPos = pos;
		}
	}
	if (best) {
		return Common::String::format("%s%s", kind, best);
	}
	if (script->_thread && script->_threadEvent) {
		return Common::String(script->_threadEvent);
	}
	return "(top-level)";
}

// The scope bag owned by CDP frame @p frameIndex (0 = innermost).
// _scopeStack parallels _callStack (II_CALL pushes a return address, the
// callee's II_SCOPE prologue pushes a scope, II_RET pops both), aligned
// from the bottom so a not-yet-pushed innermost scope (hook between the
// call and the prologue) skews nothing.
ScValue *WintermuteInspectorAgent::scopeBagFor(ScScript *script, int frameIndex) const {
	if (!script || !script->_scopeStack || !script->_callStack || frameIndex < 0) {
		return nullptr;
	}
	const int callDepth = script->_callStack->_sP + 1;   // outstanding calls
	const int scopeCount = script->_scopeStack->_sP + 1; // pushed scopes
	const int fromBottom = callDepth - frameIndex;
	if (fromBottom < 0 || fromBottom >= scopeCount) {
		return nullptr; // top-level code (or prologue window): no locals
	}
	return script->_scopeStack->getAt(scopeCount - 1 - fromBottom);
}

void WintermuteInspectorAgent::buildCallFrames(uint32 threadId, Common::Array<Inspector::CallFrameInfo> &frames) {
	ScScript *script = scriptForThread(threadId);
	if (!script) {
		return;
	}
	LiveMap::const_iterator it = _liveScripts.find((uintptr)script);
	const int handle = (it != _liveScripts.end()) ? it->_value.handle : -1;

	// One CDP frame per outstanding call, plus the innermost one. The
	// call stack stores plain-int return addresses (getAt(0) = newest).
	const int callDepth = script->_callStack ? (script->_callStack->_sP + 1) : 0;
	for (int f = 0; f <= callDepth; f++) {
		Inspector::CallFrameInfo frame;
		frame.scriptHandle = handle;
		if (f == 0) {
			frame.offset = script->_iP;
		} else {
			ScValue *ret = script->_callStack->getAt(f - 1);
			frame.offset = ret ? (uint32)ret->getInt() : 0;
		}
		frame.functionName = segmentNameAt(script, frame.offset);

		if (scopeBagFor(script, f)) {
			Inspector::ScopeInfo locals;
			locals.type = "local";
			locals.name = "Locals";
			frame.scopes.push_back(locals);
		}
		Inspector::ScopeInfo scriptGlobals;
		scriptGlobals.type = "closure";
		scriptGlobals.name = "Script globals";
		frame.scopes.push_back(scriptGlobals);
		Inspector::ScopeInfo engineGlobals;
		engineGlobals.type = "global";
		engineGlobals.name = "Engine globals";
		frame.scopes.push_back(engineGlobals);

		frames.push_back(frame);
	}
}

Inspector::DebugValue WintermuteInspectorAgent::debugValueOf(ScValue *val) const {
	if (!val) {
		return Inspector::DebugValue::undefined();
	}
	if (val->_type == VAL_VARIABLE_REF) {
		if (!val->_valRef) {
			return Inspector::DebugValue::null();
		}
		val = val->_valRef;
	}
	switch (val->_type) {
	case VAL_NULL:
		return Inspector::DebugValue::null();
	case VAL_STRING:
		return Inspector::DebugValue::fromString(val->_valString ? val->_valString : "");
	case VAL_INT:
		return Inspector::DebugValue::fromInt(val->_valInt);
	case VAL_BOOL:
		return Inspector::DebugValue::fromBool(val->_valBool);
	case VAL_FLOAT:
		return Inspector::DebugValue::fromDouble(val->_valFloat);
	case VAL_OBJECT:
		// Shallow description only: script objects can be cyclic.
		return Inspector::DebugValue::fromString(
			Common::String::format("[object: %u properties]", val->_valObject.size()));
	case VAL_NATIVE: {
		const char *desc = val->_valNative ? val->_valNative->scToString() : nullptr;
		return Inspector::DebugValue::fromString(desc ? desc : "[native]");
	}
	default:
		return Inspector::DebugValue::undefined();
	}
}

void WintermuteInspectorAgent::fillScopeFromBag(ScValue *bag, Inspector::RemoteObjectTable &table, int objRef) const {
	if (!bag) {
		return;
	}
	for (Common::HashMap<Common::String, ScValue *>::const_iterator it = bag->_valObject.begin();
	     it != bag->_valObject.end(); ++it) {
		table.addProperty(objRef, it->_key, debugValueOf(it->_value));
	}
}

void WintermuteInspectorAgent::buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
                                                Inspector::RemoteObjectTable &table, int objRef) {
	ScScript *script = scriptForThread(threadId);
	if (!script) {
		return;
	}
	// Mirror buildCallFrames(): [Locals,] Script globals, Engine globals.
	int idx = scopeIndex;
	ScValue *bag = scopeBagFor(script, frameIndex);
	if (bag) {
		if (idx == 0) {
			fillScopeFromBag(bag, table, objRef);
			return;
		}
		idx--;
	}
	if (idx == 0) {
		fillScopeFromBag(script->_globals, table, objRef);
	} else if (idx == 1 && script->_engine) {
		fillScopeFromBag(script->_engine->_globals, table, objRef);
	}
}

// Read-only variant of ScScript::getVar(): same resolution order (frame
// scope -> script globals -> engine globals) but never creates missing
// variables — evaluating a typo must not plant vars into the game.
// Works without a script context too (Runtime.evaluate while running):
// engine globals are still reachable then.
ScValue *WintermuteInspectorAgent::findVariable(ScScript *script, int frameIndex, const char *name) const {
	ScValue *scope = script ? scopeBagFor(script, frameIndex < 0 ? 0 : frameIndex) : nullptr;
	if (scope && scope->propExists(name)) {
		return scope->getProp(name);
	}
	if (script && script->_globals && script->_globals->propExists(name)) {
		return script->_globals->getProp(name);
	}
	ScEngine *engine = script ? script->_engine : (_gameRef ? _gameRef->_scEngine : nullptr);
	if (engine && engine->_globals && engine->_globals->propExists(name)) {
		return engine->_globals->getProp(name);
	}
	return nullptr;
}

bool WintermuteInspectorAgent::parseLiteral(const Common::String &text, Inspector::DebugValue &out) const {
	Common::String t = text;
	t.trim();
	if (t.empty()) {
		return false;
	}
	if (t == "true") {
		out = Inspector::DebugValue::fromBool(true);
		return true;
	}
	if (t == "false") {
		out = Inspector::DebugValue::fromBool(false);
		return true;
	}
	if (t == "null") {
		out = Inspector::DebugValue::null();
		return true;
	}
	if (t.size() >= 2 && t[0] == '"' && t.lastChar() == '"') {
		out = Inspector::DebugValue::fromString(Common::String(t.c_str() + 1, t.size() - 2));
		return true;
	}
	const char *s = t.c_str();
	char *end = nullptr;
	const long asInt = strtol(s, &end, 0);
	if (end != s && end && *end == '\0') {
		out = Inspector::DebugValue::fromInt(asInt);
		return true;
	}
	const double asDouble = strtod(s, &end);
	if (end != s && end && *end == '\0') {
		out = Inspector::DebugValue::fromDouble(asDouble);
		return true;
	}
	return false;
}

// Debug-console / watch / breakpoint-condition evaluation. Supported:
// bare names and dotted paths ("item", "Game.Name"), literals,
// "name == literal" / "name != literal" comparisons, and "name = literal"
// pokes into existing variables.
bool WintermuteInspectorAgent::evaluate(uint32 threadId, int frameIndex, const Common::String &expression,
                                        Inspector::RemoteObjectTable &table, Inspector::DebugValue &result) {
	ScScript *script = scriptForThread(threadId);
	if (!script && _gameRef && _gameRef->_scEngine) {
		script = _gameRef->_scEngine->_currentScript; // Runtime.evaluate while running
	}

	Common::String expr = expression;
	expr.trim();
	if (expr.empty()) {
		return false;
	}

	// Comparisons ("==" / "!=") — the form breakpoint conditions take.
	for (uint32 i = 0; i + 1 < expr.size(); i++) {
		const bool eq = (expr[i] == '=' && expr[i + 1] == '=');
		const bool ne = (expr[i] == '!' && expr[i + 1] == '=');
		if (!eq && !ne) {
			continue;
		}
		Common::String lhs(expr.c_str(), i);
		Common::String rhs(expr.c_str() + i + 2);
		lhs.trim();
		rhs.trim();
		Inspector::DebugValue lv, rv;
		if (!evaluate(threadId, frameIndex, lhs, table, lv)) {
			return false;
		}
		if (!parseLiteral(rhs, rv) && !evaluate(threadId, frameIndex, rhs, table, rv)) {
			return false;
		}
		result = Inspector::DebugValue::fromBool(debugValuesEqual(lv, rv) == eq);
		return true;
	}

	// Assignment ("name = literal"); "<="/">=" fall through unevaluated.
	for (uint32 i = 0; i < expr.size(); i++) {
		if (expr[i] != '=') {
			continue;
		}
		if (i == 0 || (i + 1 < expr.size() && expr[i + 1] == '=') ||
		    expr[i - 1] == '<' || expr[i - 1] == '>' || expr[i - 1] == '!') {
			break;
		}
		Common::String lhs(expr.c_str(), i);
		Common::String rhs(expr.c_str() + i + 1);
		lhs.trim();
		rhs.trim();
		Inspector::DebugValue rv;
		if (lhs.empty() || lhs.contains('.') || !parseLiteral(rhs, rv)) {
			return false;
		}
		ScValue *var = findVariable(script, frameIndex, lhs.c_str());
		if (!var) {
			return false; // only poke variables that already exist
		}
		switch (rv.type) {
		case Inspector::DebugValue::kInt:
			var->setInt((int)rv.intVal);
			break;
		case Inspector::DebugValue::kDouble:
			var->setFloat(rv.doubleVal);
			break;
		case Inspector::DebugValue::kBool:
			var->setBool(rv.boolVal);
			break;
		case Inspector::DebugValue::kString:
			var->setString(rv.stringVal.c_str());
			break;
		case Inspector::DebugValue::kNull:
			var->setNULL();
			break;
		default:
			return false;
		}
		result = debugValueOf(var);
		return true;
	}

	// Plain literal (useful inside conditions).
	if (parseLiteral(expr, result)) {
		return true;
	}

	// Bare variable name or dotted path.
	uint32 dot = expr.size();
	for (uint32 i = 0; i < expr.size(); i++) {
		if (expr[i] == '.') {
			dot = i;
			break;
		}
	}
	Common::String head(expr.c_str(), dot);
	head.trim();
	if (head.empty()) {
		return false;
	}
	ScValue *value = findVariable(script, frameIndex, head.c_str());
	if (!value) {
		return false;
	}
	while (dot < expr.size()) {
		const uint32 from = dot + 1;
		dot = expr.size();
		for (uint32 i = from; i < expr.size(); i++) {
			if (expr[i] == '.') {
				dot = i;
				break;
			}
		}
		Common::String part(expr.c_str() + from, dot - from);
		part.trim();
		if (part.empty()) {
			return false;
		}
		value = value->getProp(part.c_str()); // objects and natives alike
		if (!value) {
			return false;
		}
	}
	result = debugValueOf(value);
	return true;
}

} // End of namespace Wintermute
