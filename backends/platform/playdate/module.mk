MODULE := backends/platform/playdate

MODULE_OBJS := \
	playdate-backend.o \
	playdate-input.o \
	playdate-fs.o \
	playdate-main.o

MODULE_OBJS := $(addprefix $(MODULE)/, $(MODULE_OBJS))
OBJS := $(MODULE_OBJS) $(OBJS)
MODULE_DIRS += $(sort $(dir $(MODULE_OBJS)))

PLAYDATE_SDK_PATH ?= /Users/matt/Developer/PlaydateSDK
PLAYDATE_LIB := pdex.dylib
PLAYDATE_PDX := scummvm.pdx
PLAYDATE_DETECT_OBJS := engines/agi/detection.o engines/agi/wagparser.o
PLAYDATE_CXXFLAGS := $(filter-out -I/opt/homebrew/%,$(CXXFLAGS))
PLAYDATE_LDFLAGS := $(filter-out -L/opt/homebrew/%,$(LDFLAGS))
# Strip out host framework/libs; Playdate runtime provides the needed symbols.
PLAYDATE_LIBS := $(filter-out -framework% -L/opt/homebrew/% -lfreetype -lfribidi,$(LIBS))
PLAYDATE_LIBS := $(filter-out AudioUnit AudioToolbox Carbon CoreMIDI Cocoa,$(PLAYDATE_LIBS))

.PHONY: playdate-lib playdate-pdx playdate-run playdate-clean

# Build a simulator-compatible pdex.dylib by force-loading the AGI engine
# and audio core while filtering out SDL objects.
playdate-lib: $(PLAYDATE_DETECT_OBJS) $(OBJS)
	@echo "Linking Playdate library..."
	$(CXX) $(PLAYDATE_LDFLAGS) $(PLAYDATE_CXXFLAGS) -dynamiclib -Wl,-undefined,error -Wl,-exported_symbol,_eventHandler \
		-install_name @rpath/$(PLAYDATE_LIB) \
		-o $(PLAYDATE_LIB) \
		$(PLAYDATE_DETECT_OBJS) \
		-Wl,-force_load,engines/agi/libagi.a \
		-Wl,-force_load,audio/libaudio.a \
		$(filter-out backends/platform/sdl/% backends/graphics/sdl% backends/events/sdl% backends/mixer/sdl% backends/midi/coreaudio.o backends/midi/coremidi.o backends/midi/cocoa.o backends/dialogs/macosx/% backends/taskbar/macosx/% backends/updates/macosx/% backends/text-to-speech/macosx/%,$(OBJS)) \
		$(PLAYDATE_LIBS)
	@codesign --force --sign - $(PLAYDATE_LIB) 2>/dev/null || true
	@otool -L $(PLAYDATE_LIB) | grep -E "homebrew|SDL|vorbis|FLAC|jpeg|png" || echo "  ✓ No extra dylib deps"
	@nm -g $(PLAYDATE_LIB) | grep -q "_eventHandler" && echo "  ✓ eventHandler exported" || echo "  ✗ eventHandler NOT exported"

# Create .pdx bundle
playdate-pdx: playdate-lib
	@echo "Creating .pdx bundle..."
	@mkdir -p $(PLAYDATE_PDX)
	@cp $(PLAYDATE_LIB) $(PLAYDATE_PDX)/
	@cp backends/platform/playdate/pdxinfo $(PLAYDATE_PDX)/
	@mkdir -p $(PLAYDATE_PDX)/games
	@if [ -n "$(PLAYDATE_DEFAULT_GAME_DIR)" ] && [ -d "$(PLAYDATE_DEFAULT_GAME_DIR)" ]; then \
		dest=$(PLAYDATE_PDX)/games/$$(basename "$(PLAYDATE_DEFAULT_GAME_DIR)"); \
		rm -rf "$$dest"; \
		cp -R "$(PLAYDATE_DEFAULT_GAME_DIR)" "$$dest"; \
		echo "Bundled default AGI game from $(PLAYDATE_DEFAULT_GAME_DIR)"; \
	elif ls -A "$(PLAYDATE_PDX)/games" >/dev/null 2>&1; then \
		echo "Using existing bundled games in $(PLAYDATE_PDX)/games"; \
	else \
		echo "Warning: no AGI game assets bundled. Set PLAYDATE_DEFAULT_GAME_DIR to a game directory."; \
	fi
	@echo "Bundle created at $(PLAYDATE_PDX)"

# Launch in simulator
playdate-run: playdate-pdx
	@echo "Launching Playdate Simulator..."
	@open "$(PLAYDATE_SDK_PATH)/bin/Playdate Simulator.app" $(PLAYDATE_PDX)

# Clean Playdate build artifacts
playdate-clean:
	@rm -f $(PLAYDATE_LIB)
	@rm -rf $(PLAYDATE_PDX)
