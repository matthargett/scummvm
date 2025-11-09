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

#ifdef __cplusplus
extern "C" {
#endif
#include "pd_api.h"
#ifdef __cplusplus
}
#endif

static PlaydateAPI *g_pd = nullptr;

// Playdate event handler - main entry point
#ifdef __cplusplus
extern "C"
#endif
int eventHandler(PlaydateAPI *pd, PDSystemEvent event, uint32_t arg) {
	(void)arg;  // Unused

	if (event == kEventInit) {
		// Save Playdate API pointer
		g_pd = pd;

		// Create OSystem
		g_system = new OSystem_Playdate(pd);
		assert(g_system);

		// Build command line arguments
		static const char *argv[] = { "scummvm", nullptr };
		static int argc = 1;

		// Run ScummVM main
		// Note: This may block, but that should be OK for Playdate
		int result = scummvm_main(argc, const_cast<char**>(argv));

		pd->system->logToConsole("ScummVM exited with code %d", result);

		// Cleanup
		if (g_system) {
			g_system->destroy();
			delete g_system;
			g_system = nullptr;
		}
	} else if (event == kEventTerminate) {
		// Cleanup on terminate
		if (g_system) {
			g_system->quit();
		}
	}

	return 0;
}
