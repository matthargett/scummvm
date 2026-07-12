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

#ifndef PLATFORM_PLAYDATE_COROUTINE_H
#define PLATFORM_PLAYDATE_COROUTINE_H

/**
 * Minimal cooperative coroutine used to reconcile ScummVM's blocking
 * main loop with the Playdate's callback based runtime.
 *
 * The Playdate OS calls the game's update callback once per frame and
 * expects it to return promptly. ScummVM's scummvm_main() never
 * returns until the user quits, so it is run on a separate stack and
 * yields back to the OS callback once per rendered frame.
 *
 * The implementation uses ucontext in the simulator (a native shared
 * object) and a small Cortex-M7 context switch on the device.
 */
namespace Playdate {

typedef void (*CoroutineEntry)();

/** Initializes the coroutine system for the calling (main) context. */
void coroutineInit();

/**
 * Creates a coroutine that will run @p entry the first time it is
 * switched to. @p entry must not return.
 */
void coroutineCreate(CoroutineEntry entry, uint stackSize);

/** Switches from the main context into the coroutine. */
void coroutineResume();

/** Switches from the coroutine back to the main context. */
void coroutineYield();

} // End of namespace Playdate

#endif
