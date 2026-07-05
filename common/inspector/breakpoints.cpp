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

#include "common/inspector/breakpoints.h"
#include "common/inspector/regex.h"

namespace Inspector {

BreakpointStore::BreakpointStore() : _nextId(1), _active(true), _resolvedCount(0) {
}

BreakpointStore::~BreakpointStore() {
	clear();
}

void BreakpointStore::clear() {
	for (uint32 i = 0; i < _breakpoints.size(); i++)
		delete _breakpoints[i];
	_breakpoints.clear();
	_hitMap.clear();
	_resolvedCount = 0;
	_active = true;
}

const BreakpointStore::Breakpoint *BreakpointStore::setByUrl(
		const Common::String &url, const Common::String &urlRegex,
		int line, int column, const Common::String &condition,
		const ScriptRegistry &registry, Common::String &error) {
	if (url.empty() == urlRegex.empty()) {
		error = "Either url or urlRegex must be specified.";
		return nullptr;
	}
	if (line < 0) {
		error = "Positive line number expected.";
		return nullptr;
	}
	if (!urlRegex.empty()) {
		Regex re(urlRegex);
		if (!re.valid()) {
			error = "Invalid urlRegex.";
			return nullptr;
		}
	}
	// V8 refuses a second breakpoint at the same requested location
	// (ledger #7): "Breakpoint at specified location already exists."
	for (uint32 i = 0; i < _breakpoints.size(); i++) {
		const Breakpoint *bp = _breakpoints[i];
		if (bp->url == url && bp->urlRegex == urlRegex &&
		    bp->requestedLine == line && bp->requestedColumn == column) {
			error = "Breakpoint at specified location already exists.";
			return nullptr;
		}
	}

	Breakpoint *bp = new Breakpoint();
	bp->id = Common::String::format("bp-%d", _nextId++);
	bp->url = url;
	bp->urlRegex = urlRegex;
	bp->requestedLine = line;
	bp->requestedColumn = column;
	bp->condition = condition;
	_breakpoints.push_back(bp);
	uint32 bpIndex = _breakpoints.size() - 1;

	// Resolve against everything already parsed. No match is NOT an
	// error: the response carries locations: [] and the breakpoint binds
	// to scripts parsed later (ledger #2).
	for (int h = 0; h < registry.scriptCount(); h++) {
		Location loc;
		if (resolveAgainst(*bp, h, registry, loc))
			recordLocation(bpIndex, loc);
	}
	return bp;
}

bool BreakpointStore::resolveAgainst(Breakpoint &bp, int scriptHandle,
                                     const ScriptRegistry &registry, Location &out) const {
	const Common::String &scriptUrl = registry.url(scriptHandle);
	bool matches;
	if (!bp.url.empty()) {
		matches = (bp.url == scriptUrl);
	} else {
		Regex re(bp.urlRegex);
		matches = re.valid() && re.test(scriptUrl);
	}
	if (!matches)
		return false;
	int line = registry.snapToStatement(scriptHandle, bp.requestedLine);
	if (line < 0)
		return false; // no statement at or after the requested line
	uint32 offset = 0;
	if (!registry.offsetForLine(scriptHandle, line, offset))
		return false;
	out.scriptHandle = scriptHandle;
	out.line = line;
	out.offset = offset;
	return true;
}

void BreakpointStore::recordLocation(uint32 bpIndex, const Location &loc) {
	Breakpoint *bp = _breakpoints[bpIndex];
	// One location per (breakpoint, script, line).
	for (uint32 i = 0; i < bp->locations.size(); i++)
		if (bp->locations[i].scriptHandle == loc.scriptHandle &&
		    bp->locations[i].line == loc.line)
			return;
	bp->locations.push_back(loc);
	_resolvedCount++;
	uint64 key = hitKey(loc.scriptHandle, loc.offset);
	if (!_hitMap.contains(key))
		_hitMap[key] = Common::Array<uint32>();
	_hitMap[key].push_back(bpIndex);
}

bool BreakpointStore::remove(const Common::String &breakpointId) {
	for (uint32 i = 0; i < _breakpoints.size(); i++) {
		if (_breakpoints[i]->id == breakpointId) {
			_resolvedCount -= _breakpoints[i]->locations.size();
			delete _breakpoints[i];
			_breakpoints.remove_at(i);
			// Indices into _breakpoints shifted; rebuild the hit map.
			_hitMap.clear();
			for (uint32 b = 0; b < _breakpoints.size(); b++) {
				for (uint32 l = 0; l < _breakpoints[b]->locations.size(); l++) {
					const Location &loc = _breakpoints[b]->locations[l];
					uint64 key = hitKey(loc.scriptHandle, loc.offset);
					if (!_hitMap.contains(key))
						_hitMap[key] = Common::Array<uint32>();
					_hitMap[key].push_back(b);
				}
			}
			return true;
		}
	}
	return false;
}

void BreakpointStore::bindScript(int scriptHandle, const ScriptRegistry &registry,
                                 Common::Array<Resolution> &newlyResolved) {
	for (uint32 i = 0; i < _breakpoints.size(); i++) {
		Location loc;
		if (resolveAgainst(*_breakpoints[i], scriptHandle, registry, loc)) {
			uint32 before = _breakpoints[i]->locations.size();
			recordLocation(i, loc);
			if (_breakpoints[i]->locations.size() > before) {
				Resolution res;
				res.breakpointId = _breakpoints[i]->id;
				res.location = loc;
				newlyResolved.push_back(res);
			}
		}
	}
}

bool BreakpointStore::hitTest(int scriptHandle, uint32 offset,
                              Common::Array<const Breakpoint *> &hits) const {
	if (!_active || _resolvedCount == 0)
		return false;
	HitMap::const_iterator it = _hitMap.find(hitKey(scriptHandle, offset));
	if (it == _hitMap.end())
		return false;
	for (uint32 i = 0; i < it->_value.size(); i++)
		hits.push_back(_breakpoints[it->_value[i]]);
	return !hits.empty();
}

const BreakpointStore::Breakpoint *BreakpointStore::findById(const Common::String &id) const {
	for (uint32 i = 0; i < _breakpoints.size(); i++)
		if (_breakpoints[i]->id == id)
			return _breakpoints[i];
	return nullptr;
}

} // End of namespace Inspector
