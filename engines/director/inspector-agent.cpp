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

#include "director/inspector-agent.h"

#include "common/config-manager.h"
#include "common/inspector/inspector.h"

#include "director/director.h"
#include "director/lingo/lingo-object.h"

namespace Director {

DirectorInspectorAgent::DirectorInspectorAgent(DirectorEngine *vm) :
		_vm(vm), _session(nullptr), _lastScript(nullptr), _lastSize(0),
		_lastHandle(-1), _nextThreadSerial(1) {
}

void DirectorInspectorAgent::init() {
	_session = Inspector::initSession(this);
}

void DirectorInspectorAgent::shutdown() {
	if (_session) {
		Inspector::shutdownSession();
		_session = nullptr;
	}
}

Common::String DirectorInspectorAgent::targetTitle() const {
	return ConfMan.getActiveDomainName() + " (Director)";
}

Common::String DirectorInspectorAgent::describeThread(uint32 threadId) const {
	return Common::String::format("Lingo state %u", threadId);
}

uint32 DirectorInspectorAgent::threadIdFor(LingoState *state) {
	if (!state)
		return 1;
	Common::HashMap<void *, uint32>::iterator it = _threadSerials.find(state);
	if (it != _threadSerials.end())
		return it->_value;
	uint32 id = _nextThreadSerial++;
	_threadSerials[state] = id;
	return id;
}

int DirectorInspectorAgent::handleFor(ScriptData *script) const {
	if (!script)
		return -1;
	ScriptHandleMap::const_iterator it = _scriptHandles.find(script);
	if (it == _scriptHandles.end())
		return -1;
	return it->_value.handle;
}

Common::String DirectorInspectorAgent::sanitizeURLComponent(const Common::String &name) {
	Common::String out;
	for (uint i = 0; i < name.size(); i++) {
		char c = name[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
			out += c;
		else
			out += '_';
	}
	if (out.empty())
		out = "_";
	return out;
}

Common::String DirectorInspectorAgent::scriptURL(const Symbol &handler, ScriptContext *ctx) const {
	// scummvm-dbg://director/<scripttype>-<contextid>/<handlername>
	// Byte-for-byte stable and lowercase (js-debug matches URLs with
	// case-insensitive regexes; see DESIGN.md).
	const char *typeStr = scriptType2str(ctx ? ctx->_scriptType : kNoneScript);
	int ctxId = ctx ? ctx->_id : 0;
	Common::String handlerName = (handler.name && !handler.name->empty()) ? *handler.name : "anon";
	Common::String url = Common::String::format("scummvm-dbg://director/%s-%d/%s",
		typeStr, ctxId, sanitizeURLComponent(handlerName).c_str());
	url.toLowercase();
	return url;
}

void DirectorInspectorAgent::buildListing(ScriptData *script, Inspector::ScriptListing &listing) const {
	// Lingo "bytecode" is an array of inst function pointers with inline
	// operands; Lingo::decodeInstruction() knows every opcode's operand
	// layout (the same tables the interpreter runs from), so iterating it
	// yields exactly the pc values the dispatch loop stops at. Every
	// instruction is a statement (a valid step/breakpoint target).
	uint pc = 0;
	while (pc < script->size()) {
		const uint start = pc;
		Common::String text = g_lingo->decodeInstruction(script, pc, &pc);
		listing.lines.push_back(Inspector::ListingLine(
			start, Common::String::format("[%04d] %s", start, text.c_str()), true));
		if (pc <= start) // safety: decodeInstruction always advances, but never loop
			pc = start + 1;
	}
}

int DirectorInspectorAgent::ensureScriptRegistered(LingoState *state) {
	ScriptData *script = state->script;
	ScriptHandleMap::iterator it = _scriptHandles.find(script);
	if (it != _scriptHandles.end() && it->_value.size == script->size())
		return it->_value.handle;

	// Name the script after the handler about to run in it. The topmost
	// CFrame's Symbol is that handler (pushContext() sets _state->script
	// from it); the pointer key keeps re-lookups cheap and the size check
	// catches a freed ScriptData whose address got reused after a movie
	// switch (the URL is then re-registered under a fresh handle).
	Symbol sym;
	if (!state->callstack.empty())
		sym = state->callstack.back()->sp;
	ScriptContext *ctx = sym.ctx ? sym.ctx : state->context;

	Inspector::ScriptListing listing;
	listing.url = scriptURL(sym, ctx);
	buildListing(script, listing);

	ScriptRec rec;
	rec.handle = _session->registerScript(listing);
	rec.size = script->size();
	_scriptHandles[script] = rec;
	return rec.handle;
}

void DirectorInspectorAgent::onInstruction() {
	if (!_session)
		return;
	LingoState *state = g_lingo->_state;
	if (!state || !state->script)
		return;

	// Lazy registration must not depend on armed(): an attached client
	// needs Debugger.scriptParsed events (and pending-breakpoint binding)
	// before any breakpoint exists. The pointer+size fast path keeps this
	// to two compares per instruction once a handler is known.
	if (state->script != _lastScript || state->script->size() != _lastSize) {
		_lastScript = state->script;
		_lastSize = state->script->size();
		_lastHandle = ensureScriptRegistered(state);
	}

	if (_lastHandle < 0 || !_session->armed())
		return;

	// _state->pc still indexes the instruction about to execute (the
	// dispatch below the hook does pc++ then runs pc - 1).
	_session->instructionHook(threadIdFor(state), _lastHandle, state->pc,
	                          state->callstack.size());
}

void DirectorInspectorAgent::onVariableRead(const Common::String &name) {
	if (!_session || !_session->watchArmed())
		return;
	Datum value;
	readVariable(0, name, value);
	_session->variableAccessHook(threadIdFor(g_lingo->_state), name, false,
	                             datumToValue(value));
}

void DirectorInspectorAgent::onVariableWrite(const Common::String &name) {
	if (!_session || !_session->watchArmed())
		return;
	Datum value;
	readVariable(0, name, value);
	_session->variableAccessHook(threadIdFor(g_lingo->_state), name, true,
	                             datumToValue(value));
}

void DirectorInspectorAgent::transportTick() {
	if (_session)
		Inspector::transportTick();
}

Common::String DirectorInspectorAgent::frameFunctionName(CFrame *frame) const {
	// Mirrors Lingo::formatCallStack()'s naming.
	if (frame->sp.type == VOIDSYM || !frame->sp.name)
		return "[unknown]";
	Common::String result;
	if (frame->sp.ctx && frame->sp.ctx->_id)
		result += Common::String::format("%d:", frame->sp.ctx->_id);
	if (frame->sp.ctx && frame->sp.ctx->isFactory())
		result += frame->sp.ctx->getName() + ":";
	result += *frame->sp.name;
	return result;
}

void DirectorInspectorAgent::buildCallFrames(uint32 threadId,
                                             Common::Array<Inspector::CallFrameInfo> &frames) {
	// The pause always happens synchronously inside the hook, so the
	// paused thread is the current LingoState.
	LingoState *state = g_lingo->_state;
	if (!state)
		return;
	const Common::Array<CFrame *> &callstack = state->callstack;
	const int count = (int)callstack.size();

	if (count == 0) {
		// Shouldn't happen at a hook pause, but degrade gracefully.
		if (!state->script)
			return;
		Inspector::CallFrameInfo info;
		info.functionName = "[top]";
		info.scriptHandle = handleFor(state->script);
		info.offset = state->pc;
		Inspector::ScopeInfo locals;
		locals.type = "local";
		locals.name = "Locals";
		info.scopes.push_back(locals);
		Inspector::ScopeInfo globals;
		globals.type = "global";
		globals.name = "Globals";
		info.scopes.push_back(globals);
		frames.push_back(info);
		return;
	}

	// callstack grows caller-first; CDP wants innermost first. The
	// innermost frame runs _state->script at _state->pc; an outer frame's
	// script/pc are the retScript/retPC saved by the frame pushed on top
	// of it (retPC is the caller's resume pc — the instruction after the
	// call, always a valid statement start; retPC - 1 would land inside
	// the call's inline operands).
	for (int i = count - 1; i >= 0; i--) {
		Inspector::CallFrameInfo info;
		if (i == count - 1) {
			info.scriptHandle = handleFor(state->script);
			info.offset = state->pc;
		} else {
			info.scriptHandle = handleFor(callstack[i + 1]->retScript);
			info.offset = (uint32)MAX(callstack[i + 1]->retPC, 0);
		}
		info.functionName = frameFunctionName(callstack[i]);

		Inspector::ScopeInfo locals;
		locals.type = "local";
		locals.name = "Locals";
		info.scopes.push_back(locals);
		Inspector::ScopeInfo globals;
		globals.type = "global";
		globals.name = "Globals";
		info.scopes.push_back(globals);
		if (i == count - 1) {
			Inspector::ScopeInfo stack;
			stack.type = "local";
			stack.name = "Stack (operands)";
			info.scopes.push_back(stack);
		}
		frames.push_back(info);
	}
}

DatumHash *DirectorInspectorAgent::localsForFrame(int frameIndex) const {
	LingoState *state = g_lingo->_state;
	if (!state)
		return nullptr;
	const int count = (int)state->callstack.size();
	if (frameIndex <= 0 || count == 0)
		return state->localVars;
	if (frameIndex >= count)
		return nullptr;
	// Frame N (innermost-first) executes callstack[count - 1 - N]; its
	// locals were saved as retLocalVars by the frame pushed on top of it.
	return state->callstack[count - frameIndex]->retLocalVars;
}

const Datum *DirectorInspectorAgent::meForFrame(int frameIndex) const {
	LingoState *state = g_lingo->_state;
	if (!state)
		return nullptr;
	const int count = (int)state->callstack.size();
	if (frameIndex <= 0 || count == 0)
		return &state->me;
	if (frameIndex >= count)
		return nullptr;
	return &state->callstack[count - frameIndex]->retMe;
}

Inspector::DebugValue DirectorInspectorAgent::datumToValue(const Datum &d) const {
	switch (d.type) {
	case INT:
		return Inspector::DebugValue::fromInt(d.u.i);
	case FLOAT:
		return Inspector::DebugValue::fromDouble(d.u.f);
	case STRING:
		return Inspector::DebugValue::fromString(*d.u.s);
	case SYMBOL:
		return Inspector::DebugValue::fromString(
			Common::String::format("#%s", d.u.s ? d.u.s->c_str() : ""));
	case VOID:
		return Inspector::DebugValue::null();
	default:
		break;
	}
	// Aggregates and references: readable one-line dump, same formatting
	// the engine's own debugger uses.
	return Inspector::DebugValue::fromString(
		Common::String::format("[%s] %s", d.type2str(), d.asString(true).c_str()));
}

void DirectorInspectorAgent::buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
                                              Inspector::RemoteObjectTable &table, int objRef) {
	LingoState *state = g_lingo->_state;
	if (!state)
		return;

	if (scopeIndex == 0) {
		// Locals: the frame's argument/declared-variable hash, plus "me".
		const Datum *me = meForFrame(frameIndex);
		if (me && me->type == OBJECT)
			table.addProperty(objRef, "me", datumToValue(*me));
		DatumHash *locals = localsForFrame(frameIndex);
		if (!locals)
			return;
		Common::Array<Common::String> keys;
		for (DatumHash::iterator it = locals->begin(); it != locals->end(); ++it)
			keys.push_back(it->_key);
		Common::sort(keys.begin(), keys.end());
		for (uint i = 0; i < keys.size(); i++)
			table.addProperty(objRef, keys[i], datumToValue(locals->getVal(keys[i])));
	} else if (scopeIndex == 1) {
		// Globals: every Lingo global the movie declared, capped so a
		// pathological movie can't flood the pane. Entries flagged
		// ignoreGlobal are ScummVM-internal (showGlobals skips them too).
		const uint kMaxGlobals = 200;
		Common::Array<Common::String> keys;
		for (DatumHash::iterator it = g_lingo->_globalvars.begin();
				it != g_lingo->_globalvars.end(); ++it) {
			if (!it->_value.ignoreGlobal)
				keys.push_back(it->_key);
		}
		Common::sort(keys.begin(), keys.end());
		for (uint i = 0; i < keys.size() && i < kMaxGlobals; i++)
			table.addProperty(objRef, keys[i], datumToValue(g_lingo->_globalvars.getVal(keys[i])));
		if (keys.size() > kMaxGlobals)
			table.addProperty(objRef, "...",
				Inspector::DebugValue::fromString(
					Common::String::format("(%u more globals not shown)",
						(uint)(keys.size() - kMaxGlobals))));
	} else {
		// Operand stack (innermost frame only): topmost entries first.
		const uint kMaxStack = 32;
		const StackData &stack = state->stack;
		uint shown = 0;
		for (int i = (int)stack.size() - 1; i >= 0 && shown < kMaxStack; i--, shown++)
			table.addProperty(objRef, Common::String::format("stack[%d]", i),
			                  datumToValue(stack[i]));
	}
}

bool DirectorInspectorAgent::parseLiteral(const Common::String &token, Datum &out) const {
	if (token.empty())
		return false;
	// Quoted string
	if (token.size() >= 2 && token[0] == '"' && token[token.size() - 1] == '"') {
		out = Datum(Common::String(token.c_str() + 1, token.size() - 2));
		return true;
	}
	// Integer / float
	bool digits = false, dot = false;
	for (uint i = 0; i < token.size(); i++) {
		char c = token[i];
		if (c == '-' && i == 0)
			continue;
		if (c == '.' && !dot) {
			dot = true;
			continue;
		}
		if (c < '0' || c > '9')
			return false;
		digits = true;
	}
	if (!digits)
		return false;
	if (dot)
		out = Datum(atof(token.c_str()));
	else
		out = Datum(atoi(token.c_str()));
	return true;
}

bool DirectorInspectorAgent::readVariable(int frameIndex, const Common::String &name, Datum &out) const {
	if (name.equalsIgnoreCase("the result")) {
		out = g_lingo->_theResult;
		return true;
	}
	DatumHash *locals = localsForFrame(frameIndex);
	if (locals && locals->contains(name)) {
		out = locals->getVal(name);
		return true;
	}
	const Datum *me = meForFrame(frameIndex);
	if (me && me->type == OBJECT && me->u.obj->hasProp(name)) {
		out = me->u.obj->getProp(name);
		return true;
	}
	if (g_lingo->_globalvars.contains(name)) {
		out = g_lingo->_globalvars.getVal(name);
		return true;
	}
	return false;
}

bool DirectorInspectorAgent::evaluate(uint32 threadId, int frameIndex,
                                      const Common::String &expression,
                                      Inspector::RemoteObjectTable &table,
                                      Inspector::DebugValue &result) {
	// Supported forms: bare variable names ("gScore", "the result"),
	// literals, "name = <literal>" pokes and "<name|literal> == <literal>"
	// breakpoint conditions — enough for watch panes and console use.
	Common::String expr = expression;
	expr.trim();
	if (expr.empty())
		return false;
	if (frameIndex < 0)
		frameIndex = 0;

	// Comparison "a == b"?
	int cmpPos = -1;
	for (uint i = 0; i + 1 < expr.size(); i++) {
		if (expr[i] == '=' && expr[i + 1] == '=') {
			cmpPos = (int)i;
			break;
		}
	}
	if (cmpPos >= 0) {
		Common::String lhs(expr.c_str(), (uint32)cmpPos);
		Common::String rhs(expr.c_str() + cmpPos + 2);
		lhs.trim();
		rhs.trim();
		Datum lval, rval;
		if (!parseLiteral(lhs, lval) && !readVariable(frameIndex, lhs, lval))
			return false;
		if (!parseLiteral(rhs, rval) && !readVariable(frameIndex, rhs, rval))
			return false;
		result = Inspector::DebugValue::fromBool(lval.equalTo(rval, true) != 0);
		return true;
	}

	// Assignment "name = literal"? (single '=', not preceded by <, >, !)
	for (uint i = 0; i < expr.size(); i++) {
		if (expr[i] != '=')
			continue;
		if (i > 0 && (expr[i - 1] == '<' || expr[i - 1] == '>' || expr[i - 1] == '!'))
			break;
		Common::String lhs(expr.c_str(), i);
		Common::String rhs(expr.c_str() + i + 1);
		lhs.trim();
		rhs.trim();
		Datum value;
		if (lhs.empty() || !parseLiteral(rhs, value))
			return false;
		DatumHash *locals = localsForFrame(frameIndex);
		if (locals && locals->contains(lhs))
			locals->setVal(lhs, value);
		else
			g_lingo->_globalvars[lhs] = value; // Lingo default scope
		result = datumToValue(value);
		return true;
	}

	Datum value;
	if (parseLiteral(expr, value) || readVariable(frameIndex, expr, value)) {
		result = datumToValue(value);
		return true;
	}
	return false;
}

} // End of namespace Director
