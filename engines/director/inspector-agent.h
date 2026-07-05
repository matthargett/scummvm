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

#ifndef DIRECTOR_INSPECTOR_AGENT_H
#define DIRECTOR_INSPECTOR_AGENT_H

#include "common/array.h"
#include "common/hash-ptr.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/queue.h"
#include "common/rect.h"
#include "common/str-array.h"
#include "common/str.h"
#include "common/ustr.h"
#include "common/inspector/agent.h"
#include "common/inspector/session.h"

#include "director/types.h"

namespace Director {
class CastMember; // lingo.h relies on the includer declaring this
}

#include "director/lingo/lingo.h"

namespace Director {

class DirectorEngine;

/**
 * Script-inspector adapter for the Lingo interpreter (see
 * common/inspector/DESIGN.md). The CDP "script" unit is ONE HANDLER: each
 * ScriptData (the compiled body of one Lingo handler, an array of inst
 * function pointers plus inline operands) is registered lazily on first
 * execution as
 *
 *     scummvm-dbg://director/<scripttype>-<contextid>/<handlername>
 *
 * with a listing decoded by the engine's own Lingo::decodeInstruction().
 * Listing offsets are ScriptData indices — exactly the values
 * LingoState::pc holds at instruction starts — so breakpoints resolved by
 * the core match the interpreter's program counter 1:1.
 *
 * The VM hook sits in Lingo::execute() next to g_debugger->stepHook(),
 * before the instruction executes, when _state->pc still indexes the
 * instruction about to run. Each LingoState (one per window, plus frozen
 * states) is one inspector thread; call depth is the engine's real
 * _state->callstack.size().
 */
class DirectorInspectorAgent : public Inspector::Agent {
public:
	explicit DirectorInspectorAgent(DirectorEngine *vm);

	/** Create the session (no-op unless inspector_enable is set). */
	void init();
	void shutdown();

	/** Per-instruction hook, called from Lingo::execute() whenever the
	 *  adapter exists; registers unseen scripts, then re-checks armed(). */
	void onInstruction();

	/** Variable watch hooks (Debugger::varReadHook/varWriteHook). */
	void onVariableRead(const Common::String &name);
	void onVariableWrite(const Common::String &name);

	/** Per-frame transport pump, called from the DirectorEngine main loop
	 *  (and from Lingo::execute()'s periodic event servicing). */
	void transportTick();

	bool active() const { return _session != nullptr; }

	// --- Inspector::Agent ---
	Common::String engineId() const override { return "director"; }
	Common::String targetTitle() const override;
	void buildCallFrames(uint32 threadId, Common::Array<Inspector::CallFrameInfo> &frames) override;
	void buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
	                      Inspector::RemoteObjectTable &table, int objRef) override;
	bool evaluate(uint32 threadId, int frameIndex, const Common::String &expression,
	              Inspector::RemoteObjectTable &table, Inspector::DebugValue &result) override;
	Common::String describeThread(uint32 threadId) const override;

private:
	struct ScriptRec {
		int handle;
		uint32 size; ///< ScriptData size at registration (stale-pointer guard)

		ScriptRec() : handle(-1), size(0) {}
	};
	typedef Common::HashMap<ScriptData *, ScriptRec> ScriptHandleMap;

	DirectorEngine *_vm;
	Inspector::Session *_session;

	ScriptHandleMap _scriptHandles; ///< ScriptData* -> registered script
	ScriptData *_lastScript;        ///< onInstruction() fast path
	uint32 _lastSize;
	int _lastHandle;

	Common::HashMap<void *, uint32> _threadSerials; ///< LingoState* -> small id
	uint32 _nextThreadSerial;

	uint32 threadIdFor(LingoState *state);
	int handleFor(ScriptData *script) const;
	int ensureScriptRegistered(LingoState *state);
	void buildListing(ScriptData *script, Inspector::ScriptListing &listing) const;
	Common::String scriptURL(const Symbol &handler, ScriptContext *ctx) const;
	static Common::String sanitizeURLComponent(const Common::String &name);

	Inspector::DebugValue datumToValue(const Datum &d) const;
	DatumHash *localsForFrame(int frameIndex) const;
	const Datum *meForFrame(int frameIndex) const;
	bool readVariable(int frameIndex, const Common::String &name, Datum &out) const;
	bool parseLiteral(const Common::String &token, Datum &out) const;
	Common::String frameFunctionName(CFrame *frame) const;
};

} // End of namespace Director

#endif
