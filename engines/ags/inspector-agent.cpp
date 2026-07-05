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

#include "ags/inspector-agent.h"

#include "common/config-manager.h"
#include "common/inspector/inspector.h"

#include "ags/engine/script/cc_instance.h"
#include "ags/engine/script/runtime_script_value.h"
#include "ags/engine/script/script_runtime.h"
#include "ags/engine/script/system_imports.h"
#include "ags/shared/script/cc_internal.h"
#include "ags/shared/script/cc_script.h"
#include "ags/globals.h"

namespace AGS3 {

// Register name table from cc_instance.cpp ({"null","sp","mar",...}).
extern const char *regnames[];

AGSInspectorAgent *g_inspectorAgent = nullptr;

// The function installed via ccSetDebugHook(); invoked by
// ccInstance::Run() on every SCMD_LINENUM opcode (with pc at that
// opcode) and with inst == nullptr when a script invocation returns.
static void inspectorNewLineHook(ccInstance *inst, int lineNumber) {
	AGSInspectorAgent *agent = g_inspectorAgent;
	if (!agent)
		return;
	agent->onScriptLine(inst, lineNumber);
	// Keep any previously installed debugger (plugin AGSE_SCRIPTDEBUG
	// hook / editor debugger) working.
	AGSInspectorAgent::NewLineHook chained = agent->chainedNewLineHook();
	if (chained)
		chained(inst, lineNumber);
}

// --- small local helpers -------------------------------------------------

// Exported function names are mangled "name$argcount"; data exports are
// plain, but strip defensively in both cases.
static Common::String stripExportName(const char *exportName) {
	if (!exportName)
		return Common::String();
	const char *dollar = strchr(exportName, '$');
	if (dollar)
		return Common::String(exportName, dollar);
	return Common::String(exportName);
}

// Exact-address lookup of an exported function (used to decorate call
// targets in listings).
static Common::String exportedFunctionAt(const ccScript *scr, int32 addr) {
	for (int k = 0; k < scr->numexports; ++k) {
		if (((scr->export_addr[k] >> 24L) & 0x000ff) != EXPORT_FUNCTION)
			continue;
		if ((scr->export_addr[k] & 0x00ffffff) != addr)
			continue;
		return stripExportName(scr->exports[k]);
	}
	return Common::String();
}

// Bounded, ASCII-safe rendering of a string literal operand.
static Common::String quoteStringLiteral(const char *str) {
	Common::String out("\"");
	uint i = 0;
	for (; str && str[i] && i < 32; ++i) {
		const char c = str[i];
		if (c == '"' || c == '\\') {
			out += '\\';
			out += c;
		} else if ((unsigned char)c < 32 || (unsigned char)c >= 127) {
			out += '?';
		} else {
			out += c;
		}
	}
	if (str && str[i])
		out += "...";
	out += '"';
	return out;
}

// Stable, canonical URL component (see DESIGN.md: byte-stable,
// lowercase-stable, no extra slashes).
static Common::String sanitizeUrlComponent(const Common::String &name) {
	Common::String out;
	for (uint i = 0; i < name.size(); ++i) {
		char c = name[i];
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		                c == '.' || c == '-' || c == '_';
		out += ok ? c : '_';
	}
	if (out.empty())
		out = "script";
	return out;
}

static int registerIndexByName(const Common::String &name) {
	for (int r = SREG_SP; r < CC_NUM_REGISTERS; ++r) {
		if (name.equalsIgnoreCase(regnames[r]))
			return r;
	}
	return -1;
}

// --- lifecycle -------------------------------------------------------------

AGSInspectorAgent::AGSInspectorAgent() : _session(nullptr), _chainedHook(nullptr),
		_hotScript(nullptr), _hotSection(nullptr), _hotHandle(-1) {
}

void AGSInspectorAgent::init() {
	_session = Inspector::initSession(this);
	if (!_session)
		return;
	installNewLineHook();
}

void AGSInspectorAgent::installNewLineHook() {
	if (_G(new_line_hook) == &inspectorNewLineHook)
		return;
	// Chain whatever was installed before us (a plugin's
	// AGSE_SCRIPTDEBUG hook; the editor-debugger hook never installs on
	// ScummVM), so we run first and forward.
	_chainedHook = _G(new_line_hook);
	ccSetDebugHook(&inspectorNewLineHook);
}

void AGSInspectorAgent::shutdown() {
	if (_G(new_line_hook) == &inspectorNewLineHook)
		ccSetDebugHook(_chainedHook);
	_chainedHook = nullptr;
	if (_session) {
		Inspector::shutdownSession();
		_session = nullptr;
	}
	_sections.clear();
	_hotScript = nullptr;
	_hotSection = nullptr;
	_hotHandle = -1;
}

void AGSInspectorAgent::transportTick() {
	if (!_session)
		return;
	// A plugin requesting/unrequesting AGSE_SCRIPTDEBUG replaces the
	// debug hook wholesale; hook back in front of it once per frame.
	installNewLineHook();
	Inspector::transportTick();
}

Common::String AGSInspectorAgent::targetTitle() const {
	return ConfMan.getActiveDomainName() + " (AGS)";
}

// --- the VM hook -----------------------------------------------------------

void AGSInspectorAgent::onScriptLine(ccInstance *inst, int lineNumber) {
	if (!inst || !_session)
		return;
	// SCMD_LINENUM fires with inst->pc still at the line-number opcode:
	// a statement start in our listings. inst is the activation chain
	// (the "thread"); the executing code belongs to inst->runningInst,
	// which differs from inst during far (cross-script) calls.
	//
	// Registration must happen BEFORE the armed() gate: deferred URL
	// breakpoints only bind when a script registers, and armed() only
	// becomes true once a breakpoint has resolved — gating registration
	// on armed() would deadlock breakpoint binding. The section lookup
	// has a pointer-compare fast path, so the disarmed cost is small.
	ccInstance *codeInst = inst->runningInst ? inst->runningInst : inst;
	const int handle = ensureSectionRegistered(codeInst, inst->pc);
	if (handle < 0 || !_session->armed())
		return;
	_session->instructionHook(threadIdFor(inst), handle, (uint32)inst->pc,
	                          (uint32)inst->callStackSize + 1);
}

uint32 AGSInspectorAgent::threadIdFor(const ccInstance *inst) {
	const uint64 v = (uint64)(uintptr_t)inst;
	const uint32 folded = (uint32)(v ^ (v >> 32));
	return folded ? folded : 1;
}

ccInstance *AGSInspectorAgent::threadInstance(uint32 threadId) const {
	if (threadId != 0) {
		std::deque<ccInstance *> &threads = _GP(InstThreads);
		for (size_t i = 0; i < threads.size(); ++i) {
			ccInstance *inst = threads.at(i);
			if (inst && threadIdFor(inst) == threadId)
				return inst;
		}
	}
	return ccInstance::GetCurrentInstance();
}

// --- script (section) registration ------------------------------------------

// Mirrors ccScript::GetSectionName(): the first section whose offset is
// >= offs ends the search, and the instruction belongs to the previous
// one; -1 means "(unknown section)" (offsets at or below
// sectionOffsets[0], or scripts without section info).
int AGSInspectorAgent::sectionIndexFor(const ccScript *scr, int32 offset) const {
	int i;
	for (i = 0; i < scr->numSections; ++i) {
		if (scr->sectionOffsets[i] >= offset)
			break;
	}
	return i - 1;
}

bool AGSInspectorAgent::scriptStillLoaded(const SectionRecord &rec) const {
	for (int i = 0; i < MAX_LOADED_INSTANCES; ++i) {
		const ccInstance *inst = _G(loadedInstances)[i];
		if (inst && inst->instanceof.get() == rec.script && inst->code == rec.codePtr)
			return true;
	}
	return false;
}

int AGSInspectorAgent::ensureSectionRegistered(ccInstance *codeInst, int32 pc) {
	if (!_session || !codeInst || !codeInst->instanceof || !codeInst->code)
		return -1;
	const ccScript *scr = codeInst->instanceof.get();
	const char *sectionName = scr->GetSectionName(pc);
	if (!sectionName)
		sectionName = "(unknown section)";

	// Hot path: GetSectionName() returns a pointer into the script's own
	// name table, so consecutive hooks in one section compare pointers.
	if (scr == _hotScript && sectionName == _hotSection)
		return _hotHandle;

	const Common::String name(sectionName);
	int staleSlot = -1;
	int liveSameName = 0;
	for (uint i = 0; i < _sections.size(); ++i) {
		const SectionRecord &rec = _sections[i];
		if (rec.name != name)
			continue;
		if (rec.script == scr && rec.codePtr == codeInst->code &&
		        rec.codeSize == codeInst->codesize) {
			_hotScript = scr;
			_hotSection = sectionName;
			_hotHandle = rec.handle;
			return rec.handle;
		}
		if (scriptStillLoaded(rec))
			liveSameName++; // shared header with code in two live scripts
		else if (staleSlot < 0)
			staleSlot = (int)i; // script was reloaded (e.g. RunAGSGame)
	}

	// Register a fresh script. A stale record keeps its URL (fresh
	// handle, mirroring V8 re-parse behaviour); a genuine same-name
	// collision gets a distinct, deterministic URL.
	SectionRecord rec;
	rec.name = name;
	if (staleSlot >= 0) {
		rec.url = _sections[staleSlot].url;
	} else {
		rec.url = Common::String::format("scummvm-dbg://ags/%s",
		                                 sanitizeUrlComponent(name).c_str());
		if (liveSameName > 0)
			rec.url += Common::String::format("~%d", liveSameName + 1);
	}
	rec.script = scr;
	rec.codePtr = codeInst->code;
	rec.codeSize = codeInst->codesize;

	Inspector::ScriptListing listing;
	buildListing(codeInst, pc, rec.url, listing);
	rec.handle = _session->registerScript(listing);

	if (staleSlot >= 0)
		_sections[staleSlot] = rec;
	else
		_sections.push_back(rec);

	_hotScript = scr;
	_hotSection = sectionName;
	_hotHandle = rec.handle;
	return rec.handle;
}

// --- listing (static disassembly) -------------------------------------------

Common::String AGSInspectorAgent::disassembleArg(const ccInstance *codeInst, int32 codePos,
                                                 bool isReg) const {
	const intptr_t value = codeInst->code[codePos];
	if (isReg) {
		if (value >= 0 && value < CC_NUM_REGISTERS)
			return Common::String(regnames[value]);
		return Common::String::format("r%d", (int)value);
	}
	// code_fixups (per code index) tells how the raw word is interpreted;
	// FIXUP_GLOBALDATA was rewritten to a real pointer at instance
	// creation, FIXUP_IMPORT to an index into the global import table.
	const char fixup = codeInst->code_fixups ? codeInst->code_fixups[codePos] : (char)FIXUP_NOFIXUP;
	switch (fixup) {
	case FIXUP_GLOBALDATA: {
		const ScriptVariable *var = (const ScriptVariable *)value;
		return Common::String::format("gvar@%d", var ? (int)var->ScAddress : -1);
	}
	case FIXUP_FUNCTION: {
		Common::String fn = exportedFunctionAt(codeInst->instanceof.get(), (int32)value);
		if (!fn.empty())
			return Common::String::format("&%s", fn.c_str());
		return Common::String::format("func@%d", (int)value);
	}
	case FIXUP_STRING:
		if (value >= 0 && value < codeInst->stringssize && codeInst->strings)
			return quoteStringLiteral(codeInst->strings + value);
		return Common::String::format("str@%d", (int)value);
	case FIXUP_IMPORT: {
		const ScriptImport *import = _GP(simp).getByIndex((uint32)value);
		if (import && !import->Name.IsEmpty())
			return Common::String::format("&%s", import->Name.GetCStr());
		return Common::String::format("import#%d", (int)value);
	}
	case FIXUP_STACK:
		return Common::String::format("stack@%d", (int)value);
	default:
		return Common::String::format("%d", (int)value);
	}
}

void AGSInspectorAgent::buildListing(const ccInstance *codeInst, int32 pcHint,
                                     const Common::String &url,
                                     Inspector::ScriptListing &listing) const {
	const ccScript *scr = codeInst->instanceof.get();
	listing.url = url;

	// Section bounds, mirroring GetSectionName(): an offset belongs to
	// section k when sectionOffsets[k] < offset <= sectionOffsets[k+1]
	// (last section extends to the end of code; k == -1 covers offsets
	// at or below sectionOffsets[0]).
	const int secIdx = sectionIndexFor(scr, pcHint);
	int32 lo = INT32_MIN;
	int32 hi = INT32_MAX;
	if (secIdx >= 0)
		lo = scr->sectionOffsets[secIdx];
	if (secIdx + 1 < scr->numSections)
		hi = scr->sectionOffsets[secIdx + 1];

	// Walk the whole code array from 0 so instruction boundaries are
	// derived exactly like the interpreter's (and DetermineScriptLine's)
	// fetch: opcode word (instance id masked off) + fixed arg count.
	//
	// Statement lines are exactly the SCMD_LINENUM instructions: the VM's
	// new-line hook fires with pc at those, so every offset passed to
	// instructionHook() is a statement-line offset of this listing. All
	// other instructions are non-statement disassembly lines.
	const intptr_t *code = codeInst->code;
	const int32 codeSize = codeInst->codesize;
	for (int32 pc = 0; pc < codeSize;) {
		const int op = (int)(code[pc] & INSTANCE_ID_REMOVEMASK);
		const char *opName = nullptr;
		int argCount = 0;
		const bool *argIsReg = nullptr;
		if (!script_commands_get_info(op, opName, argCount, argIsReg))
			break; // unknown opcode: cannot walk further safely
		if (pc + argCount >= codeSize)
			break; // truncated instruction
		if (pc > hi)
			break; // walked past our section
		if (pc > lo) {
			if (op == SCMD_LINENUM) {
				const int srcLine = (int)code[pc + 1];
				listing.lines.push_back(Inspector::ListingLine(
					(uint32)pc,
					Common::String::format("[%06d] // line %d", pc, srcLine),
					true, srcLine > 0 ? srcLine - 1 : -1));
			} else {
				Common::String text = Common::String::format("[%06d]   %s", pc, opName);
				for (int i = 0; i < argCount; ++i) {
					text += (i == 0) ? " " : ", ";
					text += disassembleArg(codeInst, pc + 1 + i, argIsReg[i]);
				}
				listing.lines.push_back(Inspector::ListingLine((uint32)pc, text, false));
			}
		}
		pc += argCount + 1;
	}
}

// --- frames and scopes -------------------------------------------------------

// Nearest preceding exported function within the same section; local
// (non-exported) functions inherit the closest exported name, which is
// the usual nearest-symbol convention.
Common::String AGSInspectorAgent::functionNameAt(const ccInstance *codeInst, int32 pc,
                                                 int lineNumber) const {
	const ccScript *scr = codeInst->instanceof ? codeInst->instanceof.get() : nullptr;
	if (!scr)
		return Common::String::format("pc %d", pc);
	const int secIdx = sectionIndexFor(scr, pc);
	const char *best = nullptr;
	int32 bestAddr = -1;
	for (int k = 0; k < scr->numexports; ++k) {
		if (((scr->export_addr[k] >> 24L) & 0x000ff) != EXPORT_FUNCTION)
			continue;
		const int32 eaddr = scr->export_addr[k] & 0x00ffffff;
		if (eaddr > pc || eaddr <= bestAddr)
			continue;
		if (sectionIndexFor(scr, eaddr) != secIdx)
			continue; // do not leak names across section boundaries
		bestAddr = eaddr;
		best = scr->exports[k];
	}
	if (best)
		return stripExportName(best);
	return Common::String::format("%s:%d", scr->GetSectionName(pc), lineNumber);
}

void AGSInspectorAgent::appendFrame(ccInstance *inst, ccInstance *codeInst, int32 addr,
                                    int lineNumber, Common::Array<Inspector::CallFrameInfo> &frames) {
	Inspector::CallFrameInfo frame;
	frame.scriptHandle = ensureSectionRegistered(codeInst, addr);
	frame.offset = (uint32)addr;
	frame.functionName = functionNameAt(codeInst, addr, lineNumber);

	Inspector::ScopeInfo regs;
	regs.type = "local";
	regs.name = "Registers";
	frame.scopes.push_back(regs);

	Inspector::ScopeInfo stack;
	stack.type = "local";
	stack.name = "Stack (top)";
	frame.scopes.push_back(stack);

	Inspector::ScopeInfo globals;
	globals.type = "global";
	const char *section = (codeInst->instanceof) ? codeInst->instanceof->GetSectionName(addr) : "?";
	globals.name = Common::String::format("Globals (%s)", section);
	frame.scopes.push_back(globals);

	frames.push_back(frame);
}

void AGSInspectorAgent::buildCallFrames(uint32 threadId,
                                        Common::Array<Inspector::CallFrameInfo> &frames) {
	ccInstance *inst = threadInstance(threadId);
	if (!inst)
		return;

	// Innermost frame: the live execution position...
	appendFrame(inst, inst->runningInst ? inst->runningInst : inst, inst->pc,
	            inst->line_number, frames);
	// ...then the recorded call sites (near and far calls both push
	// these arrays), most recent first.
	for (int j = inst->callStackSize - 1; j >= 0; --j) {
		ccInstance *codeInst = inst->callStackCodeInst[j];
		if (!codeInst)
			continue;
		appendFrame(inst, codeInst, inst->callStackAddr[j], inst->callStackLineNumber[j], frames);
	}
}

// The ccInstance owning the code of one frame built by buildCallFrames:
// frame 0 is the executing (running) instance, frame n maps to call
// stack entry callStackSize - n.
ccInstance *AGSInspectorAgent::frameCodeInst(ccInstance *inst, int frameIndex) const {
	if (!inst)
		return nullptr;
	if (frameIndex > 0) {
		const int j = inst->callStackSize - frameIndex;
		if (j >= 0 && j < inst->callStackSize && inst->callStackCodeInst[j])
			return inst->callStackCodeInst[j];
	}
	return inst->runningInst ? inst->runningInst : inst;
}

Inspector::DebugValue AGSInspectorAgent::valueOfRSV(const ccInstance *inst,
                                                    const RuntimeScriptValue &val) const {
	switch (val.Type) {
	case kScValUndefined:
		return Inspector::DebugValue::undefined();
	case kScValInteger:
	case kScValPluginArg:
		return Inspector::DebugValue::fromInt(val.IValue);
	case kScValFloat:
		return Inspector::DebugValue::fromDouble(val.FValue);
	case kScValStringLiteral:
		return val.CStr ? Inspector::DebugValue::fromString(val.CStr)
		                : Inspector::DebugValue::null();
	case kScValStackPtr:
		if (inst && inst->stack && val.RValue >= inst->stack &&
		        val.RValue <= inst->stack + inst->num_stackentries)
			return Inspector::DebugValue::fromString(
				Common::String::format("<stack[%d]>", (int)(val.RValue - inst->stack)));
		return Inspector::DebugValue::fromString("<stack ptr>");
	case kScValGlobalVar:
		// Pointer to a script global; show the 32-bit value it holds.
		return Inspector::DebugValue::fromInt(val.ReadInt32());
	case kScValData:
		return Inspector::DebugValue::fromString(
			Common::String::format("<data:%d>", val.Size));
	case kScValScriptObject:
	case kScValPluginObject:
	case kScValStaticArray: {
		if (val.IsNull())
			return Inspector::DebugValue::null();
		const AGS::Shared::String name = _GP(simp).findName(val);
		if (!name.IsEmpty())
			return Inspector::DebugValue::fromString(
				Common::String::format("&%s", name.GetCStr()));
		return Inspector::DebugValue::fromString("<object>");
	}
	case kScValStaticFunction:
	case kScValObjectFunction:
	case kScValPluginFunction: {
		const AGS::Shared::String name = _GP(simp).findName(val);
		if (!name.IsEmpty())
			return Inspector::DebugValue::fromString(
				Common::String::format("&%s", name.GetCStr()));
		return Inspector::DebugValue::fromString("<function>");
	}
	case kScValCodePtr:
		return Inspector::DebugValue::fromString("<code ptr>");
	default:
		return Inspector::DebugValue::fromString("<?>");
	}
}

void AGSInspectorAgent::buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
                                         Inspector::RemoteObjectTable &table, int objRef) {
	ccInstance *inst = threadInstance(threadId);
	if (!inst)
		return;

	if (scopeIndex == 0) {
		// VM registers. They belong to the activation chain, not to one
		// frame, so all frames show the live values.
		for (int r = SREG_SP; r < CC_NUM_REGISTERS; ++r)
			table.addProperty(objRef, regnames[r], valueOfRSV(inst, inst->registers[r]));
	} else if (scopeIndex == 1) {
		// Bounded window of the value stack top (function args and
		// locals live here; AGS bytecode carries no local names).
		const RuntimeScriptValue &sp = inst->registers[SREG_SP];
		if (sp.Type != kScValStackPtr || !sp.RValue || !inst->stack)
			return;
		RuntimeScriptValue *top = sp.RValue;
		if (top < inst->stack || top > inst->stack + inst->num_stackentries)
			return;
		int count = 0;
		for (RuntimeScriptValue *entry = top - 1; entry >= inst->stack && count < 12; --entry, ++count)
			table.addProperty(objRef, Common::String::format("stack[%d]", (int)(entry - inst->stack)),
			                  valueOfRSV(inst, *entry));
	} else {
		// Named globals: the script's data exports ("export" keyword),
		// resolved to real global memory at instance creation.
		ccInstance *codeInst = frameCodeInst(inst, frameIndex);
		if (!codeInst || !codeInst->instanceof || !codeInst->exports)
			return;
		const ccScript *scr = codeInst->instanceof.get();
		for (int k = 0; k < scr->numexports; ++k) {
			if (((scr->export_addr[k] >> 24L) & 0x000ff) != EXPORT_DATA)
				continue;
			table.addProperty(objRef, stripExportName(scr->exports[k]),
			                  valueOfRSV(codeInst, codeInst->exports[k]));
		}
	}
}

// --- evaluate ----------------------------------------------------------------

// Supported forms: register names (ax, bx, sp, ...), exported global
// variable names, "name = <int>" pokes and "lhs == <int>" comparisons
// (breakpoint conditions).
bool AGSInspectorAgent::evaluate(uint32 threadId, int frameIndex,
                                 const Common::String &expression,
                                 Inspector::RemoteObjectTable &table,
                                 Inspector::DebugValue &result) {
	Common::String expr = expression;
	expr.trim();
	if (expr.empty())
		return false;

	ccInstance *inst = threadInstance(threadId);
	if (!inst)
		inst = _G(gameinst).get(); // global context while nothing runs
	if (!inst)
		return false;
	ccInstance *codeInst = frameCodeInst(inst, frameIndex < 0 ? 0 : frameIndex);
	if (!codeInst)
		return false;

	// Comparison "lhs == <int>" (used for breakpoint conditions).
	const char *eqeq = strstr(expr.c_str(), "==");
	if (eqeq) {
		Common::String lhs(expr.c_str(), eqeq);
		Common::String rhs(eqeq + 2);
		lhs.trim();
		rhs.trim();
		Inspector::DebugValue lval;
		if (!evaluate(threadId, frameIndex, lhs, table, lval))
			return false;
		const long want = strtol(rhs.c_str(), nullptr, 0);
		if (lval.type == Inspector::DebugValue::kInt)
			result = Inspector::DebugValue::fromBool(lval.intVal == want);
		else if (lval.type == Inspector::DebugValue::kBool)
			result = Inspector::DebugValue::fromBool(lval.boolVal == (want != 0));
		else
			return false;
		return true;
	}

	// Assignment "name = <int>" (a single '=', not <=, >=, !=).
	const char *eq = strchr(expr.c_str(), '=');
	if (eq && eq != expr.c_str() && eq[1] != '=' &&
	        eq[-1] != '<' && eq[-1] != '>' && eq[-1] != '!') {
		Common::String lhs(expr.c_str(), eq);
		Common::String rhs(eq + 1);
		lhs.trim();
		rhs.trim();
		const int32 value = (int32)strtol(rhs.c_str(), nullptr, 0);

		const int reg = registerIndexByName(lhs);
		if (reg >= 0) {
			inst->registers[reg].SetInt32(value);
			result = Inspector::DebugValue::fromInt(value);
			return true;
		}
		const ccScript *scr = codeInst->instanceof ? codeInst->instanceof.get() : nullptr;
		if (!scr || !codeInst->exports)
			return false;
		for (int k = 0; k < scr->numexports; ++k) {
			if (((scr->export_addr[k] >> 24L) & 0x000ff) != EXPORT_DATA)
				continue;
			if (stripExportName(scr->exports[k]) != lhs)
				continue;
			codeInst->exports[k].WriteInt32(value);
			result = Inspector::DebugValue::fromInt(codeInst->exports[k].ReadInt32());
			return true;
		}
		return false;
	}

	// Plain reads: a VM register...
	const int reg = registerIndexByName(expr);
	if (reg >= 0) {
		result = valueOfRSV(inst, inst->registers[reg]);
		return true;
	}
	// ...or an exported global of the frame's script.
	const ccScript *scr = codeInst->instanceof ? codeInst->instanceof.get() : nullptr;
	if (!scr || !codeInst->exports)
		return false;
	for (int k = 0; k < scr->numexports; ++k) {
		if (((scr->export_addr[k] >> 24L) & 0x000ff) != EXPORT_DATA)
			continue;
		if (stripExportName(scr->exports[k]) != expr)
			continue;
		result = valueOfRSV(codeInst, codeInst->exports[k]);
		return true;
	}
	return false;
}

Common::String AGSInspectorAgent::describeThread(uint32 threadId) const {
	ccInstance *inst = threadInstance(threadId);
	if (!inst)
		return Common::String::format("ags-thread-%u", threadId);
	const ccInstance *codeInst = inst->runningInst ? inst->runningInst : inst;
	const char *section = codeInst->instanceof ? codeInst->instanceof->GetSectionName(inst->pc) : "?";
	return Common::String::format("AGS \"%s\" line %d (instance %d)",
	                              section, inst->line_number, inst->loadedInstanceId);
}

} // namespace AGS3
