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

#ifndef WINTERMUTE_INSPECTOR_AGENT_H
#define WINTERMUTE_INSPECTOR_AGENT_H

#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/inspector/agent.h"
#include "common/inspector/session.h"

#include "engines/wintermute/base/scriptables/dcscript.h"

namespace Wintermute {

class BaseGame;
class ScScript;
class ScValue;

/**
 * Script-inspector adapter for the Wintermute script VM (see
 * common/inspector/DESIGN.md). Every compiled .script file is presented
 * as one script (scummvm-dbg://wintermute/<path>) whose source is a
 * static disassembly of the compiled buffer; the II_DBG_LINE markers the
 * WME compiler embeds become the statement lines (text "// line N"), so
 * breakpoints and steps land on source-line boundaries exactly like the
 * engine's own (compile-time-disabled) debugger. Scripts compiled
 * without debug info fall back to instruction-granularity statements.
 *
 * Every ListingLine.offset is the byte offset of an instruction start in
 * the compiled buffer — the exact values ScScript::_iP holds when the
 * per-instruction hook fires (top of executeInstruction(), before the
 * opcode fetch).
 *
 * Threads: each ScScript object (main script, event thread or method
 * thread) is one inspector thread. Threads created from the same file
 * share the script handle (their buffers are byte-identical copies) but
 * get their own thread id. Call depth is _callStack->_sP + 2 (the stack
 * pointer is -1 when no call is outstanding).
 */
class WintermuteInspectorAgent : public Inspector::Agent {
public:
	/** Set while a session is live; ScScript's guarded hooks route
	 *  through it (the object itself is owned by WintermuteEngine). */
	static WintermuteInspectorAgent *g_agent;

	explicit WintermuteInspectorAgent(BaseGame *game);
	~WintermuteInspectorAgent() override;

	/** Create the session (no-op unless inspector_enable is set). */
	void init();
	void shutdown();
	bool active() const { return _session != nullptr; }

	/** Per-frame transport pump + lazy registration sweep, called from
	 *  WintermuteEngine::messageLoop(). */
	void transportTick();

	/** Per-instruction hook, called from ScScript::executeInstruction()
	 *  before the opcode fetch (_iP = instruction start); the call site
	 *  checks Inspector::g_session->armed() so this is off the hot path
	 *  when no client interaction is pending. */
	void onInstruction(ScScript *script);

	/** ScScript::cleanup() notification — the object identity (pointer,
	 *  buffer, filename) dies or is about to be reused. */
	void onScriptCleanup(ScScript *script);

	/** ScScript::runtimeError() notification. */
	void onRuntimeError(ScScript *script, const Common::String &message);

	// --- Inspector::Agent ---
	Common::String engineId() const override { return "wintermute"; }
	Common::String targetTitle() const override;
	void buildCallFrames(uint32 threadId, Common::Array<Inspector::CallFrameInfo> &frames) override;
	void buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
	                      Inspector::RemoteObjectTable &table, int objRef) override;
	bool evaluate(uint32 threadId, int frameIndex, const Common::String &expression,
	              Inspector::RemoteObjectTable &table, Inspector::DebugValue &result) override;
	Common::String describeThread(uint32 threadId) const override;

private:
	struct ThreadInfo {
		uint32 threadId;
		int handle;

		ThreadInfo() : threadId(0), handle(-1) {}
	};

	typedef Common::HashMap<Common::String, int> HandleMap; ///< filename -> handle
	typedef Common::HashMap<uintptr, ThreadInfo> LiveMap;   ///< live ScScript* -> ids
	typedef Common::HashMap<uint32, ScScript *> ThreadMap;  ///< threadId -> live script

	BaseGame *_gameRef;
	Inspector::Session *_session;

	HandleMap _scriptHandles;
	LiveMap _liveScripts;
	ThreadMap _threads;
	uint32 _nextThreadId;

	// One-entry cache: ScEngine::tick() runs each script's instructions
	// back to back, so the armed hook usually skips both hash lookups.
	ScScript *_lastScript;
	uint32 _lastThreadId;
	int _lastHandle;

	TOpcodesType _opcodesType; ///< FoxTail opcode remap, as in ScScript

	bool ensureTracked(ScScript *script, ThreadInfo &info);
	ScScript *scriptForThread(uint32 threadId) const;
	uint32 decodeOpcode(uint32 inst) const;
	void buildListing(ScScript *script, Inspector::ScriptListing &listing) const;
	Common::String urlForFilename(const char *filename) const;
	Common::String segmentNameAt(ScScript *script, uint32 offset) const;
	ScValue *scopeBagFor(ScScript *script, int frameIndex) const;
	ScValue *findVariable(ScScript *script, int frameIndex, const char *name) const;
	Inspector::DebugValue debugValueOf(ScValue *val) const;
	void fillScopeFromBag(ScValue *bag, Inspector::RemoteObjectTable &table, int objRef) const;
	void checkVariableAccess(ScScript *script);
	bool parseLiteral(const Common::String &text, Inspector::DebugValue &out) const;
};

} // End of namespace Wintermute

#endif
