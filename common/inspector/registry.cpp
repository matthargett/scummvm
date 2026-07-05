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

#include "common/inspector/registry.h"
#include "common/inspector/sourcemap.h"

namespace Inspector {

ScriptRegistry::ScriptRegistry() {
}

int ScriptRegistry::addScript(const ScriptListing &listing) {
	// Offsets must be ascending so offset<->line translation can binary
	// search. Equal offsets are tolerated (multi-line pseudo instructions).
	for (uint32 i = 1; i < listing.lines.size(); i++)
		if (listing.lines[i].offset < listing.lines[i - 1].offset)
			return -1;

	Record rec;
	rec.listing = listing;

	if (!listing.originalSource.empty()) {
		// One flat map per script (indexed "sections" maps are poorly
		// supported outside browsers): generated side is the listing,
		// original side the engine-provided source.
		SourceMapBuilder builder(listing.url);
		Common::String name = listing.originalName.empty() ?
			(listing.url + ".src") : listing.originalName;
		int src = builder.addSource(name, listing.originalSource);
		for (uint32 i = 0; i < listing.lines.size(); i++) {
			if (listing.lines[i].originalLine >= 0)
				builder.addMapping((int)i, 0, src, listing.lines[i].originalLine, 0);
			else
				builder.addUnmappedRange((int)i, 0);
		}
		rec.sourceMapURL = builder.buildDataURL();
	}

	_scripts.push_back(rec);
	return (int)_scripts.size() - 1;
}

Common::String ScriptRegistry::scriptIdFor(int handle) const {
	// V8 uses small numeric strings; anything stable and unique works,
	// but clients MUST receive it as a JSON string (ledger #5).
	return Common::String::format("%d", handle + 1);
}

int ScriptRegistry::handleForScriptId(const Common::String &scriptId) const {
	int id = atoi(scriptId.c_str());
	int handle = id - 1;
	if (!validHandle(handle))
		return -1;
	return handle;
}

const Common::String &ScriptRegistry::url(int handle) const {
	static const Common::String kEmpty;
	if (!validHandle(handle))
		return kEmpty;
	return _scripts[handle].listing.url;
}

Common::String ScriptRegistry::sourceText(int handle) const {
	if (!validHandle(handle))
		return Common::String();
	const Common::Array<ListingLine> &lines = _scripts[handle].listing.lines;
	Common::String text;
	for (uint32 i = 0; i < lines.size(); i++) {
		text += lines[i].text;
		text += '\n';
	}
	return text;
}

int ScriptRegistry::lineCount(int handle) const {
	if (!validHandle(handle))
		return 0;
	return (int)_scripts[handle].listing.lines.size();
}

int ScriptRegistry::lineForOffset(int handle, uint32 offset) const {
	if (!validHandle(handle))
		return -1;
	const Common::Array<ListingLine> &lines = _scripts[handle].listing.lines;
	if (lines.empty() || offset < lines[0].offset)
		return -1;
	// Binary search: last line with lineOffset <= offset.
	int lo = 0, hi = (int)lines.size() - 1;
	while (lo < hi) {
		int mid = (lo + hi + 1) / 2;
		if (lines[mid].offset <= offset)
			lo = mid;
		else
			hi = mid - 1;
	}
	return lo;
}

bool ScriptRegistry::offsetForLine(int handle, int line, uint32 &offset) const {
	if (!validHandle(handle))
		return false;
	const Common::Array<ListingLine> &lines = _scripts[handle].listing.lines;
	if (line < 0 || (uint32)line >= lines.size())
		return false;
	offset = lines[line].offset;
	return true;
}

int ScriptRegistry::snapToStatement(int handle, int line) const {
	if (!validHandle(handle))
		return -1;
	const Common::Array<ListingLine> &lines = _scripts[handle].listing.lines;
	if (line < 0)
		line = 0;
	for (uint32 i = (uint32)line; i < lines.size(); i++)
		if (lines[i].isStatement)
			return (int)i;
	return -1;
}

int ScriptRegistry::statementForOffset(int handle, uint32 offset) const {
	int line = lineForOffset(handle, offset);
	if (line < 0)
		return -1;
	const Common::Array<ListingLine> &lines = _scripts[handle].listing.lines;
	for (int i = line; i >= 0; i--)
		if (lines[i].isStatement)
			return i;
	return line;
}

bool ScriptRegistry::isStatementLine(int handle, int line) const {
	if (!validHandle(handle))
		return false;
	const Common::Array<ListingLine> &lines = _scripts[handle].listing.lines;
	if (line < 0 || (uint32)line >= lines.size())
		return false;
	return lines[line].isStatement;
}

Common::String ScriptRegistry::sourceMapURL(int handle) const {
	if (!validHandle(handle))
		return Common::String();
	return _scripts[handle].sourceMapURL;
}

Common::JSONValue *ScriptRegistry::scriptParsedParams(int handle, int executionContextId) const {
	if (!validHandle(handle))
		return nullptr;
	const Record &rec = _scripts[handle];
	const Common::Array<ListingLine> &lines = rec.listing.lines;

	Common::JSONObject params;
	params["scriptId"] = new Common::JSONValue(scriptIdFor(handle));
	params["url"] = new Common::JSONValue(rec.listing.url);
	// Standalone scripts: extents start at 0:0 (nonzero starts mean
	// "embedded in a containing resource" and change coordinate meaning).
	params["startLine"] = new Common::JSONValue((long long int)0);
	params["startColumn"] = new Common::JSONValue((long long int)0);
	long long endLine = (long long)lines.size();
	long long endColumn = 0;
	params["endLine"] = new Common::JSONValue(endLine);
	params["endColumn"] = new Common::JSONValue(endColumn);
	params["executionContextId"] = new Common::JSONValue((long long int)executionContextId);
	params["hash"] = new Common::JSONValue(Common::String::format("insp-%d", handle));
	if (!rec.sourceMapURL.empty())
		params["sourceMapURL"] = new Common::JSONValue(rec.sourceMapURL);
	params["length"] = new Common::JSONValue((long long int)sourceText(handle).size());
	return new Common::JSONValue(params);
}

} // End of namespace Inspector
