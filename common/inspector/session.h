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

#ifndef COMMON_INSPECTOR_SESSION_H
#define COMMON_INSPECTOR_SESSION_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/inspector/agent.h"
#include "common/inspector/breakpoints.h"
#include "common/inspector/registry.h"
#include "common/inspector/remoteobject.h"
#include "common/inspector/stepping.h"

namespace Inspector {

class Command;

/**
 * Callback the Session uses to keep servicing the protocol while the VM
 * is blocked in a pause. The V8 embedder analog is
 * runMessageLoopOnPause(): a paused target that stops reading its socket
 * deadlocks every client (Deno hit a hard crash on exactly this,
 * denoland/deno#5822 — ledger #13).
 *
 * The real transport delivers queued WebSocket messages via
 * Session::onMessage() and sleeps briefly; unit tests feed scripted
 * command sequences. Return false to abort the pause (client gone).
 */
class PausePump {
public:
	virtual ~PausePump() {}
	virtual bool pumpWhilePaused() = 0;
};

/**
 * One CDP debug session wiring a single engine Agent to a single client.
 *
 * Threading contract: every method is called on the engine thread —
 * onMessage() either from the engine's per-frame pump or from inside the
 * PausePump while the VM is blocked in instructionHook(). The socket
 * transport only moves bytes and queues strings (see
 * backends/networking/sdl_net's inspector server).
 */
class Session {
public:
	explicit Session(Agent *agent);
	~Session();

	// --- transport side ---

	/** Dispatch one incoming CDP message; responses/events are queued. */
	void onMessage(const Common::String &json);

	/** Drain one queued outgoing message. */
	bool nextOutgoing(Common::String &out);
	bool hasOutgoing() const { return !_outgoing.empty(); }

	/** Client disconnected: resume, drop session-scoped state (node
	 *  resumes and discards the session's breakpoints on detach). */
	void onDisconnect();

	void setPausePump(PausePump *pump) { _pump = pump; }

	// --- engine side ---

	/**
	 * Register a script (usually at resource-load time). Emits
	 * Debugger.scriptParsed and binds pending breakpoints when a debugger
	 * is attached; scripts registered earlier are replayed on
	 * Debugger.enable. @return script handle, -1 on malformed listing
	 */
	int registerScript(const ScriptListing &listing);

	/**
	 * Fast-path guard for the per-instruction hook: engines call
	 *   if (session->armed()) session->instructionHook(...);
	 * so the disabled cost is one predictable branch.
	 */
	bool armed() const { return _armed; }

	/**
	 * The per-instruction VM hook. @p callDepth is the engine's current
	 * script-call depth (>= 1); frame identity is derived from it (see
	 * FrameTracker). May block inside a pause pump.
	 */
	void instructionHook(uint32 threadId, int scriptHandle, uint32 offset, uint32 callDepth);

	/** Script runtime error. Pauses when setPauseOnExceptions is active;
	 *  reports Runtime.exceptionThrown when the runtime domain is on. */
	void reportException(uint32 threadId, int scriptHandle, uint32 offset,
	                     const Common::String &description);

	/** Forward engine console/log output as Runtime.consoleAPICalled.
	 *  @p level is a CDP console type ("log", "warning", "error", ...) */
	void consoleMessage(const Common::String &level, const Common::String &text);

	/** Guard for variable watch hooks (cheap flag). */
	bool watchArmed() const { return _watchArmed; }

	/** Variable read/write hook for GameScript.setWatchpoint. */
	void variableAccessHook(uint32 threadId, const Common::String &name,
	                        bool isWrite, const DebugValue &value);

	/** Hold the VM at its first instruction until the client sends
	 *  Runtime.runIfWaitingForDebugger (--inspect-brk equivalent;
	 *  without it fast games finish before the client attaches). */
	void setWaitForDebugger(bool wait);

	// --- state inspection (server / tests) ---

	bool paused() const { return _paused; }
	bool debuggerEnabled() const { return _debuggerEnabled; }
	const ScriptRegistry &registry() const { return _registry; }
	Agent *agent() const { return _agent; }

private:
	Agent *_agent;
	PausePump *_pump;

	ScriptRegistry _registry;
	BreakpointStore _breakpoints;
	StepController _step;
	RemoteObjectTable _objects;

	typedef Common::HashMap<uint32, FrameTracker *> TrackerMap;
	TrackerMap _trackers;

	Common::Array<Common::String> _outgoing;
	uint32 _outgoingHead; ///< consumed prefix of _outgoing

	bool _runtimeEnabled;
	bool _debuggerEnabled;
	bool _armed;
	bool _paused;
	uint32 _pausedThread;
	bool _pauseRequested;
	bool _skipAllPauses;
	bool _waitingForDebugger;
	bool _runIfWaitingReceived;
	Common::String _pauseOnExceptions; ///< "none" | "caught" | "uncaught" | "all"
	int64 _eventSerial;                ///< monotonic pseudo-timestamp

	// Cached state of the current pause.
	Common::Array<CallFrameInfo> _pausedFrames;
	typedef Common::HashMap<uint32, int> ScopeCache; ///< (frame<<16|scope) -> objRef
	ScopeCache _scopeCache;

	struct Watchpoint {
		Common::String id;
		Common::String name;
		bool onRead;
		bool onWrite;
	};
	Common::Array<Watchpoint> _watchpoints;
	int _nextWatchpointId;
	bool _watchArmed;

	// --- plumbing ---
	void queueMessage(const Common::String &msg);
	void queueEvent(const Common::String &method, Common::JSONValue *params);
	void updateArmed();
	FrameTracker &trackerFor(uint32 threadId);

	// --- pause machinery ---
	void doPause(uint32 threadId, const Common::String &reason,
	             const Common::Array<Common::String> *hitBreakpointIds,
	             Common::JSONValue *data);
	void resumeExecution(bool emitResumed);
	Common::JSONValue *buildCallFramesJSON();
	bool checkBreakpointCondition(uint32 threadId, const Common::String &condition);

	// --- dispatch ---
	void dispatch(const Command &cmd);
	void handleRuntimeEnable(const Command &cmd);
	void handleDebuggerEnable(const Command &cmd);
	void handleSetBreakpointByUrl(const Command &cmd);
	void handleSetBreakpoint(const Command &cmd);
	void handleRemoveBreakpoint(const Command &cmd);
	void handleGetPossibleBreakpoints(const Command &cmd);
	void handleGetScriptSource(const Command &cmd);
	void handleStep(const Command &cmd, StepMode mode);
	void handleEvaluate(const Command &cmd, bool onCallFrame);
	void handleGetProperties(const Command &cmd);
	void handleSetWatchpoint(const Command &cmd);
	void handleRemoveWatchpoint(const Command &cmd);
	void handleListThreads(const Command &cmd);

	bool parseFrameId(const Common::String &frameId, int &frameIndex) const;
	Common::JSONValue *locationJSON(int scriptHandle, int line) const;

	Session(const Session &);            // = delete
	Session &operator=(const Session &); // = delete
};

/**
 * The active session, or nullptr when no inspector is attached. Engine
 * adapters hook their VM loops through this:
 *   if (Inspector::g_session && Inspector::g_session->armed())
 *       Inspector::g_session->instructionHook(...);
 */
extern Session *g_session;

} // End of namespace Inspector

#endif
