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

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "backends/platform/playdate/osystem_playdate.h"
#include "base/main.h"
#include "pd_api.h"

static PlaydateAPI *g_pd = nullptr;
static bool g_scummvmInitialized = false;
static bool g_scummvmRunning = false;

// Playdate update callback - called every frame
static int updateCallback(void *userdata) {
	if (!g_scummvmRunning) {
		return 0;
	}

	// Process ScummVM frame
	// The main loop is handled internally by scummvm_main
	// We just need to keep the event loop running

	return 1;  // Return 1 to continue running
}

// Playdate event handler - main entry point
#ifdef __cplusplus
extern "C"
#endif
int eventHandler(PlaydateAPI *pd, PDSystemEvent event, uint32_t arg) {
	(void)arg;  // Unused

	if (event == kEventInit) {
		// Save Playdate API pointer
		g_pd = pd;

		// Set update callback
		pd->system->setUpdateCallback(updateCallback, pd);

		// Initialize ScummVM
		if (!g_scummvmInitialized) {
			// Create OSystem
			g_system = new OSystem_Playdate(pd);
			assert(g_system);

			// Build command line arguments
			// For now, just pass the program name
			// TODO: Add support for loading specific games
			const char *argv[] = { "scummvm" };
			int argc = 1;

			// Start ScummVM main loop in background
			// Note: This will block, so we need to handle this differently
			// For now, we'll initialize and then run updates in the callback
			g_scummvmInitialized = true;
			g_scummvmRunning = true;

			// Note: We can't call scummvm_main here directly as it blocks
			// Instead, we need to initialize the backend and then
			// handle the main loop updates in the update callback

			// Initialize backend
			g_system->initBackend();

			pd->system->logToConsole("ScummVM initialized for Playdate");
		}
	} else if (event == kEventTerminate) {
		// Cleanup
		if (g_system) {
			g_system->destroy();
			g_system = nullptr;
		}
		g_scummvmRunning = false;
	} else if (event == kEventPause) {
		// Game paused
		g_scummvmRunning = false;
	} else if (event == kEventResume) {
		// Game resumed
		g_scummvmRunning = true;
	}

	return 0;
}

// Alternative approach: Run ScummVM in a thread-like manner
// Since Playdate doesn't support threads, we need to integrate differently

// For initial testing, we'll use a simpler approach:
// Let the update callback drive the main loop

static bool g_mainLoopStarted = false;

static int playdateUpdate(void *userdata) {
	if (!g_scummvmRunning) {
		return 1;
	}

	if (!g_mainLoopStarted) {
		// Start the main loop on first update
		const char *argv[] = { "scummvm", "--list-targets" };
		int argc = 2;

		// This will block, so we need a different approach
		// TODO: Integrate ScummVM's main loop more deeply
		// scummvm_main(argc, (char **)argv);

		g_mainLoopStarted = true;
	}

	// Process events and update
	// The event manager and graphics manager will be called
	// through the normal ScummVM flow

	return 1;
}
