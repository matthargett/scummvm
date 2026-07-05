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

#ifndef COMMON_INSPECTOR_SOURCEMAP_H
#define COMMON_INSPECTOR_SOURCEMAP_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/str.h"

namespace Inspector {

/**
 * Source Map v3 (ECMA-426) generator.
 *
 * Used to map a script's disassembly listing (the "generated" code the
 * CDP client sees) back to the engine's original-language script listing
 * (LISP/Smalltalk-flavoured for SCI/AGI, Lingo for Director, ...), which
 * is embedded via `sourcesContent` so it does not need to exist on disk —
 * the same mechanism Deno uses for TypeScript and emscripten for C.
 *
 * Implementor corner cases encoded here (and in the unit tests):
 *  - VLQ values are limited to 32-bit quantities; the sign bit lives in
 *    bit 0, so INT32_MIN cannot round-trip ("negative zero") and huge
 *    values need unsigned/logical shifts (nodejs/node#31490). Bytecode
 *    offsets used as columns are huge *by design*, so this matters.
 *  - the generated-column delta resets on every new line ONLY; the
 *    source-index/line/column/name deltas run across the whole map.
 */
class SourceMapBuilder {
public:
	/** @param file value of the top-level "file" field (generated file name) */
	explicit SourceMapBuilder(const Common::String &file);

	/**
	 * Register an original source.
	 * @param name    entry for "sources" (a URL or display name)
	 * @param content inline source text for "sourcesContent"
	 * @return source index for addMapping()
	 */
	int addSource(const Common::String &name, const Common::String &content);

	/**
	 * Add one mapping. Mappings must be added in generated order
	 * (ascending line, then ascending column).
	 * @return false if any coordinate is out of the VLQ 32-bit range or
	 *         out of order; the mapping is then dropped.
	 */
	bool addMapping(int genLine, int genCol, int sourceIndex, int srcLine, int srcCol);

	/** Add a 1-field segment: marks generated code with no original source. */
	bool addUnmappedRange(int genLine, int genCol);

	/** Serialize to Source Map v3 JSON. */
	Common::String build() const;

	/** Serialize to a data: URL suitable for Debugger.scriptParsed's sourceMapURL. */
	Common::String buildDataURL() const;

	/**
	 * Base64-VLQ encode one value into @p out.
	 * @return false (nothing appended) when v == INT32_MIN, which cannot
	 *         be represented (its encoding would alias "-0").
	 */
	static bool encodeVLQ(int32 v, Common::String &out);

	/**
	 * Decode one base64-VLQ value from @p data starting at @p pos
	 * (advances @p pos). Returns false on malformed input or on values
	 * outside the signed 32-bit range (ECMA-426 mandates rejection).
	 * Provided for tests and future consumers.
	 */
	static bool decodeVLQ(const Common::String &data, uint32 &pos, int32 &out);

private:
	struct Segment {
		int genLine, genCol;
		int sourceIndex; ///< -1 for unmapped 1-field segments
		int srcLine, srcCol;
	};

	Common::String _file;
	Common::Array<Common::String> _sources;
	Common::Array<Common::String> _sourcesContent;
	Common::Array<Segment> _segments;
};

} // End of namespace Inspector

#endif
