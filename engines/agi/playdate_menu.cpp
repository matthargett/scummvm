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

#include "agi/playdate_menu.h"
#include "agi/agi.h"
#include "agi/graphics.h"
#include "agi/keyboard.h"
#include "agi/text.h"
#include "agi/words.h"

namespace Agi {

static const uint32 kQuestionDelayMs = 250;

// Sierra's global system/menu verbs. These are offered through the Playdate
// system menu (or simply not needed on a save-anywhere handheld), so they are
// kept out of the word picker - otherwise a game that runs them in logic 0
// every cycle (Space Quest) buries its real room verbs under them.
static bool isSystemVerb(const Common::String &word) {
	static const char *const kSystemVerbs[] = {
		"save", "restore", "restart", "quit", "pause", "help", "aid",
		"fast", "fastest", "slow", "normal", "speed", "clock", "sound",
		"music", "version", "about", "credits", "delay"
	};
	for (uint i = 0; i < ARRAYSIZE(kSystemVerbs); ++i) {
		if (word.equalsIgnoreCase(kSystemVerbs[i]))
			return true;
	}
	return false;
}

// The interpreter's and the games' debugging commands
static bool isDebugVerb(const Common::String &word) {
	static const char *const kDebugVerbs[] = {
		"debug", "tester", "test", "cheat", "tp", "pos", "position", "mem",
		"priority", "object"
	};
	// "kqdbg", "sq2dbg", ...
	if (word.hasSuffixIgnoreCase("dbg"))
		return true;
	for (uint i = 0; i < ARRAYSIZE(kDebugVerbs); ++i) {
		if (word.equalsIgnoreCase(kDebugVerbs[i]))
			return true;
	}
	return false;
}

PlaydateMenu::PlaydateMenu(AgiEngine *vm) :
	_vm(vm), _parserGame(false), _hasRoomLogicPhrase(false),
	_mode(kModeVerb), _verbId(0), _charInput(kCharNone), _questionSinceMs(0), _answerKeysLogics(0), _revision(0) {
}

PlaydateMenu::~PlaydateMenu() {
}

bool PlaydateMenu::pickerVisible() {
	// The on-screen keyboard is always shown when text is asked for; the
	// word list only for games that use the parser.
	if (charInput() != kCharNone)
		return true;
	return _parserGame;
}

PlaydateMenu::CharInput PlaydateMenu::charInput() {
	bool mainCycle = !_vm->_game.cycleInnerLoopActive;
	if (!mainCycle) {
		switch (_vm->_game.cycleInnerLoopType) {
		case CYCLE_INNERLOOP_GETSTRING:
			return kCharString;
		case CYCLE_INNERLOOP_GETNUMBER:
			return kCharNumber;
		case CYCLE_INNERLOOP_HAVEKEY:
			mainCycle = true;
			break;
		default:
			break;
		}
	}

	// A message that stays on screen while the game runs on with the prompt
	// off, in a room whose logic looks at which character was typed, is a
	// question (the Leisure Suit Larry age quiz). A message box that waits
	// for a key is not: that one only looks the same until its wait begins.
	if (!mainCycle || !_vm->_text->_messageState.window_Active || _vm->promptIsEnabled() ||
	    answerKeys().empty()) {
		_questionSinceMs = 0;
		return kCharNone;
	}

	const uint32 now = g_system->getMillis();
	if (_questionSinceMs == 0)
		_questionSinceMs = now ? now : 1;
	return now - _questionSinceMs >= kQuestionDelayMs ? kCharKey : kCharNone;
}

// Collects the characters a logic compares the key variable with
void PlaydateMenu::collectKeyTests(const AgiLogic &logic, KeyTests &keyTests) const {
	const AgiOpCodeEntry *commands = _vm->getOpCodesTable();
	const AgiOpCodeEntry *tests = _vm->getCondOpCodesTable();

	bool inTest = false;
	for (int ip = logic.sIP; ip + 2 < logic.size;) {
		const byte op = logic.data[ip++];
		if (!inTest) {
			if (op == 0xFF)
				inTest = true;
			else if (op == 0xFE)
				ip += 2;
			else
				ip += commands[op].parameterSize;
			continue;
		}

		if (op == 0xFF) {
			inTest = false;
			ip += 2;
		} else if (op == 0x0E && _vm->getVersion() >= 0x2000) { // said
			ip += logic.data[ip] * 2 + 1;
		} else if (op < 0xFC) {
			const byte value = logic.data[ip + 1];
			if (logic.data[ip] == VM_VAR_KEY && Common::isPrint(value) && value != ' ') {
				if (op == 0x01)
					keyTests.equal.push_back(value);
				else if (op == 0x03)
					keyTests.less.push_back(value);
				else if (op == 0x05)
					keyTests.greater.push_back(value);
			}
			ip += tests[op].parameterSize;
		}
	}
}

// A script accepts a range either as "key > x && key < y" or, negated, as
// "!(key < x) && !(key > y)"; both forms are read as the range they imply.
bool PlaydateMenu::KeyTests::accept(byte key) const {
	for (uint i = 0; i < equal.size(); ++i) {
		if (equal[i] == key)
			return true;
	}
	for (uint g = 0; g < greater.size(); ++g) {
		for (uint l = 0; l < less.size(); ++l) {
			if (greater[g] < key && key < less[l] && less[l] - greater[g] <= 27)
				return true;
			if (less[l] <= key && key <= greater[g] && greater[g] - less[l] < 26)
				return true;
		}
	}
	return false;
}

// Logic 0 is left out: its key tests are the game's hot keys
const Common::String &PlaydateMenu::answerKeys() {
	uint loaded = 0;
	for (uint n = 1; n < MAX_DIRECTORY_ENTRIES; ++n) {
		if (_vm->_game.dirLogic[n].flags & RES_LOADED)
			loaded = loaded * 31 + n;
	}
	if (loaded == _answerKeysLogics)
		return _answerKeys;
	_answerKeysLogics = loaded;
	_answerKeys.clear();

	KeyTests keyTests;
	for (uint n = 1; n < MAX_DIRECTORY_ENTRIES; ++n) {
		if (_vm->_game.dirLogic[n].flags & RES_LOADED)
			collectKeyTests(_vm->_game.logics[n], keyTests);
	}

	for (char c = 'a'; c <= 'z'; ++c) {
		if (keyTests.accept(c) || keyTests.accept(toupper(c)))
			_answerKeys += c;
	}
	return _answerKeys;
}

uint PlaydateMenu::pickerList() {
	syncCharMode();
	return _mode == kModeChar ? (uint)kModeChar + _charInput : (uint)_mode;
}

bool PlaydateMenu::pickerAcceptsInput() {
	if (charInput() != kCharNone)
		return true; // on-screen keyboard owns input during text entry
	// Word picker only takes input for parser games at the command prompt,
	// never during message boxes, menus or other inner loops.
	return _parserGame && !_vm->_game.cycleInnerLoopActive &&
	       !_vm->_waitingForKey && _vm->promptIsEnabled();
}

void PlaydateMenu::resetContextWords() {
	_phrases.clear();
	_phraseFromLogic0.clear();
	// _hasRoomLogicPhrase is intentionally sticky across rooms: the game's
	// command-structure style does not change room to room.
	enterVerbMode();
}

void PlaydateMenu::addSaidPhrase(const uint16 *ids, uint count, bool fromLogic0) {
	// A said() phrase is "verb noun...". The picker can only offer
	// commands it can compose in full, so a phrase containing a wildcard
	// (1 = anyword, 9999 = rest-of-line) is dropped: anyword consumes a
	// parsed word the keyboard-less picker cannot supply, so offering the
	// verb alone would submit a command that fails the said() test.
	Common::Array<uint16> phrase;
	for (uint i = 0; i < count; ++i) {
		if (ids[i] == 1 || ids[i] == 9999)
			return;
		// Ignore-words (id 0) match nothing and are not stored in the
		// dictionary, so they cannot be shown or typed; skip them.
		if (ids[i] == 0)
			continue;
		phrase.push_back(ids[i]);
	}
	if (phrase.empty())
		return;

	// Drop the global system/menu commands (save, restore, speed controls, ...).
	// The player reaches those through the Playdate system menu, not the word
	// picker; in games that keep everything in logic 0 (Space Quest) they run
	// every cycle and would otherwise swamp the actual room verbs.
	const Common::String verb = _vm->_words->firstWordForId(phrase[0]);
	if (isSystemVerb(verb) || isDebugVerb(verb))
		return;

	// A said() from a room logic proves this game scopes commands per room, so
	// its logic-0 phrases are global clutter and get hidden (see phraseVisible).
	if (!fromLogic0)
		_hasRoomLogicPhrase = true;

	// Deduplicate identical phrases.
	for (uint i = 0; i < _phrases.size(); ++i) {
		if (_phrases[i].size() != phrase.size())
			continue;
		bool same = true;
		for (uint j = 0; j < phrase.size(); ++j) {
			if (_phrases[i][j] != phrase[j]) {
				same = false;
				break;
			}
		}
		if (same) {
			// Keep the more-visible source: a phrase also seen in a room logic
			// should not stay hidden as a logic-0 duplicate.
			if (!fromLogic0)
				_phraseFromLogic0[i] = false;
			return;
		}
	}

	_phrases.push_back(phrase);
	_phraseFromLogic0.push_back(fromLogic0);

	// Seeing any said() phrase proves this game uses the parser, so the
	// picker becomes active for the rest of the session.
	_parserGame = true;

	// A newly seen verb should appear in the verb list right away
	if (_mode == kModeVerb)
		enterVerbMode();
}

void PlaydateMenu::enterVerbMode() {
	_mode = kModeVerb;
	_verbId = 0;
	_verbWord.clear();

	_listIds.clear();
	_listWords.clear();
	_listCommands.clear();

	// Distinct verbs (first id of each phrase), in first-seen order.
	for (uint i = 0; i < _phrases.size(); ++i) {
		if (!phraseVisible(i))
			continue;
		const uint16 id = _phrases[i][0];
		bool present = false;
		for (uint j = 0; j < _listIds.size(); ++j) {
			if (_listIds[j] == id) {
				present = true;
				break;
			}
		}
		if (present)
			continue;

		Common::String word = wordForId(id);
		if (word.empty())
			continue;
		_listIds.push_back(id);
		_listWords.push_back(word);
	}

	_revision++;
}

// The word shown for a word group: its canonical form, or a synonym of it
// if that is one the list does not show
Common::String PlaydateMenu::wordForId(uint16 id) const {
	const Common::String first = _vm->_words->firstWordForId(id);
	if (!Playdate::isProfanity(first))
		return first;

	Common::Array<uint16> ids;
	ids.push_back(id);
	Common::Array<Common::String> synonyms;
	_vm->_words->collectWordsForIds(ids, synonyms);

	Common::String best;
	for (uint i = 0; i < synonyms.size(); ++i) {
		if (Playdate::isProfanity(synonyms[i]))
			continue;
		if (best.empty() || synonyms[i].size() < best.size())
			best = synonyms[i];
	}
	return best.empty() ? first : best;
}

Common::String PlaydateMenu::phraseText(const Common::Array<uint16> &ids, uint from) const {
	// Joins the words of a phrase (from index `from`) into a command
	// string. Returns empty if any id has no dictionary word.
	Common::String out;
	for (uint k = from; k < ids.size(); ++k) {
		Common::String word = wordForId(ids[k]);
		if (word.empty())
			return Common::String();
		if (!out.empty())
			out += ' ';
		out += word;
	}
	return out;
}

// verbWord is taken BY VALUE on purpose. The caller passes _listWords[selected]
// (select() does enterNounMode(_listIds[i], _listWords[i])), and the first thing
// we do is _listWords.clear() - which frees that very element. A const-reference
// parameter would then dangle, and the "verb-only phrase" branch below
// (label = verbWord) would copy a freed String: harmless-looking on some
// allocators, but a null _str / assert-abort on others (e.g. the macOS
// simulator). Copying the argument up front sidesteps the lifetime problem.
void PlaydateMenu::enterNounMode(uint16 verbId, Common::String verbWord) {
	_mode = kModeNoun;
	_verbId = verbId;
	_verbWord = verbWord;

	_listIds.clear();
	_listWords.clear();
	_listCommands.clear();

	// One entry per complete phrase that starts with this verb. The label
	// shows the words after the verb (or the verb itself when the phrase
	// is the verb alone); selecting submits the entire phrase, so
	// multi-word said() tests like said("put","key","in","lock") are
	// satisfied in full.
	for (uint i = 0; i < _phrases.size(); ++i) {
		if (!phraseVisible(i))
			continue;
		if (_phrases[i][0] != verbId)
			continue;

		Common::String command = phraseText(_phrases[i], 0);
		if (command.empty())
			continue; // a word is missing from the dictionary

		bool present = false;
		for (uint j = 0; j < _listCommands.size(); ++j) {
			if (_listCommands[j] == command) {
				present = true;
				break;
			}
		}
		if (present)
			continue;

		Common::String label = phraseText(_phrases[i], 1);
		if (label.empty())
			label = verbWord; // verb-only phrase

		_listCommands.push_back(command);
		_listWords.push_back(label);
		_listIds.push_back(0);

		// Offer a couple of alternate synonyms for a plain verb+noun command,
		// so the player can find the word the game's narrative actually uses
		// (e.g. "dagger" when the dictionary's canonical form is "knife"). Kept
		// gentle: only two-word phrases, at most kMaxSynonyms alternates each,
		// and only while the list is still short, so a room full of objects
		// does not balloon the column. Any synonym of the noun parses to the
		// same word id, so the composed command still satisfies the said() test.
		if (_phrases[i].size() == 2 && (int)_listWords.size() < kNounSynonymCap) {
			Common::Array<uint16> nounId;
			nounId.push_back(_phrases[i][1]);
			Common::Array<Common::String> synonyms;
			_vm->_words->collectWordsForIds(nounId, synonyms);
			int added = 0;
			for (uint s = 0; s < synonyms.size() && added < kMaxSynonyms; ++s) {
				if (synonyms[s].equalsIgnoreCase(label))
					continue; // already shown as the primary word
				Common::String synCommand = verbWord + " " + synonyms[s];
				bool dup = false;
				for (uint j = 0; j < _listCommands.size(); ++j) {
					if (_listCommands[j] == synCommand) {
						dup = true;
						break;
					}
				}
				if (dup)
					continue;
				_listCommands.push_back(synCommand);
				_listWords.push_back(synonyms[s]);
				_listIds.push_back(0);
				added++;
			}
		}
	}

	_revision++;
}

void PlaydateMenu::enterCharMode(CharInput input) {
	_mode = kModeChar;
	_charInput = input;

	_listIds.clear();
	_listWords.clear();
	_listCommands.clear();

	if (input == kCharKey) {
		const Common::String &keys = answerKeys();
		for (uint i = 0; i < keys.size(); ++i) {
			_listIds.push_back((uint16)keys[i]);
			_listWords.push_back(Common::String(keys[i]));
		}
		_listIds.push_back(AGI_KEY_ENTER);
		_listWords.push_back("Ent");
		_revision++;
		return;
	}

	if (input == kCharNumber) {
		// GetNumber accepts digits only.
		for (char c = '0'; c <= '9'; ++c) {
			_listIds.push_back((uint16)c);
			_listWords.push_back(Common::String(c));
		}
	} else {
		// GetString accepts any printable character, so the keyboard must
		// offer more than letters: names may contain digits, and prompts
		// like the Leisure Suit Larry phone number ("555-8039") need digits
		// and a dash. Letters come first (the common case), then digits,
		// space and a little punctuation.
		for (char c = 'A'; c <= 'Z'; ++c) {
			_listIds.push_back((uint16)c);
			_listWords.push_back(Common::String(c));
		}
		for (char c = '0'; c <= '9'; ++c) {
			_listIds.push_back((uint16)c);
			_listWords.push_back(Common::String(c));
		}
		_listIds.push_back((uint16)' ');
		_listWords.push_back("Spc");
		static const char kPunct[] = { '-', '.', '\'' };
		for (uint i = 0; i < ARRAYSIZE(kPunct); ++i) {
			_listIds.push_back((uint16)kPunct[i]);
			_listWords.push_back(Common::String(kPunct[i]));
		}
	}

	// Editing and submission entries, common to both modes.
	_listIds.push_back(AGI_KEY_BACKSPACE);
	_listWords.push_back("Del");
	_listIds.push_back(AGI_KEY_ENTER);
	_listWords.push_back("Ent");

	_revision++;
}

void PlaydateMenu::syncCharMode() {
	const CharInput input = charInput();
	if (input != kCharNone) {
		// Enter (or re-enter if the kind of input changed) char mode.
		if (_mode != kModeChar || _charInput != input)
			enterCharMode(input);
	} else if (_mode == kModeChar) {
		// The text prompt ended; return to the word list.
		enterVerbMode();
	}
}

const Common::StringArray &PlaydateMenu::pickerWords() {
	syncCharMode();
	return _listWords;
}

Common::String PlaydateMenu::pickerSelect(uint index) {
	syncCharMode();

	if (_mode == kModeChar) {
		if (index >= _listIds.size())
			return Common::String();
		const uint16 key = _listIds[index];
		if (key == AGI_KEY_ENTER)
			return "\r";
		if (key == AGI_KEY_BACKSPACE)
			return "\b";
		return Common::String((char)key);
	}

	if (_mode == kModeVerb) {
		if (index >= _listIds.size())
			return Common::String();
		enterNounMode(_listIds[index], _listWords[index]);

		// A verb whose only phrase is the verb alone is submitted directly
		// rather than showing a one-item noun list.
		if (_listCommands.size() == 1 && _listCommands[0] == _verbWord) {
			const Common::String command = _listCommands[0];
			enterVerbMode();
			return command + '\r';
		}
		if (_listCommands.empty())
			enterVerbMode(); // nothing composable; stay on verbs
		return Common::String();
	}

	if (index >= _listCommands.size())
		return Common::String();
	const Common::String command = _listCommands[index];
	enterVerbMode();
	return command + '\r';
}

bool PlaydateMenu::pickerBack() {
	if (_mode != kModeNoun)
		return false;
	enterVerbMode();
	return true;
}

} // End of namespace Agi
