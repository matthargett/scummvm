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

#ifndef COMMON_INSPECTOR_STEPPING_H
#define COMMON_INSPECTOR_STEPPING_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/hashmap.h"

namespace Inspector {

/**
 * Stepping semantics for multiplexed game-script VMs, distilled from gdb's
 * contract, Hermes' implementation and mobdebug's documented failure of
 * naive call/return counting (see DESIGN.md, "Stepping semantics"):
 *
 *  - steps land on statement starts only;
 *  - step-over/step-out key on *frame identity*, never on depth counters
 *    (recursion and native re-entry break counters);
 *  - a step in one script thread must not stop in another;
 *  - step-over at a return degrades to step-out;
 *  - re-reaching the same statement through a loop back-edge stops.
 */

/** One statement-start event from the VM, as seen by the step logic. */
struct StepPoint {
	uint32 threadId;      ///< engine-defined script-thread identity
	uint64 frameToken;    ///< stable per-activation identity (FrameTracker)
	int scriptHandle;
	int statementLine;    ///< listing line of the statement start
	uint32 offset;        ///< bytecode offset

	StepPoint() : threadId(0), frameToken(0), scriptHandle(-1), statementLine(-1), offset(0) {}
	StepPoint(uint32 thread, uint64 frame, int script, int line, uint32 ofs) :
		threadId(thread), frameToken(frame), scriptHandle(script),
		statementLine(line), offset(ofs) {}
};

enum StepMode {
	kStepNone,
	kStepInto,
	kStepOver,
	kStepOut
};

class StepController {
public:
	StepController();

	/**
	 * Arm a step from the current pause location.
	 * @param at          where execution is currently paused
	 * @param callerChain frame tokens of every *caller* of the current
	 *                    frame (any order; membership is what matters)
	 */
	void arm(StepMode mode, const StepPoint &at, const Common::Array<uint64> &callerChain);

	void clear();
	bool armed() const { return _mode != kStepNone; }
	StepMode mode() const { return _mode; }

	/**
	 * Decide whether this statement-start event completes the step.
	 * Events from other threads never complete a step (breakpoints are
	 * checked independently of stepping and still hit anywhere).
	 */
	bool shouldPause(const StepPoint &pt) const;

private:
	StepMode _mode;
	StepPoint _armedAt;
	Common::Array<uint64> _callerChain;

	bool inCallerChain(uint64 token) const;
};

/**
 * Derives stable frame-identity tokens from the call depth engines can
 * report cheaply at every instruction. Growing depth mints fresh tokens
 * (a recursive call therefore gets a token distinct from its caller's,
 * even though the code address is identical); shrinking depth pops back
 * to the caller's original token.
 *
 * Known limitation (documented): a return immediately followed by a call
 * between two hook events (tail-call-like chaining) keeps the same depth
 * and is treated as the same activation. Engines with real push/pop hooks
 * (Director's pushContextHook, Wintermute's ScScript) can drive
 * onCall/onReturn directly instead of feed().
 */
class FrameTracker {
public:
	FrameTracker();

	/** Report the current call depth (>= 1); returns the current frame token. */
	uint64 feed(uint32 depth);

	/** Explicit push/pop for engines with real call/return hooks. */
	uint64 onCall();
	uint64 onReturn();

	uint64 currentToken() const;
	uint32 depth() const { return _stack.size(); }

	/** Tokens of all *callers* of the current frame (excludes current). */
	void callerChain(Common::Array<uint64> &out) const;

	void reset();

private:
	Common::Array<uint64> _stack;
	uint64 _nextSerial;
};

} // End of namespace Inspector

#endif
