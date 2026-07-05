#include <cxxtest/TestSuite.h>

#include "common/inspector/regex.h"

/**
 * Mini-regex used for CDP Debugger.setBreakpointByUrl `urlRegex` matching.
 * The must-pass patterns are the shapes VS Code js-debug really generates
 * (src/common/urlUtils.ts): case-insensitivity via character classes,
 * non-capturing groups, alternation of percent-encoded/literal forms,
 * escaped metacharacters and a ($|\?) suffix (ledger #6).
 */
class InspectorRegexTestSuite : public CxxTest::TestSuite {
	static bool matches(const char *pattern, const char *subject) {
		Inspector::Regex re(pattern);
		return re.test(subject);
	}

public:
	void test_literal_unanchored() {
		TS_ASSERT(matches("abc", "abc"));
		TS_ASSERT(matches("abc", "xxabcxx"));
		TS_ASSERT(!matches("abc", "ab"));
		TS_ASSERT(!matches("abc", "axbxc"));
		TS_ASSERT(matches("", "anything")); // empty pattern matches anywhere
	}

	void test_anchors() {
		TS_ASSERT(matches("^abc$", "abc"));
		TS_ASSERT(!matches("^abc$", "xabc"));
		TS_ASSERT(!matches("^abc$", "abcx"));
		TS_ASSERT(matches("^ab", "abc"));
		TS_ASSERT(matches("bc$", "abc"));
		TS_ASSERT(!matches("^bc", "abc"));
	}

	void test_dot_matches_everything_but_newline() {
		TS_ASSERT(matches("a.c", "abc"));
		TS_ASSERT(matches("a.c", "a/c"));
		TS_ASSERT(!matches("a.c", "a\nc"));
	}

	void test_character_classes() {
		TS_ASSERT(matches("[fF][oO][oO]", "FOO"));
		TS_ASSERT(matches("[fF][oO][oO]", "foo"));
		TS_ASSERT(matches("[fF][oO][oO]", "Foo"));
		TS_ASSERT(!matches("[fF][oO][oO]", "fxo"));
		TS_ASSERT(matches("[a-z0-9_]", "q"));
		TS_ASSERT(matches("[a-z0-9_]", "7"));
		TS_ASSERT(!matches("^[a-z0-9_]$", "Q"));
		TS_ASSERT(matches("^[^/]+$", "no-slashes-here"));
		TS_ASSERT(!matches("^[^/]+$", "a/b"));
	}

	void test_quantifiers_greedy_with_backtracking() {
		TS_ASSERT(matches("^a*$", ""));
		TS_ASSERT(matches("^a*$", "aaaa"));
		TS_ASSERT(matches("^a+$", "a"));
		TS_ASSERT(!matches("^a+$", ""));
		TS_ASSERT(matches("^ab?c$", "ac"));
		TS_ASSERT(matches("^ab?c$", "abc"));
		// Requires giving back characters from the greedy star.
		TS_ASSERT(matches("^a*a$", "aaa"));
		TS_ASSERT(matches("^.*b$", "aaab"));
	}

	void test_alternation_and_groups() {
		TS_ASSERT(matches("^(?:cat|dog)$", "cat"));
		TS_ASSERT(matches("^(?:cat|dog)$", "dog"));
		TS_ASSERT(!matches("^(?:cat|dog)$", "cow"));
		TS_ASSERT(matches("^(a|b)+$", "abba"));
		TS_ASSERT(matches("^x(?:ab)*y$", "xy"));
		TS_ASSERT(matches("^x(?:ab)*y$", "xababy"));
		TS_ASSERT(!matches("^x(?:ab)*y$", "xaby_no"));
	}

	void test_escapes() {
		TS_ASSERT(matches("a\\.c", "a.c"));
		TS_ASSERT(!matches("a\\.c", "abc"));
		TS_ASSERT(matches("\\/path\\/to", "/path/to"));
		TS_ASSERT(matches("c:\\\\dir", "c:\\dir"));
		TS_ASSERT(matches("^\\d+$", "12345"));
		TS_ASSERT(!matches("^\\d+$", "12a45"));
		TS_ASSERT(matches("^\\w+$", "Foo_bar9"));
		TS_ASSERT(!matches("^\\w+$", "a b"));
		TS_ASSERT(matches("a\\sb", "a b"));
		TS_ASSERT(matches("^\\S+$", "dense"));
		TS_ASSERT(matches("\\d", "x5y"));
	}

	// The exact shape js-debug emits for a breakpoint in /usr/g/room11.s:
	// case-insensitive classes for letters, escaped slashes and dots, and
	// the ($|\?) suffix so query strings are ignored.
	void test_jsdebug_file_url_regex() {
		const char *pattern =
			"[fF][iI][lL][eE]:\\/\\/\\/[uU][sS][rR]\\/[gG]\\/"
			"[rR][oO][oO][mM]11\\.[sS]($|\\?)";
		TS_ASSERT(matches(pattern, "file:///usr/g/room11.s"));
		TS_ASSERT(matches(pattern, "FILE:///USR/G/ROOM11.S"));
		TS_ASSERT(matches(pattern, "file:///usr/g/room11.s?cachebust=1"));
		TS_ASSERT(!matches(pattern, "file:///usr/g/room11.sx"));
		TS_ASSERT(!matches(pattern, "file:///usr/g/room11_s"));
	}

	// js-debug alternates literal and percent-encoded spellings of
	// non-ASCII path segments: (?:<utf8 bytes>|%F0%9F%92%A9).
	void test_jsdebug_percent_encoding_alternation() {
		Common::String pattern = "\\/(?:";
		pattern += "\xF0\x9F\x92\xA9"; // literal UTF-8 bytes
		pattern += "|%[fF]0%9[fF]%92%[aA]9)\\.[jJ][sS]($|\\?)";
		Inspector::Regex re(pattern);
		TS_ASSERT(re.test("scummvm-dbg://x/\xF0\x9F\x92\xA9.js"));
		TS_ASSERT(re.test("scummvm-dbg://x/%F0%9F%92%A9.js"));
		TS_ASSERT(re.test("scummvm-dbg://x/%f0%9f%92%a9.js"));
		TS_ASSERT(!re.test("scummvm-dbg://x/poop.js"));
	}

	// Our own synthetic script URLs must be matchable too.
	void test_scummvm_dbg_urls() {
		TS_ASSERT(matches("^scummvm-dbg:\\/\\/scumm\\/room11\\/local-200$",
		                  "scummvm-dbg://scumm/room11/local-200"));
		TS_ASSERT(matches("scummvm-dbg:.*\\/script-994", "scummvm-dbg://sci/script-994"));
	}

	void test_invalid_patterns_never_match() {
		TS_ASSERT(!matches("[abc", "a"));       // unterminated class
		TS_ASSERT(!matches("(?:abc", "abc"));   // unterminated group
		TS_ASSERT(!matches("abc)", "abc"));     // stray close
		TS_ASSERT(!matches("*a", "aaa"));       // quantifier without atom
		TS_ASSERT(!matches("a\\", "a"));        // trailing backslash
		Inspector::Regex bad("[abc");
		TS_ASSERT(!bad.valid());
	}

	// A zero-width loop body must not hang the matcher, and a
	// catastrophic-backtracking-shaped pattern must terminate (bounded
	// steps degrade to no-match rather than a hung debuggee).
	void test_pathological_patterns_terminate() {
		TS_ASSERT(matches("(?:a*)*b", "aaab"));
		TS_ASSERT(!matches("^(?:a*)*$", "aaa!"));
		Common::String longSubject;
		for (int i = 0; i < 60; i++)
			longSubject += 'a';
		longSubject += '!';
		// Classic exponential blowup shape; must return quickly.
		TS_ASSERT(!matches("^(?:a|a)+$", longSubject.c_str()));
	}

	void test_plus_after_group_backtracks_across_boundary() {
		TS_ASSERT(matches("^(?:ab|a)+b$", "aab"));
		TS_ASSERT(matches("^(?:ab|a)+b$", "abab")); // (ab)(a) + b
	}
};
