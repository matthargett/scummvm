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

#include "common/inspector/sourcemap.h"
#include "common/base64.h"
#include "common/formats/json.h"

namespace Inspector {

static const char *kBase64Chars =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

SourceMapBuilder::SourceMapBuilder(const Common::String &file) : _file(file) {
}

int SourceMapBuilder::addSource(const Common::String &name, const Common::String &content) {
	_sources.push_back(name);
	_sourcesContent.push_back(content);
	return (int)_sources.size() - 1;
}

bool SourceMapBuilder::addMapping(int genLine, int genCol, int sourceIndex, int srcLine, int srcCol) {
	if (genLine < 0 || genCol < 0 || srcLine < 0 || srcCol < 0)
		return false;
	if (sourceIndex < 0 || (uint32)sourceIndex >= _sources.size())
		return false;
	if (!_segments.empty()) {
		const Segment &prev = _segments[_segments.size() - 1];
		if (genLine < prev.genLine || (genLine == prev.genLine && genCol < prev.genCol))
			return false; // mappings must be generated in order
	}
	Segment s;
	s.genLine = genLine;
	s.genCol = genCol;
	s.sourceIndex = sourceIndex;
	s.srcLine = srcLine;
	s.srcCol = srcCol;
	_segments.push_back(s);
	return true;
}

bool SourceMapBuilder::addUnmappedRange(int genLine, int genCol) {
	if (genLine < 0 || genCol < 0)
		return false;
	if (!_segments.empty()) {
		const Segment &prev = _segments[_segments.size() - 1];
		if (genLine < prev.genLine || (genLine == prev.genLine && genCol < prev.genCol))
			return false;
	}
	Segment s;
	s.genLine = genLine;
	s.genCol = genCol;
	s.sourceIndex = -1;
	s.srcLine = 0;
	s.srcCol = 0;
	_segments.push_back(s);
	return true;
}

bool SourceMapBuilder::encodeVLQ(int32 v, Common::String &out) {
	// The sign lives in bit 0 of the first digit, so the value range is
	// +/- (2^31 - 1): INT32_MIN would alias the encoding of "-0" and
	// break the round-trip (nodejs/node#31490).
	if (v == (int32)0x80000000)
		return false;
	// All shifting is done on unsigned values; using arithmetic shifts on
	// negative numbers is the other classic VLQ bug.
	uint32 x = (v < 0) ? (((uint32)(-v) << 1) | 1) : ((uint32)v << 1);
	do {
		uint32 digit = x & 31;
		x >>= 5;
		if (x)
			digit |= 32; // continuation
		out += kBase64Chars[digit];
	} while (x);
	return true;
}

bool SourceMapBuilder::decodeVLQ(const Common::String &data, uint32 &pos, int32 &out) {
	uint64 value = 0;
	uint32 shift = 0;
	bool more = true;
	while (more) {
		if (pos >= data.size())
			return false; // dangling continuation
		char c = data[pos++];
		const char *p = strchr(kBase64Chars, c);
		if (!p || !c)
			return false;
		uint32 digit = (uint32)(p - kBase64Chars);
		more = (digit & 32) != 0;
		value |= (uint64)(digit & 31) << shift;
		shift += 5;
		// ECMA-426: if value >= 2^32, throw.
		if (value >> 32)
			return false;
	}
	bool negative = (value & 1) != 0;
	value >>= 1;
	// ECMA-426: for the signed result, if value >= 2^31, throw.
	if (value >= 0x80000000ULL)
		return false;
	out = negative ? -(int32)value : (int32)value;
	return true;
}

Common::String SourceMapBuilder::build() const {
	// Assemble "mappings": segments grouped by generated line (";"),
	// separated by "," within a line. The generated-column delta resets
	// on every line; source-index/line/column deltas run across the map.
	Common::String mappings;
	int curLine = 0;
	int prevGenCol = 0;
	int prevSource = 0, prevSrcLine = 0, prevSrcCol = 0;
	bool firstOnLine = true;

	for (uint32 i = 0; i < _segments.size(); i++) {
		const Segment &s = _segments[i];
		while (curLine < s.genLine) {
			mappings += ';';
			curLine++;
			prevGenCol = 0; // ONLY this field resets per line
			firstOnLine = true;
		}
		if (!firstOnLine)
			mappings += ',';
		firstOnLine = false;

		encodeVLQ(s.genCol - prevGenCol, mappings);
		prevGenCol = s.genCol;
		if (s.sourceIndex >= 0) {
			encodeVLQ(s.sourceIndex - prevSource, mappings);
			encodeVLQ(s.srcLine - prevSrcLine, mappings);
			encodeVLQ(s.srcCol - prevSrcCol, mappings);
			prevSource = s.sourceIndex;
			prevSrcLine = s.srcLine;
			prevSrcCol = s.srcCol;
		}
	}

	Common::JSONObject root;
	root["version"] = new Common::JSONValue((long long int)3);
	root["file"] = new Common::JSONValue(_file);
	Common::JSONArray sources, contents;
	for (uint32 i = 0; i < _sources.size(); i++) {
		sources.push_back(new Common::JSONValue(_sources[i]));
		contents.push_back(new Common::JSONValue(_sourcesContent[i]));
	}
	root["sources"] = new Common::JSONValue(sources);
	root["sourcesContent"] = new Common::JSONValue(contents);
	root["names"] = new Common::JSONValue(Common::JSONArray());
	root["mappings"] = new Common::JSONValue(mappings);

	Common::JSONValue rootValue(root);
	return rootValue.stringify();
}

Common::String SourceMapBuilder::buildDataURL() const {
	Common::String json = build();
	Common::String encoded = Common::b64EncodeData(const_cast<char *>(json.c_str()), json.size());
	return Common::String("data:application/json;charset=utf-8;base64,") + encoded;
}

} // End of namespace Inspector
