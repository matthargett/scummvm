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

#ifndef AGS_INSPECTOR_AGENT_H
#define AGS_INSPECTOR_AGENT_H

#include "common/array.h"
#include "common/inspector/agent.h"
#include "common/inspector/session.h"

namespace AGS3 {

struct ccInstance;
struct ccScript;
struct RuntimeScriptValue;

/**
 * Script-inspector adapter for the AGS bytecode interpreter (see
 * common/inspector/DESIGN.md). Each script *section* (the per-file unit
 * AGS's own debugging model uses: GlobalScript.asc, room scripts, module
 * scripts, headers with code) is presented as one script
 * (scummvm-dbg://ags/<section-name>) whose source is a static
 * disassembly of the owning ccInstance's code array. Listing offsets are
 * code-array indices, the exact values ccInstance::pc holds, so
 * breakpoints resolved by the core map 1:1 onto the interpreter's
 * program counter.
 *
 * Instead of adding a raw dispatch-loop hook, the adapter rides AGS's
 * existing per-source-line debug plumbing: ccSetDebugHook() installs a
 * new_line_hook that ccInstance::Run() invokes from the SCMD_LINENUM
 * opcode, with pc still pointing at the SCMD_LINENUM instruction itself.
 * The listing marks exactly those instructions as statement lines, so
 * hook offsets and statement offsets coincide and stepping lands
 * precisely. When no inspector session exists the hook is never
 * installed and the VM keeps its existing single `if (_G(new_line_hook))`
 * branch per source line.
 *
 * The editor-debugger hook (game_start.cpp) is never installed on
 * ScummVM (the editor transport is Windows-only and not ported), but AGS
 * plugins may install scriptDebugHook via IAGSEngine::RequestEventHook;
 * this adapter chains to whatever hook was installed before it, and
 * re-asserts itself once per frame in case a plugin replaced the hook
 * later.
 *
 * A "thread" is one ccInstance activation chain (an entry of
 * _GP(InstThreads): the global script, a fork running
 * repeatedly_execute_always while another instance blocks, a room or
 * dialog script instance); threadId folds the ccInstance pointer.
 * callDepth is callStackSize + 1, read from the engine's real call
 * stack, which both near (SCMD_CALL) and far (SCMD_CALLAS) calls push.
 */
class AGSInspectorAgent : public Inspector::Agent {
public:
	/** Matches new_line_hook_type (script_runtime.h). */
	typedef void (*NewLineHook)(ccInstance *, int);

	AGSInspectorAgent();

	/** Create the session (no-op unless inspector_enable is set) and
	 *  install the VM's new-line debug hook, chaining any previous one. */
	void init();
	void shutdown();

	/** Called from the installed new_line_hook on every SCMD_LINENUM
	 *  (and with inst == nullptr when a script invocation returns). */
	void onScriptLine(ccInstance *inst, int lineNumber);

	/** Per-frame transport pump, called from UpdateGameOnce(). Also
	 *  re-asserts the debug hook in case a plugin replaced it. */
	void transportTick();

	/** Previous new_line_hook (editor/plugin), forwarded after ours. */
	NewLineHook chainedNewLineHook() const { return _chainedHook; }

	bool active() const { return _session != nullptr; }

	// --- Inspector::Agent ---
	Common::String engineId() const override { return "ags"; }
	Common::String targetTitle() const override;
	void buildCallFrames(uint32 threadId, Common::Array<Inspector::CallFrameInfo> &frames) override;
	void buildScopeObject(uint32 threadId, int frameIndex, int scopeIndex,
	                      Inspector::RemoteObjectTable &table, int objRef) override;
	bool evaluate(uint32 threadId, int frameIndex, const Common::String &expression,
	              Inspector::RemoteObjectTable &table, Inspector::DebugValue &result) override;
	Common::String describeThread(uint32 threadId) const override;

private:
	/** One registered script section. A section is identified by its
	 *  name plus the owning compiled script (shared headers may repeat a
	 *  name across scripts; a reloaded game replaces the script). */
	struct SectionRecord {
		Common::String name;      ///< section name (GetSectionName result)
		Common::String url;       ///< stable synthetic URL
		const ccScript *script;   ///< owning compiled script
		const void *codePtr;      ///< ccInstance::code array identity
		int32 codeSize;
		int handle;               ///< Session::registerScript handle

		SectionRecord() : script(nullptr), codePtr(nullptr), codeSize(0), handle(-1) {}
	};

	Inspector::Session *_session;
	NewLineHook _chainedHook;
	Common::Array<SectionRecord> _sections;

	// Hot-path cache: consecutive line hooks stay in one section, so a
	// pointer compare avoids any string work per executed source line.
	const ccScript *_hotScript;
	const char *_hotSection;
	int _hotHandle;

	void installNewLineHook();

	static uint32 threadIdFor(const ccInstance *inst);
	ccInstance *threadInstance(uint32 threadId) const;
	ccInstance *frameCodeInst(ccInstance *inst, int frameIndex) const;

	int ensureSectionRegistered(ccInstance *codeInst, int32 pc);
	int sectionIndexFor(const ccScript *scr, int32 offset) const;
	bool scriptStillLoaded(const SectionRecord &rec) const;
	void buildListing(const ccInstance *codeInst, int32 pcHint,
	                  const Common::String &url, Inspector::ScriptListing &listing) const;
	Common::String disassembleArg(const ccInstance *codeInst, int32 codePos, bool isReg) const;
	Common::String functionNameAt(const ccInstance *codeInst, int32 pc, int lineNumber) const;
	void appendFrame(ccInstance *inst, ccInstance *codeInst, int32 addr, int lineNumber,
	                 Common::Array<Inspector::CallFrameInfo> &frames);
	Inspector::DebugValue valueOfRSV(const ccInstance *inst, const RuntimeScriptValue &val) const;
};

/**
 * Module-level adapter instance, mirroring how the engine's other debug
 * hooks are wired. Created in initialize_start_and_play_game() (only
 * kept when the inspector_enable config is set), destroyed in
 * quit_free().
 */
extern AGSInspectorAgent *g_inspectorAgent;

} // namespace AGS3

#endif
