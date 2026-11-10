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

#ifndef AGI_PLAYDATE_MENU_H
#define AGI_PLAYDATE_MENU_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "backends/platform/playdate/playdate-picker.h"

namespace Agi {

class AgiEngine;

/**
 * Vocabulary of the Playdate crank word picker for AGI parser games.
 *
 * The words offered are scoped to the current room: every time a logic runs
 * a said() test, the words it checks for are recorded. A said() phrase is
 * "verb noun...", so the picker first offers the room's verbs, then the
 * nouns the room pairs with the chosen verb. During GetString/GetNumber the
 * list turns into an on-screen keyboard. The list itself is drawn and driven
 * by the backend (see Playdate::PickerSource).
 */
class PlaydateMenu : public Playdate::PickerSource {
public:
	PlaydateMenu(AgiEngine *vm);
	~PlaydateMenu() override;

	bool pickerVisible() override;
	bool pickerAcceptsInput() override;
	const Common::StringArray &pickerWords() override;
	uint pickerRevision() override { return _revision; }
	uint pickerList() override;
	Common::String pickerSelect(uint index) override;
	bool pickerBack() override;

	/** Clears the recorded room vocabulary. Called on room changes. */
	void resetContextWords();

	/** Records a said() phrase (verb id first, then noun ids). */
	void addSaidPhrase(const uint16 *ids, uint count, bool fromLogic0);

	// Whether phrase i should be offered given the current game's style
	// (logic-0 phrases are hidden once any room-logic phrase has been seen).
	bool phraseVisible(uint i) const {
		return !(_hasRoomLogicPhrase && _phraseFromLogic0[i]);
	}

private:
	enum Mode {
		kModeVerb,
		kModeNoun,
		kModeChar // on-screen keyboard
	};

	enum CharInput {
		kCharNone,
		kCharString, // GetString
		kCharNumber, // GetNumber
		kCharKey     // a key answer to a message left on screen
	};

	void enterVerbMode();
	// verbWord is by value, not by reference: the caller passes an element of
	// _listWords, which this function clears before using verbWord again.
	void enterNounMode(uint16 verbId, Common::String verbWord);
	void enterCharMode(CharInput input);
	Common::String phraseText(const Common::Array<uint16> &ids, uint from) const;

	/** The character input the game is waiting for, if any. */
	CharInput charInput();

	/** Enters/leaves char mode to match the current inner-loop state. */
	void syncCharMode();

	AgiEngine *_vm;
	bool _parserGame; // sticky: set once any said() phrase is recorded

	// Sticky: set once a said() phrase is recorded from a room logic (logic
	// number != 0). Games that use room logics (King's Quest) then hide their
	// logic-0 phrases (global speed/system commands) as clutter; games that
	// keep everything in logic 0 (Space Quest) never set this, so their
	// logic-0 phrases are shown - otherwise the picker would be empty.
	bool _hasRoomLogicPhrase;

	// Recorded room vocabulary. Each phrase is the full said() word-group
	// id sequence with the verb first. Only phrases the picker can fully
	// compose are kept: those containing a wildcard (anyword / rest-of-
	// line), which need a word the keyboard-less picker cannot supply, are
	// dropped, so every offered command satisfies its said() test.
	// _phraseFromLogic0[i] tags whether phrase i came from logic 0.
	Common::Array<Common::Array<uint16> > _phrases;
	Common::Array<bool> _phraseFromLogic0;

	Mode _mode;
	uint16 _verbId;            // selected verb (in noun mode)
	Common::String _verbWord;  // its canonical word
	CharInput _charInput;      // char mode: what the keyboard is for
	uint32 _questionSinceMs;   // since when a message has been left on screen

	// The list currently shown. In verb mode _listIds holds verb ids; in
	// noun mode _listCommands holds the full command each entry submits.
	Common::Array<uint16> _listIds;
	Common::Array<Common::String> _listWords;    // display labels
	Common::Array<Common::String> _listCommands; // full commands (noun mode)
	uint _revision; // bumped whenever the list is rebuilt

	static const int kMaxSynonyms = 2;   // extra synonym entries per verb+noun command
	static const int kNounSynonymCap = 10; // stop adding synonyms once the noun list is this long
};

} // End of namespace Agi

#endif /* AGI_PLAYDATE_MENU_H */
