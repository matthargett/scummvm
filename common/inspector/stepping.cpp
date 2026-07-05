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

#include "common/inspector/stepping.h"

namespace Inspector {

StepController::StepController() : _mode(kStepNone) {
}

void StepController::arm(StepMode mode, const StepPoint &at,
                         const Common::Array<uint64> &callerChain) {
	_mode = mode;
	_armedAt = at;
	_callerChain = callerChain;
}

void StepController::clear() {
	_mode = kStepNone;
	_callerChain.clear();
}

bool StepController::inCallerChain(uint64 token) const {
	for (uint32 i = 0; i < _callerChain.size(); i++)
		if (_callerChain[i] == token)
			return true;
	return false;
}

bool StepController::shouldPause(const StepPoint &pt) const {
	if (_mode == kStepNone)
		return false;

	// A step is scoped to the script thread that initiated it: other
	// threads' statements execute freely during the step (mobdebug's
	// coroutine guard; DESIGN.md ledger #12).
	if (pt.threadId != _armedAt.threadId)
		return false;

	switch (_mode) {
	case kStepInto:
		// Any statement start in this thread finishes the step: the next
		// statement in the same frame, the first statement of a callee,
		// the statement a caller resumes at after a return, or the same
		// statement re-entered through a loop back-edge (ledger #11).
		return true;

	case kStepOver:
		// Same activation: any statement start (next statement, or the
		// same one re-reached around a loop). A *different* activation of
		// the same code — recursion — has a different token, is not in
		// the caller chain, and is skipped (ledger #9).
		if (pt.frameToken == _armedAt.frameToken)
			return true;
		// Frame returned: step-over degrades to step-out and finishes at
		// the first statement reached in any captured caller (ledger #10).
		return inCallerChain(pt.frameToken);

	case kStepOut:
		return inCallerChain(pt.frameToken);

	default:
		return false;
	}
}

FrameTracker::FrameTracker() : _nextSerial(1) {
}

uint64 FrameTracker::feed(uint32 depth) {
	if (depth < 1)
		depth = 1;
	while (_stack.size() > depth)
		_stack.pop_back();
	while (_stack.size() < depth)
		_stack.push_back(_nextSerial++);
	return _stack.back();
}

uint64 FrameTracker::onCall() {
	_stack.push_back(_nextSerial++);
	return _stack.back();
}

uint64 FrameTracker::onReturn() {
	if (!_stack.empty())
		_stack.pop_back();
	return currentToken();
}

uint64 FrameTracker::currentToken() const {
	if (_stack.empty())
		return 0;
	return _stack[_stack.size() - 1];
}

void FrameTracker::callerChain(Common::Array<uint64> &out) const {
	out.clear();
	if (_stack.size() < 2)
		return;
	for (uint32 i = 0; i + 1 < _stack.size(); i++)
		out.push_back(_stack[i]);
}

void FrameTracker::reset() {
	_stack.clear();
	_nextSerial = 1;
}

} // End of namespace Inspector
