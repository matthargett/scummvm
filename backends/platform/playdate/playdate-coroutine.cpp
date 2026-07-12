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

#include "common/scummsys.h"

#include "backends/platform/playdate/playdate-coroutine.h"

namespace Playdate {

static CoroutineEntry s_entry;

#if TARGET_PLAYDATE

// Cortex-M7 (Thumb-2, hard-float) cooperative context switch. A saved
// context is a pointer to a block holding r4-r11, sp, lr and the
// callee-saved VFP registers s16-s31. This follows the well-known
// libco ARM implementation.

struct Context {
	// s16-s31 (16 registers) followed by r4-r11, sp, lr
	uint32 buffer[16 + 10];
};

static Context s_mainContext;
static Context s_coroContext;
static uint8 *s_coroStack;

__attribute__((naked, used))
static void coroutineSwitch(Context * /*to (r0)*/, Context * /*from (r1)*/) {
	// Thumb-2 does not allow SP or PC in stmia/ldmia register lists, so
	// they are saved and restored through a scratch register.
	__asm__ volatile(
		"vstmia r1!, {d8-d15}\n"     // save callee-saved VFP (s16-s31)
		"stmia r1!, {r4-r11}\n"      // save callee-saved core registers
		"mov r2, sp\n"
		"stmia r1!, {r2, lr}\n"      // save sp and return address

		"vldmia r0!, {d8-d15}\n"     // restore the other context
		"ldmia r0!, {r4-r11}\n"
		"ldmia r0!, {r2, r3}\n"      // r2 = sp, r3 = resume address
		"mov sp, r2\n"
		"bx r3\n"
	);
}

static void coroutineTrampoline() {
	s_entry();
	// entry must not return
	for (;;)
		;
}

void coroutineCreate(CoroutineEntry entry, uint stackSize) {
	s_entry = entry;
	s_coroStack = new uint8[stackSize];

	// Top of stack, 8-byte aligned per the AAPCS
	uint32 sp = (uint32)(s_coroStack + stackSize);
	sp &= ~7u;

	uint32 *ctx = s_coroContext.buffer;
	for (int i = 0; i < 16 + 10; i++)
		ctx[i] = 0;
	ctx[16 + 8] = sp;                              // sp slot
	ctx[16 + 9] = (uint32)coroutineTrampoline;     // lr -> pc on first switch
}

void coroutineInit() {
}

void coroutineResume() {
	coroutineSwitch(&s_coroContext, &s_mainContext);
}

void coroutineYield() {
	coroutineSwitch(&s_mainContext, &s_coroContext);
}

#else // simulator: native shared object, use ucontext

} // End of namespace Playdate

#include <ucontext.h>

namespace Playdate {

static ucontext_t s_mainContext;
static ucontext_t s_coroContext;
static uint8 *s_coroStack;

static void coroutineTrampoline() {
	s_entry();
	for (;;)
		;
}

void coroutineInit() {
}

void coroutineCreate(CoroutineEntry entry, uint stackSize) {
	s_entry = entry;
	s_coroStack = new uint8[stackSize];

	getcontext(&s_coroContext);
	s_coroContext.uc_stack.ss_sp = s_coroStack;
	s_coroContext.uc_stack.ss_size = stackSize;
	s_coroContext.uc_link = &s_mainContext;
	makecontext(&s_coroContext, coroutineTrampoline, 0);
}

void coroutineResume() {
	swapcontext(&s_mainContext, &s_coroContext);
}

void coroutineYield() {
	swapcontext(&s_coroContext, &s_mainContext);
}

#endif

} // End of namespace Playdate
