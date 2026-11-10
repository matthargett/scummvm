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

#ifndef ADL_PLAYDATE_PICKER_H
#define ADL_PLAYDATE_PICKER_H

#include "common/array.h"
#include "common/str.h"
#include "common/str-array.h"

#include "backends/platform/playdate/playdate-picker.h"

#include "adl/adl.h"

namespace Adl {

/**
 * Vocabulary of the Playdate crank word picker for the ADL games.
 *
 * At the command prompt it offers the verbs that do something in the current
 * room, then the nouns that go with the chosen verb. For prompts that expect
 * one of a few keys it offers those, and for free text an on-screen keyboard.
 */
class PlaydatePicker : public Playdate::PickerSource {
public:
	PlaydatePicker(AdlEngine *vm);
	~PlaydatePicker() override;

	bool pickerVisible() override;
	bool pickerAcceptsInput() override;
	const Common::StringArray &pickerWords() override;
	uint pickerRevision() override { return _revision; }
	uint pickerList() override { return _revision; }
	Common::String pickerSelect(uint index) override;
	bool pickerBack() override;

private:
	struct Input {
		byte verb, noun;
	};

	void sync();
	void clear();
	void add(const Common::String &label, const Common::String &typed);
	void collectValid(const Commands &commands);
	void buildVerbs();
	void buildNouns(byte verb);
	void buildChoices();
	Common::String verbName(byte verb) const;
	Common::String nounName(byte noun) const;

	AdlEngine *_vm;
	uint _serial;
	uint _revision;

	// Commands that would currently be accepted; a noun of IDI_ANY stands
	// for any noun
	Common::Array<Input> _valid;
	bool _allInputs; // nothing specific is known, offer the whole dictionary
	bool _inGame;    // the command prompt has been reached

	bool _nouns;                 // the list holds the nouns of a chosen verb
	Common::Array<byte> _verbs;  // verb of each entry of the verb list
	Common::StringArray _words;  // labels shown
	Common::StringArray _typed;  // text each entry types (noun and choice lists)
};

} // End of namespace Adl

#endif
