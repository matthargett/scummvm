# Building ScummVM for Playdate

## Quick Start

### Prerequisites

1. **Playdate SDK 3.0.1**
   - Download from https://play.date/dev/
   - Install following platform-specific instructions

2. **ARM Embedded Toolchain**
   - **macOS**: Automatically installed with Playdate SDK
   - **Linux**: `sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi`
   - **Windows**: Download from https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads

3. **Environment Setup**
   ```bash
   export PLAYDATE_SDK_PATH=/path/to/PlaydateSDK
   ```

   Add to your shell profile (~/.bashrc, ~/.zshrc, etc.) for persistence.

### Build Process

```bash
# Navigate to ScummVM source directory
cd /path/to/scummvm

# Configure for Playdate
./configure --host=playdate

# Build
make -j$(nproc)

# Output will be scummvm.pdx
```

### What Gets Built

- `scummvm.pdx/` - Playdate application bundle
  - `pdex.bin` - ARM binary for Playdate device
  - `pdxinfo` - Application metadata
  - `themes/` - GUI themes (minimal)
  - `engine-data/` - Engine-specific data files

## Testing in Simulator

### macOS
```bash
open scummvm.pdx
```

### Windows
```bash
PlaydateSimulator.exe scummvm.pdx
```

### Linux
```bash
PlaydateSimulator scummvm.pdx
```

## Testing on Device

1. Connect Playdate via USB
2. Enable USB disk mode on Playdate
3. Copy `scummvm.pdx` to `/Games/` folder
4. Eject and restart Playdate

Or use the Playdate web interface at https://play.date/games/

## Adding Games

### Method 1: Include in PDX Bundle

```bash
# Create games directory in PDX
mkdir -p scummvm.pdx/games

# Copy game files (example: Space Quest 1)
cp -r /path/to/sq1/ scummvm.pdx/games/

# Rebuild PDX (if needed)
pdc scummvm.pdx scummvm.pdx
```

### Method 2: Sideload After Build

Games can also be added to the Playdate's data folder after installation. The exact mechanism depends on save file manager implementation.

## Troubleshooting

### Configure Fails with "PLAYDATE_SDK_PATH not set"
```bash
echo $PLAYDATE_SDK_PATH
# Should output path to SDK
# If empty, set it:
export PLAYDATE_SDK_PATH=/path/to/PlaydateSDK
```

### Missing ARM Toolchain
```bash
# Test if toolchain is available
arm-none-eabi-gcc --version

# If not found, install per platform instructions above
```

### Build Errors about Missing Headers
Ensure PLAYDATE_SDK_PATH points to the root of the SDK:
```bash
ls $PLAYDATE_SDK_PATH/C_API
# Should show: buildsupport  Examples  pd_api.h  ...
```

### Simulator Won't Launch PDX
- Ensure .pdx is a directory, not a file
- Check pdxinfo file exists and is valid
- Check Console output in Playdate Simulator for errors

## Performance Considerations

The Playdate uses an ARM Cortex-M7 @ 180 MHz with limited RAM. Some games may need optimization:

### For Better Performance:
- Use smaller/simpler games (AGI games recommended)
- Avoid high-resolution SCI games
- Keep saved games minimal
- Limit background processes on Playdate

### Memory Usage:
- AGI games: ~2-5 MB
- SCI games: ~5-20 MB (varies greatly)
- ScummVM core: ~1-2 MB

Total available RAM is approximately 16 MB, with some reserved for system.

## Advanced Build Options

### Debug Build
```bash
./configure --host=playdate --enable-debug --disable-optimizations
make clean && make
```

### Specific Game Only
Edit `configure` engine disable/enable section or use:
```bash
./configure --host=playdate --disable-all-engines --enable-engine=agi
```

### CMake Build (Alternative)
```bash
cd backends/platform/playdate
mkdir build && cd build
cmake .. -DPLAYDATE_SDK_PATH=$PLAYDATE_SDK_PATH
make
```

## Verification

After building, verify the PDX structure:
```bash
ls -R scummvm.pdx
# Expected output:
# scummvm.pdx:
# pdex.bin  pdxinfo  [themes/]  [engine-data/]
```

Check pdxinfo content:
```bash
cat scummvm.pdx/pdxinfo
# Should show:
# name=ScummVM
# author=ScummVM Team
# ...
```

## Next Steps

See [README.md](README.md) for:
- Supported games
- Control mapping
- Known limitations
- Implementation details
