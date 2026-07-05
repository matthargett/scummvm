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

#include "common/inspector/session.h"
#include "common/inspector/protocol.h"

namespace Inspector {

Session *g_session = nullptr;

Session::Session(Agent *agent) :
	_agent(agent),
	_pump(nullptr),
	_outgoingHead(0),
	_runtimeEnabled(false),
	_debuggerEnabled(false),
	_armed(false),
	_paused(false),
	_pausedThread(0),
	_pauseRequested(false),
	_skipAllPauses(false),
	_waitingForDebugger(false),
	_runIfWaitingReceived(false),
	_pauseOnExceptions("none"),
	_eventSerial(1),
	_nextWatchpointId(1),
	_watchArmed(false) {
}

Session::~Session() {
	for (TrackerMap::iterator it = _trackers.begin(); it != _trackers.end(); ++it)
		delete it->_value;
}

// --- plumbing ---

void Session::queueMessage(const Common::String &msg) {
	_outgoing.push_back(msg);
}

void Session::queueEvent(const Common::String &method, Common::JSONValue *params) {
	queueMessage(buildEvent(method, params));
}

bool Session::nextOutgoing(Common::String &out) {
	if (_outgoingHead >= _outgoing.size()) {
		if (_outgoingHead > 0) {
			_outgoing.clear();
			_outgoingHead = 0;
		}
		return false;
	}
	out = _outgoing[_outgoingHead++];
	if (_outgoingHead == _outgoing.size()) {
		_outgoing.clear();
		_outgoingHead = 0;
	}
	return true;
}

void Session::updateArmed() {
	_armed = _waitingForDebugger || _pauseRequested || _step.armed() ||
	         (_breakpoints.anyResolved() && _breakpoints.active());
	_watchArmed = _debuggerEnabled && !_watchpoints.empty();
}

FrameTracker &Session::trackerFor(uint32 threadId) {
	TrackerMap::iterator it = _trackers.find(threadId);
	if (it != _trackers.end())
		return *it->_value;
	FrameTracker *tracker = new FrameTracker();
	_trackers[threadId] = tracker;
	return *tracker;
}

// --- engine side ---

int Session::registerScript(const ScriptListing &listing) {
	int handle = _registry.addScript(listing);
	if (handle < 0)
		return -1;
	if (_debuggerEnabled) {
		queueEvent("Debugger.scriptParsed", _registry.scriptParsedParams(handle, 1));
		Common::Array<BreakpointStore::Resolution> resolved;
		_breakpoints.bindScript(handle, _registry, resolved);
		for (uint32 i = 0; i < resolved.size(); i++) {
			Common::JSONObject params;
			params["breakpointId"] = new Common::JSONValue(resolved[i].breakpointId);
			params["location"] = locationJSON(resolved[i].location.scriptHandle,
			                                  resolved[i].location.line);
			queueEvent("Debugger.breakpointResolved", new Common::JSONValue(params));
		}
		updateArmed();
	}
	return handle;
}

void Session::setWaitForDebugger(bool wait) {
	_waitingForDebugger = wait;
	_runIfWaitingReceived = false;
	updateArmed();
}

void Session::instructionHook(uint32 threadId, int scriptHandle, uint32 offset, uint32 callDepth) {
	if (!_armed || _paused)
		return;

	uint64 token = trackerFor(threadId).feed(callDepth);

	// --inspect-brk equivalent: hold the very first instruction until
	// Runtime.runIfWaitingForDebugger arrives (ledger: deno#9886), then
	// stop there so the client can plant breakpoints before any bytecode
	// runs.
	if (_waitingForDebugger) {
		while (_waitingForDebugger && !_runIfWaitingReceived) {
			if (!_pump || !_pump->pumpWhilePaused()) {
				_runIfWaitingReceived = true; // transport gone: run free
				break;
			}
		}
		_waitingForDebugger = false;
		updateArmed();
		if (_debuggerEnabled)
			doPause(threadId, "other", nullptr, nullptr);
		return;
	}

	if (_skipAllPauses)
		return;

	int line = _registry.lineForOffset(scriptHandle, offset);
	bool isStatement = line >= 0 && _registry.isStatementLine(scriptHandle, line);

	// Breakpoints hit in ANY thread, independent of stepping state.
	Common::Array<const BreakpointStore::Breakpoint *> hits;
	if (_breakpoints.hitTest(scriptHandle, offset, hits)) {
		Common::Array<Common::String> hitIds;
		for (uint32 i = 0; i < hits.size(); i++)
			if (checkBreakpointCondition(threadId, hits[i]->condition))
				hitIds.push_back(hits[i]->id);
		if (!hitIds.empty() && _debuggerEnabled) {
			_step.clear();
			doPause(threadId, "other", &hitIds, nullptr);
			return;
		}
	}

	if (!isStatement)
		return;

	if (_step.armed() &&
	    _step.shouldPause(StepPoint(threadId, token, scriptHandle, line, offset))) {
		_step.clear();
		doPause(threadId, "other", nullptr, nullptr);
		return;
	}

	if (_pauseRequested && _debuggerEnabled) {
		doPause(threadId, "other", nullptr, nullptr);
		return;
	}
}

bool Session::checkBreakpointCondition(uint32 threadId, const Common::String &condition) {
	if (condition.empty())
		return true;
	// An unevaluable condition pauses (fail loud, not silent): a typo'd
	// condition that never fires is the worse failure mode.
	RemoteObjectTable scratch;
	DebugValue result;
	if (!_agent || !_agent->evaluate(threadId, 0, condition, scratch, result))
		return true;
	return result.truthy();
}

void Session::reportException(uint32 threadId, int scriptHandle, uint32 offset,
                              const Common::String &description) {
	int line = _registry.lineForOffset(scriptHandle, offset);
	if (_runtimeEnabled) {
		Common::JSONObject details;
		details["exceptionId"] = new Common::JSONValue((long long int)_eventSerial);
		details["text"] = new Common::JSONValue(description);
		details["lineNumber"] = new Common::JSONValue((long long int)(line < 0 ? 0 : line));
		details["columnNumber"] = new Common::JSONValue((long long int)0);
		if (_registry.validHandle(scriptHandle))
			details["scriptId"] = new Common::JSONValue(_registry.scriptIdFor(scriptHandle));
		details["executionContextId"] = new Common::JSONValue((long long int)1);
		Common::JSONObject params;
		params["timestamp"] = new Common::JSONValue((long long int)_eventSerial++);
		params["exceptionDetails"] = new Common::JSONValue(details);
		queueEvent("Runtime.exceptionThrown", new Common::JSONValue(params));
	}
	if (_debuggerEnabled && !_skipAllPauses && !_paused && _pauseOnExceptions != "none") {
		Common::JSONObject data;
		data["description"] = new Common::JSONValue(description);
		data["uncaught"] = new Common::JSONValue(true);
		doPause(threadId, "exception", nullptr, new Common::JSONValue(data));
	}
}

void Session::consoleMessage(const Common::String &level, const Common::String &text) {
	if (!_runtimeEnabled)
		return;
	Common::JSONArray args;
	Common::JSONObject arg;
	arg["type"] = new Common::JSONValue("string");
	arg["value"] = new Common::JSONValue(text);
	args.push_back(new Common::JSONValue(arg));
	Common::JSONObject params;
	params["type"] = new Common::JSONValue(level);
	params["args"] = new Common::JSONValue(args);
	params["executionContextId"] = new Common::JSONValue((long long int)1);
	params["timestamp"] = new Common::JSONValue((long long int)_eventSerial++);
	queueEvent("Runtime.consoleAPICalled", new Common::JSONValue(params));
}

void Session::variableAccessHook(uint32 threadId, const Common::String &name,
                                 bool isWrite, const DebugValue &value) {
	if (!_watchArmed || _paused || _skipAllPauses || !_debuggerEnabled)
		return;
	for (uint32 i = 0; i < _watchpoints.size(); i++) {
		const Watchpoint &wp = _watchpoints[i];
		if (wp.name != name)
			continue;
		if ((isWrite && !wp.onWrite) || (!isWrite && !wp.onRead))
			continue;
		Common::JSONObject data;
		data["gameScriptReason"] = new Common::JSONValue("watchpoint");
		data["watchpointId"] = new Common::JSONValue(wp.id);
		data["variable"] = new Common::JSONValue(name);
		data["access"] = new Common::JSONValue(isWrite ? "write" : "read");
		data["value"] = _objects.remoteObjectJSON(value);
		doPause(threadId, "other", nullptr, new Common::JSONValue(data));
		return;
	}
}

// --- pause machinery ---

Common::JSONValue *Session::locationJSON(int scriptHandle, int line) const {
	Common::JSONObject loc;
	loc["scriptId"] = new Common::JSONValue(_registry.scriptIdFor(scriptHandle));
	loc["lineNumber"] = new Common::JSONValue((long long int)line);
	loc["columnNumber"] = new Common::JSONValue((long long int)0);
	return new Common::JSONValue(loc);
}

Common::JSONValue *Session::buildCallFramesJSON() {
	Common::JSONArray frames;
	for (uint32 i = 0; i < _pausedFrames.size(); i++) {
		const CallFrameInfo &info = _pausedFrames[i];
		Common::JSONObject frame;
		frame["callFrameId"] = new Common::JSONValue(
			Common::String::format("frame:%u:%u", _objects.generation(), i));
		frame["functionName"] = new Common::JSONValue(info.functionName);
		int line = _registry.lineForOffset(info.scriptHandle, info.offset);
		frame["location"] = locationJSON(info.scriptHandle, line < 0 ? 0 : line);
		// Deprecated in tip-of-tree but still consumed by shipping clients.
		frame["url"] = new Common::JSONValue(_registry.url(info.scriptHandle));

		Common::JSONArray scopeChain;
		for (uint32 s = 0; s < info.scopes.size(); s++) {
			Common::JSONObject scopeObj;
			scopeObj["type"] = new Common::JSONValue("object");
			scopeObj["className"] = new Common::JSONValue("Object");
			scopeObj["description"] = new Common::JSONValue(info.scopes[s].name);
			scopeObj["objectId"] = new Common::JSONValue(
				Common::String::format("scope:%u:%u:%u", _objects.generation(), i, s));
			Common::JSONObject scope;
			scope["type"] = new Common::JSONValue(info.scopes[s].type);
			scope["name"] = new Common::JSONValue(info.scopes[s].name);
			scope["object"] = new Common::JSONValue(scopeObj);
			scopeChain.push_back(new Common::JSONValue(scope));
		}
		frame["scopeChain"] = new Common::JSONValue(scopeChain);

		Common::JSONObject thisObj;
		thisObj["type"] = new Common::JSONValue("undefined");
		frame["this"] = new Common::JSONValue(thisObj);
		frames.push_back(new Common::JSONValue(frame));
	}
	return new Common::JSONValue(frames);
}

void Session::doPause(uint32 threadId, const Common::String &reason,
                      const Common::Array<Common::String> *hitBreakpointIds,
                      Common::JSONValue *data) {
	if (!_debuggerEnabled) {
		delete data;
		return;
	}
	_paused = true;
	_pausedThread = threadId;
	_pauseRequested = false;

	_pausedFrames.clear();
	_scopeCache.clear();
	if (_agent)
		_agent->buildCallFrames(threadId, _pausedFrames);

	Common::JSONObject params;
	params["callFrames"] = buildCallFramesJSON();
	params["reason"] = new Common::JSONValue(reason);
	if (data)
		params["data"] = data;
	if (hitBreakpointIds && !hitBreakpointIds->empty()) {
		Common::JSONArray ids;
		for (uint32 i = 0; i < hitBreakpointIds->size(); i++)
			ids.push_back(new Common::JSONValue((*hitBreakpointIds)[i]));
		params["hitBreakpoints"] = new Common::JSONValue(ids);
	}
	queueEvent("Debugger.paused", new Common::JSONValue(params));

	// Nested message pump: the VM thread blocks here but the protocol
	// keeps being serviced (evaluateOnCallFrame, getProperties, ...).
	while (_paused) {
		if (!_pump || !_pump->pumpWhilePaused()) {
			// Transport gone: abandon the pause so the game stays alive.
			resumeExecution(false);
			break;
		}
	}
	updateArmed();
}

void Session::resumeExecution(bool emitResumed) {
	// Every objectId (scopes, evaluation results, call frame ids) is
	// scoped to the pause; a stale id must not alias fresh state.
	_objects.reset();
	_scopeCache.clear();
	_pausedFrames.clear();
	_paused = false;
	if (emitResumed && _debuggerEnabled)
		queueEvent("Debugger.resumed", new Common::JSONValue(Common::JSONObject()));
}

void Session::onDisconnect() {
	// Node's behaviour on client detach: resume and discard the
	// session's breakpoints; a re-attaching js-debug re-sends everything.
	if (_paused)
		resumeExecution(false);
	_step.clear();
	_pauseRequested = false;
	_waitingForDebugger = false;
	_runIfWaitingReceived = true;
	_runtimeEnabled = false;
	_debuggerEnabled = false;
	_pauseOnExceptions = "none";
	_skipAllPauses = false;
	_watchpoints.clear();
	_breakpoints.clear();
	updateArmed();
}

// --- dispatch ---

void Session::onMessage(const Common::String &json) {
	Command cmd;
	int errorCode = 0;
	if (!cmd.parse(json, errorCode)) {
		// Answer even the unanswerable (ledger #3): for messages without
		// a recoverable id, V8 replies with "id": null.
		if (errorCode == kErrParse) {
			queueMessage("{\"id\":null,\"error\":{\"code\":-32700,\"message\":\"Message must be a valid JSON\"}}");
		} else {
			queueMessage("{\"id\":null,\"error\":{\"code\":-32600,\"message\":\"Message must have integer 'id' and string 'method'\"}}");
		}
		return;
	}
	dispatch(cmd);
}

void Session::dispatch(const Command &cmd) {
	const Common::String &m = cmd.method();

	// --- Runtime domain ---
	if (m == "Runtime.enable") {
		handleRuntimeEnable(cmd);
	} else if (m == "Runtime.disable") {
		_runtimeEnabled = false;
		queueMessage(buildResult(cmd.id(), nullptr));
	} else if (m == "Runtime.runIfWaitingForDebugger") {
		_runIfWaitingReceived = true;
		queueMessage(buildResult(cmd.id(), nullptr));
	} else if (m == "Runtime.evaluate") {
		handleEvaluate(cmd, false);
	} else if (m == "Runtime.getProperties") {
		handleGetProperties(cmd);
	} else if (m == "Runtime.releaseObject") {
		Common::String objectId;
		cmd.getString("objectId", objectId);
		_objects.releaseObject(objectId);
		queueMessage(buildResult(cmd.id(), nullptr));
	} else if (m == "Runtime.releaseObjectGroup") {
		Common::String group;
		cmd.getString("objectGroup", group);
		_objects.releaseGroup(group);
		queueMessage(buildResult(cmd.id(), nullptr));
	} else if (m == "Runtime.discardConsoleEntries" || m == "Runtime.setAsyncCallStackDepth" ||
	           m == "Runtime.compileScript" || m == "Runtime.setMaxCallStackSizeToCapture") {
		queueMessage(buildResult(cmd.id(), nullptr));

	// --- Debugger domain ---
	} else if (m == "Debugger.enable") {
		handleDebuggerEnable(cmd);
	} else if (m == "Debugger.disable") {
		_debuggerEnabled = false;
		if (_paused)
			resumeExecution(false);
		_step.clear();
		_pauseRequested = false;
		updateArmed();
		queueMessage(buildResult(cmd.id(), nullptr));
	} else if (m == "Debugger.setBreakpointsActive") {
		bool active = true;
		cmd.getBool("active", active);
		_breakpoints.setActive(active);
		updateArmed();
		queueMessage(buildResult(cmd.id(), nullptr));
	} else if (m == "Debugger.setSkipAllPauses") {
		bool skip = false;
		cmd.getBool("skip", skip);
		_skipAllPauses = skip;
		queueMessage(buildResult(cmd.id(), nullptr));
	} else if (m == "Debugger.setBreakpointByUrl") {
		handleSetBreakpointByUrl(cmd);
	} else if (m == "Debugger.setBreakpoint") {
		handleSetBreakpoint(cmd);
	} else if (m == "Debugger.removeBreakpoint") {
		handleRemoveBreakpoint(cmd);
	} else if (m == "Debugger.getPossibleBreakpoints") {
		handleGetPossibleBreakpoints(cmd);
	} else if (m == "Debugger.getScriptSource") {
		handleGetScriptSource(cmd);
	} else if (m == "Debugger.pause") {
		_pauseRequested = true;
		updateArmed();
		queueMessage(buildResult(cmd.id(), nullptr));
	} else if (m == "Debugger.resume") {
		queueMessage(buildResult(cmd.id(), nullptr));
		if (_paused)
			resumeExecution(true);
	} else if (m == "Debugger.stepInto") {
		handleStep(cmd, kStepInto);
	} else if (m == "Debugger.stepOver") {
		handleStep(cmd, kStepOver);
	} else if (m == "Debugger.stepOut") {
		handleStep(cmd, kStepOut);
	} else if (m == "Debugger.evaluateOnCallFrame") {
		handleEvaluate(cmd, true);
	} else if (m == "Debugger.setPauseOnExceptions") {
		Common::String state;
		if (cmd.getString("state", state) &&
		    (state == "none" || state == "caught" || state == "uncaught" || state == "all")) {
			_pauseOnExceptions = state;
			queueMessage(buildResult(cmd.id(), nullptr));
		} else {
			queueMessage(buildError(cmd.id(), kErrInvalidParams, "Unknown state"));
		}
	} else if (m == "Debugger.setAsyncCallStackDepth" || m == "Debugger.setBlackboxPatterns" ||
	           m == "Debugger.setBlackboxedRanges" || m == "Debugger.setBlackboxExecutionContexts") {
		queueMessage(buildResult(cmd.id(), nullptr));

	// --- stubs tolerated for client compatibility ---
	} else if (m == "Profiler.enable" || m == "Profiler.disable" ||
	           m == "HeapProfiler.enable" || m == "HeapProfiler.disable" ||
	           m == "Console.enable" || m == "Console.disable") {
		queueMessage(buildResult(cmd.id(), nullptr));

	// --- GameScript vendor domain (watchpoints; CDP has no data
	//     breakpoints for JS, but game VMs want them — SCI's bpw etc.) ---
	} else if (m == "GameScript.setWatchpoint") {
		handleSetWatchpoint(cmd);
	} else if (m == "GameScript.removeWatchpoint") {
		handleRemoveWatchpoint(cmd);
	} else if (m == "GameScript.listThreads") {
		handleListThreads(cmd);

	} else {
		// Unknown method: answer, or clients hang/loop (ledger #3).
		queueMessage(buildError(cmd.id(), kErrMethodNotFound,
		                        Common::String::format("'%s' wasn't found", m.c_str())));
	}
}

void Session::handleRuntimeEnable(const Command &cmd) {
	_runtimeEnabled = true;
	queueMessage(buildResult(cmd.id(), nullptr));
	// One execution context, id 1: js-debug's child-attach probe
	// hardcodes contextId 1, and scriptParsed must agree (ledger #4).
	Common::JSONObject context;
	context["id"] = new Common::JSONValue((long long int)1);
	context["origin"] = new Common::JSONValue("");
	Common::String name = "ScummVM";
	if (_agent)
		name += " " + _agent->engineId();
	context["name"] = new Common::JSONValue(name);
	Common::JSONObject auxData;
	auxData["isDefault"] = new Common::JSONValue(true);
	context["auxData"] = new Common::JSONValue(auxData);
	Common::JSONObject params;
	params["context"] = new Common::JSONValue(context);
	queueEvent("Runtime.executionContextCreated", new Common::JSONValue(params));
}

void Session::handleDebuggerEnable(const Command &cmd) {
	_debuggerEnabled = true;
	Common::JSONObject result;
	Common::String debuggerId = "scummvm-inspector";
	if (_agent)
		debuggerId = Common::String::format("scummvm-%s", _agent->engineId().c_str());
	result["debuggerId"] = new Common::JSONValue(debuggerId);
	queueMessage(buildResult(cmd.id(), new Common::JSONValue(result)));
	// Replay scriptParsed for everything already registered — clients
	// build their script tables from this (V8 does the same).
	for (int h = 0; h < _registry.scriptCount(); h++)
		queueEvent("Debugger.scriptParsed", _registry.scriptParsedParams(h, 1));
	updateArmed();
}

void Session::handleSetBreakpointByUrl(const Command &cmd) {
	int64 line = -1;
	if (!cmd.getInt("lineNumber", line) || line < 0) {
		queueMessage(buildError(cmd.id(), kErrInvalidParams, "Positive lineNumber expected"));
		return;
	}
	Common::String url, urlRegex, condition;
	cmd.getString("url", url);
	cmd.getString("urlRegex", urlRegex);
	cmd.getString("condition", condition);
	int64 column = 0;
	cmd.getInt("columnNumber", column);

	Common::String error;
	const BreakpointStore::Breakpoint *bp = _breakpoints.setByUrl(
		url, urlRegex, (int)line, (int)column, condition, _registry, error);
	if (!bp) {
		queueMessage(buildError(cmd.id(), kErrServer, error));
		return;
	}
	Common::JSONObject result;
	result["breakpointId"] = new Common::JSONValue(bp->id);
	Common::JSONArray locations;
	for (uint32 i = 0; i < bp->locations.size(); i++)
		locations.push_back(locationJSON(bp->locations[i].scriptHandle, bp->locations[i].line));
	result["locations"] = new Common::JSONValue(locations);
	queueMessage(buildResult(cmd.id(), new Common::JSONValue(result)));
	updateArmed();
}

void Session::handleSetBreakpoint(const Command &cmd) {
	// Debugger.setBreakpoint pins one script by id. Implemented on top of
	// the url store so re-parsed instances of the same resource rebind.
	if (!cmd.params() || !cmd.params()->contains("location") ||
	    !(*cmd.params())["location"]->isObject()) {
		queueMessage(buildError(cmd.id(), kErrInvalidParams, "Location expected"));
		return;
	}
	const Common::JSONObject &loc = (*cmd.params())["location"]->asObject();
	if (!loc.contains("scriptId") || !loc["scriptId"]->isString() ||
	    !loc.contains("lineNumber") || !loc["lineNumber"]->isIntegerNumber()) {
		queueMessage(buildError(cmd.id(), kErrInvalidParams, "Malformed location"));
		return;
	}
	int handle = _registry.handleForScriptId(loc["scriptId"]->asString());
	if (handle < 0) {
		queueMessage(buildError(cmd.id(), kErrServer, "No script for id"));
		return;
	}
	Common::String condition;
	cmd.getString("condition", condition);
	Common::String error;
	const BreakpointStore::Breakpoint *bp = _breakpoints.setByUrl(
		_registry.url(handle), "", (int)loc["lineNumber"]->asIntegerNumber(), 0,
		condition, _registry, error);
	if (!bp) {
		queueMessage(buildError(cmd.id(), kErrServer, error));
		return;
	}
	Common::JSONObject result;
	result["breakpointId"] = new Common::JSONValue(bp->id);
	if (!bp->locations.empty())
		result["actualLocation"] = locationJSON(bp->locations[0].scriptHandle,
		                                        bp->locations[0].line);
	queueMessage(buildResult(cmd.id(), new Common::JSONValue(result)));
	updateArmed();
}

void Session::handleRemoveBreakpoint(const Command &cmd) {
	Common::String id;
	cmd.getString("breakpointId", id);
	_breakpoints.remove(id);
	updateArmed();
	// Removing an unknown id is not an error (idempotent removal keeps
	// re-attaching clients happy).
	queueMessage(buildResult(cmd.id(), nullptr));
}

void Session::handleGetPossibleBreakpoints(const Command &cmd) {
	if (!cmd.params() || !cmd.params()->contains("start") ||
	    !(*cmd.params())["start"]->isObject()) {
		queueMessage(buildError(cmd.id(), kErrInvalidParams, "start expected"));
		return;
	}
	const Common::JSONObject &start = (*cmd.params())["start"]->asObject();
	if (!start.contains("scriptId") || !start["scriptId"]->isString()) {
		queueMessage(buildError(cmd.id(), kErrInvalidParams, "start.scriptId expected"));
		return;
	}
	int handle = _registry.handleForScriptId(start["scriptId"]->asString());
	if (handle < 0) {
		queueMessage(buildError(cmd.id(), kErrServer, "No script for id"));
		return;
	}
	int64 startLine = 0;
	if (start.contains("lineNumber") && start["lineNumber"]->isIntegerNumber())
		startLine = start["lineNumber"]->asIntegerNumber();
	int64 endLine = _registry.lineCount(handle);
	if (cmd.params()->contains("end") && (*cmd.params())["end"]->isObject()) {
		const Common::JSONObject &end = (*cmd.params())["end"]->asObject();
		if (end.contains("lineNumber") && end["lineNumber"]->isIntegerNumber())
			endLine = end["lineNumber"]->asIntegerNumber();
	}
	Common::JSONArray locations;
	for (int64 line = startLine; line < endLine && line < (int64)_registry.lineCount(handle); line++) {
		if (!_registry.isStatementLine(handle, (int)line))
			continue;
		locations.push_back(locationJSON(handle, (int)line));
	}
	Common::JSONObject result;
	result["locations"] = new Common::JSONValue(locations);
	queueMessage(buildResult(cmd.id(), new Common::JSONValue(result)));
}

void Session::handleGetScriptSource(const Command &cmd) {
	Common::String scriptId;
	cmd.getString("scriptId", scriptId);
	int handle = _registry.handleForScriptId(scriptId);
	if (handle < 0) {
		queueMessage(buildError(cmd.id(), kErrServer, "No script for id"));
		return;
	}
	Common::JSONObject result;
	result["scriptSource"] = new Common::JSONValue(_registry.sourceText(handle));
	queueMessage(buildResult(cmd.id(), new Common::JSONValue(result)));
}

void Session::handleStep(const Command &cmd, StepMode mode) {
	if (!_paused) {
		queueMessage(buildError(cmd.id(), kErrServer,
		                        "Can only perform operation while paused."));
		return;
	}
	// Ack first, then Debugger.resumed, then (later, from the hook) the
	// Debugger.paused that ends the step — clients key per-command state
	// machines on this order (ledger #26).
	queueMessage(buildResult(cmd.id(), nullptr));

	FrameTracker &tracker = trackerFor(_pausedThread);
	StepPoint at;
	at.threadId = _pausedThread;
	at.frameToken = tracker.currentToken();
	if (!_pausedFrames.empty()) {
		at.scriptHandle = _pausedFrames[0].scriptHandle;
		at.offset = _pausedFrames[0].offset;
		at.statementLine = _registry.statementForOffset(at.scriptHandle, at.offset);
	}
	Common::Array<uint64> chain;
	tracker.callerChain(chain);
	_step.arm(mode, at, chain);
	resumeExecution(true);
	updateArmed();
}

bool Session::parseFrameId(const Common::String &frameId, int &frameIndex) const {
	uint32 gen = 0;
	uint32 idx = 0;
	if (sscanf(frameId.c_str(), "frame:%u:%u", &gen, &idx) != 2)
		return false;
	if (gen != _objects.generation() || !_paused || idx >= _pausedFrames.size())
		return false;
	frameIndex = (int)idx;
	return true;
}

void Session::handleEvaluate(const Command &cmd, bool onCallFrame) {
	Common::String expression;
	cmd.getString("expression", expression);

	int frameIndex = -1;
	if (onCallFrame) {
		Common::String frameId;
		cmd.getString("callFrameId", frameId);
		if (!parseFrameId(frameId, frameIndex)) {
			queueMessage(buildError(cmd.id(), kErrServer, "Can only evaluate on valid call frame"));
			return;
		}
	}

	Common::JSONObject result;

	// js-debug periodically probes `typeof process === 'undefined' ...`
	// (telemetry and child-process attach, retried up to 200 times). The
	// documented escape hatch is answering with a plain string — Deno
	// returns 'deno'; anything JS-shaped that is not an error stops the
	// loop (ledger #14, vscode-js-debug#981).
	if (expression.contains("typeof process") || expression.contains("process.pid")) {
		Common::JSONObject str;
		str["type"] = new Common::JSONValue("string");
		str["value"] = new Common::JSONValue("process not defined");
		result["result"] = new Common::JSONValue(str);
		queueMessage(buildResult(cmd.id(), new Common::JSONValue(result)));
		return;
	}

	Common::String objectGroup;
	cmd.getString("objectGroup", objectGroup);

	DebugValue value;
	bool evaluated = false;
	if (_agent) {
		uint32 threadId = _paused ? _pausedThread : 0;
		evaluated = _agent->evaluate(threadId, frameIndex, expression, _objects, value);
	}
	// Unevaluable expressions answer undefined, never a protocol error —
	// erroring here has caused client request loops (ledger #14).
	result["result"] = _objects.remoteObjectJSON(evaluated ? value : DebugValue::undefined());
	queueMessage(buildResult(cmd.id(), new Common::JSONValue(result)));
}

void Session::handleGetProperties(const Command &cmd) {
	Common::String objectId;
	cmd.getString("objectId", objectId);

	// Scope handles materialize lazily: the client only pays for scopes
	// it actually expands in the variables pane.
	uint32 gen = 0, frame = 0, scope = 0;
	if (sscanf(objectId.c_str(), "scope:%u:%u:%u", &gen, &frame, &scope) == 3) {
		if (gen != _objects.generation() || !_paused || frame >= _pausedFrames.size() ||
		    scope >= _pausedFrames[frame].scopes.size()) {
			queueMessage(buildError(cmd.id(), kErrServer, "Could not find object with given id"));
			return;
		}
		uint32 key = (frame << 16) | scope;
		int objRef;
		ScopeCache::iterator it = _scopeCache.find(key);
		if (it != _scopeCache.end()) {
			objRef = it->_value;
		} else {
			objRef = _objects.createObject("Object", _pausedFrames[frame].scopes[scope].name);
			if (_agent)
				_agent->buildScopeObject(_pausedThread, (int)frame, (int)scope, _objects, objRef);
			_scopeCache[key] = objRef;
		}
		Common::JSONValue *props = _objects.propertiesJSON(objRef);
		Common::JSONObject result;
		result["result"] = props ? props : new Common::JSONValue(Common::JSONArray());
		queueMessage(buildResult(cmd.id(), new Common::JSONValue(result)));
		return;
	}

	int ref = -1;
	if (_objects.resolveObjectId(objectId, ref)) {
		Common::JSONValue *props = _objects.propertiesJSON(ref);
		Common::JSONObject result;
		result["result"] = props ? props : new Common::JSONValue(Common::JSONArray());
		queueMessage(buildResult(cmd.id(), new Common::JSONValue(result)));
		return;
	}
	queueMessage(buildError(cmd.id(), kErrServer, "Could not find object with given id"));
}

void Session::handleSetWatchpoint(const Command &cmd) {
	Common::String name, accessType;
	if (!cmd.getString("variable", name) || name.empty()) {
		queueMessage(buildError(cmd.id(), kErrInvalidParams, "variable expected"));
		return;
	}
	if (!cmd.getString("accessType", accessType))
		accessType = "write";
	if (accessType != "read" && accessType != "write" && accessType != "all") {
		queueMessage(buildError(cmd.id(), kErrInvalidParams, "accessType must be read|write|all"));
		return;
	}
	Watchpoint wp;
	wp.id = Common::String::format("wp-%d", _nextWatchpointId++);
	wp.name = name;
	wp.onRead = (accessType == "read" || accessType == "all");
	wp.onWrite = (accessType == "write" || accessType == "all");
	_watchpoints.push_back(wp);
	updateArmed();
	Common::JSONObject result;
	result["watchpointId"] = new Common::JSONValue(wp.id);
	queueMessage(buildResult(cmd.id(), new Common::JSONValue(result)));
}

void Session::handleRemoveWatchpoint(const Command &cmd) {
	Common::String id;
	cmd.getString("watchpointId", id);
	for (uint32 i = 0; i < _watchpoints.size(); i++) {
		if (_watchpoints[i].id == id) {
			_watchpoints.remove_at(i);
			break;
		}
	}
	updateArmed();
	queueMessage(buildResult(cmd.id(), nullptr));
}

void Session::handleListThreads(const Command &cmd) {
	Common::JSONArray threads;
	for (TrackerMap::const_iterator it = _trackers.begin(); it != _trackers.end(); ++it) {
		Common::JSONObject t;
		t["id"] = new Common::JSONValue((long long int)it->_key);
		if (_agent)
			t["description"] = new Common::JSONValue(_agent->describeThread(it->_key));
		threads.push_back(new Common::JSONValue(t));
	}
	Common::JSONObject result;
	result["threads"] = new Common::JSONValue(threads);
	queueMessage(buildResult(cmd.id(), new Common::JSONValue(result)));
}

} // End of namespace Inspector
