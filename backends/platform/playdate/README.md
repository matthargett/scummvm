# ScummVM for Playdate

This is a port of ScummVM to the Panic Playdate handheld console, focusing on AGI and SCI game engines.

## Platform Specifications

- **Display**: 400×240 pixels, 1-bit monochrome (black and white)
- **CPU**: ARM Cortex-M7
- **Controls**: D-pad, A button, B button, analog crank
- **Audio**: 44.1kHz stereo
- **SDK**: Playdate SDK 3.0.1

## Supported Engines

This Playdate port includes only the following engines to fit within the device's memory constraints:

- **AGI**: Sierra's Adventure Game Interpreter (King's Quest 1-4, Space Quest 1-2, etc.)
- **SCI**: Sierra Creative Interpreter 0-1.1 (King's Quest 5, Space Quest 4, etc.)

Note: SCI32 (higher resolution SCI games) is disabled due to memory and display constraints.

## Graphics Implementation

The Playdate has a 1-bit (monochrome) 400×240 display. ScummVM's paletted graphics are converted to monochrome using Floyd-Steinberg dithering for optimal visual quality.

AGI games originally used approximately 160×200 effective resolution (half of Hercules), which scales cleanly to Playdate's 400×240 display with integer scaling.

## Controls

- **D-pad**: Arrow keys / Movement
- **A button**: Enter / Action
- **B button**: Escape / Cancel
- **Crank**: Mouse wheel / Scroll (rotation-based)

## Audio

The port uses Playdate's audio API with 44.1kHz sample rate. AGI and SCI games will use their 3-voice AdLib sound support. Some audio processing features may be disabled to maintain acceptable performance on the Cortex-M7 processor.

## Building for Playdate

### Prerequisites

1. **Playdate SDK 3.0.1** installed
2. **ARM embedded toolchain** (`arm-none-eabi-gcc`)
   - macOS: Installed automatically with Playdate SDK to `/usr/local/playdate`
   - Linux: Install `arm-none-eabi-newlib` package
   - Windows: Install from developer.arm.com
3. Set the `PLAYDATE_SDK_PATH` environment variable:
   ```bash
   export PLAYDATE_SDK_PATH=/path/to/PlaydateSDK
   ```

### Build Steps

1. Configure ScummVM for Playdate:
   ```bash
   ./configure --host=playdate
   ```

2. Build:
   ```bash
   make
   ```

3. The output will be `scummvm.pdx` in the build directory.

### Alternative: CMake Build

For a Playdate SDK-native build using CMake:

```bash
cd backends/platform/playdate
mkdir build
cd build
cmake ..
make
```

## Installing Games

1. Copy your game data files to the `.pdx` bundle:
   ```
   scummvm.pdx/
   ├── pdex.bin
   ├── pdxinfo
   ├── games/
   │   ├── sq1/          # Space Quest 1 game files
   │   ├── kq1/          # King's Quest 1 game files
   │   └── ...
   ```

2. For the Playdate Simulator:
   ```bash
   open scummvm.pdx  # macOS
   ```

3. For Playdate device:
   - Connect via USB
   - Upload using Playdate web interface or `pdc` tool

## Memory Optimizations

The following optimizations are enabled for Playdate:

- `REDUCE_MEMORY_USAGE`: Reduces internal buffer sizes
- `DISABLE_FANCY_THEMES`: Uses minimal GUI theme
- `DISABLE_COMMAND_LINE`: No CLI parsing
- `DISABLE_TEXT_CONSOLE`: No debug console
- Disabled engines except AGI and SCI
- Disabled MT32 emulation
- Disabled Nuked OPL
- Disabled Lua scripting
- Disabled TinyGL

## Testing

Tested with:
- Space Quest 1 (Mac System 3.0 version - 1-bit original graphics)

## Known Limitations

1. **Memory**: Complex or large games may exceed available RAM
2. **Performance**: Some games may require optimization for smooth gameplay
3. **Display**: Only monochrome output (no grayscale)
4. **Save system**: Uses Playdate's file system (not all save features may work)
5. **GUI**: ScummVM's GUI is adapted for 400×240 resolution

## Implementation Notes

### Graphics Pipeline

1. Game engines render to paletted (8-bit indexed color) surface
2. Palette values converted to grayscale using ITU-R BT.601 luminance formula
3. Floyd-Steinberg dithering applied to convert grayscale to 1-bit
4. Result written to Playdate's framebuffer
5. Screen updates triggered via `pd->graphics->markUpdatedRows()`

### Event Loop Integration

Playdate uses an event-driven architecture. The ScummVM main loop is integrated via:
- `eventHandler()`: Playdate's main entry point
- `updateCallback()`: Called every frame to process events and update game state
- Event polling in `OSystem_Playdate::pollEvent()` translates Playdate inputs to ScummVM events

### File System

Uses POSIX filesystem backend with Playdate's file API for save files and game data access within the `.pdx` bundle.

## Future Improvements

Potential enhancements for future versions:

1. **Audio Optimization**: Implement custom Playdate mixer for better performance
2. **Scaling**: Add proper integer scaling for different game resolutions
3. **Crank Integration**: More creative use of the crank for game-specific actions
4. **Overlay Rendering**: Improve GUI overlay rendering for monochrome display
5. **Performance**: Profile and optimize critical paths for Cortex-M7
6. **Additional Engines**: Evaluate other lightweight engines (SCUMM v1-5, etc.)

## Credits

- ScummVM Team: Original engine implementations
- Panic Inc.: Playdate SDK and hardware
- Port maintainer: Created Playdate backend and graphics adaptation

## License

ScummVM is licensed under GPL v3. See the COPYRIGHT file for details.
