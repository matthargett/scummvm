ScummVM Playdate README
------------------------------------------------------------------------

This is a backend for the Panic Playdate handheld console. It targets the
AGI and SCI (non-SCI32) engines, which suit the device's 400x240 1-bit
display and its controls.

Table of Contents:
------------------
- [1.0 Installation](#10-installation)
- [2.0 Controls](#20-controls)
- [3.0 Adding games](#30-adding-games)
- [4.0 Autorun](#40-autorun)
- [5.0 Compiling](#50-compiling)


1.0 Installation
----------------
Copy `scummvm.pdx` to the Playdate, either by sideloading it through the
Playdate account website or, in developer mode, by copying it into the
`Games` folder of the device's USB disk. The Playdate Simulator can also
run `scummvm.pdx` directly.


2.0 Controls
------------
In an AGI game the controls drive the game and the on-screen word list
that occupies the right edge of the display:

| Control  | Action                                          |
|----------|-------------------------------------------------|
| D-pad    | Move the ego / arrow keys                       |
| Crank    | Move the word-list selector                     |
| A        | Insert the highlighted word                     |
| B        | Go back (noun list to verb list)                |

The d-pad always reaches the game, so the ego keeps moving while the
word list is up.

### The crank word list

AGI parser games expect typed commands, which the Playdate has no
keyboard for. The picker on the right offers the words that matter in
the current room: as a room's script runs, the port records every word
its `said()` tests check for. Because a `said()` phrase is "verb
noun...", the list first shows the room's verbs; choosing a verb (A)
switches to the nouns that room pairs with it, and B returns to the
verbs. Choosing a noun - or a verb the room uses on its own, like
"look" - types the whole command at the prompt and submits it. Words
too long for the column are ellipsized, and the selected one scrolls
(marquees) so it can be read in full. This is the spiritual successor
to the on-screen word list of the Game Boy Advance AGI interpreter
(GBAGI).

Non-parser AGI games (Manhunter, Mixed-Up Mother Goose) are played
entirely with the d-pad and A/B; their rooms register no `said()`
phrases, so the word list simply stays empty.

### Point-and-click (SCI) games

Holding B switches to a **pointer mode** for the mouse-driven SCI games:
the d-pad moves the cursor, A left-clicks, a B tap right-clicks, and the
crank is the mouse wheel. Hold B again to return to the game controls.


3.0 Adding games
----------------
Game data is read from the Playdate's data folder for this app
(`Data/org.scummvm.scummvm/` in the Playdate Simulator). Place each
game in its own subdirectory there and add it from ScummVM's launcher
with "Add Game...", or edit `scummvm.ini` directly.


4.0 Autorun
-----------
If a file named `autorun` is present in the data folder, its first line
is treated as a game target and that game is launched directly, skipping
the launcher. This is useful for building a pdx dedicated to a single
game. Remove the file to return to the launcher.


5.0 Compiling
-------------
The build needs the arm-none-eabi GCC toolchain and the Playdate SDK.
Point `PLAYDATE_SDK_PATH` at the SDK, then configure for one of the two
hosts:

Device build (produces an ARM `pdex.bin` inside `scummvm.pdx`):

    export PLAYDATE_SDK_PATH=/path/to/PlaydateSDK
    ./configure --host=playdate
    make

Simulator build (produces a native `pdex.so` inside `scummvm.pdx`):

    export PLAYDATE_SDK_PATH=/path/to/PlaydateSDK
    ./configure --host=playdate-simulator
    make

Both builds package the result with the SDK's `pdc` tool. The device
build links against the SDK's `setup.c`/`link_map.ld`; the simulator
build produces a shared object loaded by `PlaydateSimulator`.
