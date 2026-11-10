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

#include "common/ptr.h"

#include "adl/playdate_picker.h"

namespace Adl {

PlaydatePicker::PlaydatePicker(AdlEngine *vm) :
		_vm(vm), _serial(0), _revision(0), _allInputs(false), _inGame(false), _nouns(false) {
	Playdate::setPickerSource(this);
}

PlaydatePicker::~PlaydatePicker() {
	Playdate::setPickerSource(nullptr);
}

// Once the game takes commands the list keeps its place between prompts, so
// the game screen does not jump sideways with every command.
bool PlaydatePicker::pickerVisible() {
	sync();
	return _inGame || !_words.empty();
}

bool PlaydatePicker::pickerAcceptsInput() {
	sync();
	return !_words.empty();
}

const Common::StringArray &PlaydatePicker::pickerWords() {
	sync();
	return _words;
}

void PlaydatePicker::clear() {
	_nouns = false;
	_verbs.clear();
	_words.clear();
	_typed.clear();
	_revision++;
}

void PlaydatePicker::add(const Common::String &label, const Common::String &typed) {
	_words.push_back(label);
	_typed.push_back(typed);
}

// Rebuilds the list whenever the engine starts waiting for different input
void PlaydatePicker::sync() {
	if (_serial == _vm->_inputSerial)
		return;
	_serial = _vm->_inputSerial;

	clear();
	if (_vm->_inputKind == AdlEngine::kInputCommand)
		buildVerbs();
	else if (_vm->_inputKind != AdlEngine::kInputNone)
		buildChoices();
}

Common::String PlaydatePicker::verbName(byte verb) const {
	Common::String name;
	if (verb >= 1 && verb <= _vm->_priVerbs.size())
		name = _vm->_priVerbs[verb - 1];
	name.trim();
	return name;
}

Common::String PlaydatePicker::nounName(byte noun) const {
	Common::String name;
	if (noun >= 1 && noun <= _vm->_priNouns.size())
		name = _vm->_priNouns[noun - 1];
	name.trim();
	return name;
}

void PlaydatePicker::collectValid(const Commands &commands) {
	for (const auto &cmd : commands) {
		if (cmd.verb == IDI_ANY)
			continue;

		Common::ScopedPtr<ScriptEnv> env(_vm->createScriptEnv(cmd, _vm->_state.room, cmd.verb, cmd.noun));
		if (!_vm->matchCommand(*env))
			continue;

		Input input;
		input.verb = cmd.verb;
		input.noun = cmd.noun;
		_valid.push_back(input);
	}
}

void PlaydatePicker::buildVerbs() {
	clear();
	_inGame = true;

	_valid.clear();
	collectValid(_vm->_roomData.commands);
	collectValid(_vm->_roomCommands);
	_allInputs = _valid.empty();

	for (uint verb = 1; verb <= _vm->_priVerbs.size(); verb++) {
		bool offered = _allInputs;
		for (uint i = 0; i < _valid.size() && !offered; i++)
			offered = _valid[i].verb == verb;

		const Common::String name = verbName(verb);
		if (!offered || name.empty())
			continue;

		_verbs.push_back(verb);
		_words.push_back(name);
	}
}

void PlaydatePicker::buildNouns(byte verb) {
	const Common::String verbText = verbName(verb);
	Common::Array<byte> nouns;
	bool anyNoun = _allInputs;

	for (uint i = 0; i < _valid.size(); i++) {
		if (_valid[i].verb != verb)
			continue;
		if (_valid[i].noun == IDI_ANY)
			anyNoun = true;
		else
			nouns.push_back(_valid[i].noun);
	}

	if (_allInputs) {
		for (uint noun = 1; noun <= _vm->_priNouns.size(); noun++)
			nouns.push_back(noun);
	} else if (anyNoun) {
		// The verb takes any noun: offer it alone and with the things at hand
		for (uint noun = 1; noun <= _vm->_priNouns.size(); noun++) {
			if (nounName(noun).empty()) {
				nouns.push_back(noun);
				break;
			}
		}
		for (const auto &item : _vm->_state.items) {
			if (item.region == _vm->_state.region && (item.room == _vm->_state.room || item.room == IDI_ANY))
				nouns.push_back(item.noun);
		}
	}

	clear();
	_nouns = true;

	for (uint i = 0; i < nouns.size(); i++) {
		const Common::String name = nounName(nouns[i]);
		// The blank noun is the verb on its own
		const Common::String label = name.empty() ? verbText : name;
		const Common::String typed = name.empty() ? verbText : verbText + ' ' + name;

		bool present = false;
		for (uint j = 0; j < _typed.size() && !present; j++)
			present = _typed[j] == typed + '\r';
		if (!present)
			add(label, typed + '\r');
	}
}

void PlaydatePicker::buildChoices() {
	const bool line = _vm->_inputKind == AdlEngine::kInputLine;
	const Common::String &choices = _vm->_inputChoices;

	if (!choices.empty()) {
		for (uint i = 0; i < choices.size(); i++) {
			const Common::String key(choices[i]);
			add(key, line ? key + '\r' : key);
		}
		return;
	}

	// A lone key press is Return, which the A button sends without a list
	if (!line)
		return;

	add("Ent", "\r");
	add("Del", "\b");
	for (char c = 'A'; c <= 'Z'; c++)
		add(Common::String(c), Common::String(c));
	for (char c = '0'; c <= '9'; c++)
		add(Common::String(c), Common::String(c));
	add("Spc", " ");
}

Common::String PlaydatePicker::pickerSelect(uint index) {
	sync();
	if (index >= _words.size())
		return Common::String();

	if (_vm->_inputKind != AdlEngine::kInputCommand)
		return _typed[index];

	uint entry = index;
	if (!_nouns) {
		buildNouns(_verbs[index]);
		// A verb with a single way to use it is typed right away
		if (_typed.size() != 1)
			return Common::String();
		entry = 0;
	}

	// The list returns with the next prompt
	const Common::String typed = _typed[entry];
	clear();
	return typed;
}

bool PlaydatePicker::pickerBack() {
	if (!_nouns || _vm->_inputKind != AdlEngine::kInputCommand)
		return false;
	buildVerbs();
	return true;
}

} // End of namespace Adl
