# CI Testing for Playdate Port

## GitHub Actions Workflow

A GitHub Actions workflow has been added to automatically build and test the Playdate port on every push to branches matching `claude/scummvm-playdate-agi-sci-*`.

### Workflow Location
`.github/workflows/playdate.yml`

### What It Does

1. **Downloads and Caches Playdate SDK 3.0.1**
   - Downloads from official Panic CDN
   - Caches SDK to speed up future builds
   - Extracts to `~/PlaydateSDK`

2. **Installs ARM Toolchain**
   - Installs `gcc-arm-none-eabi` and related tools
   - Required for cross-compilation to ARM Cortex-M7

3. **Configures ScummVM**
   - Runs `./configure --host=playdate`
   - Sets up build environment with Playdate-specific flags

4. **Builds ScummVM**
   - Runs `make -j$(nproc)`
   - Captures build log as artifact

5. **Checks Build Output**
   - Verifies PDX bundle or executable was created
   - Uploads artifacts for inspection

### Viewing Results

1. Go to your GitHub repository
2. Click "Actions" tab
3. Find the latest workflow run for your branch
4. Click on the run to see details
5. Download build artifacts if available

### Current Status

The workflow is now active and will run on every push. The first run will:
- Identify any compilation errors
- Show missing dependencies
- Reveal build system issues

## Expected Build Issues

Based on the implementation, here are likely issues that may occur:

### 1. Playdate SDK Download
**Issue**: SDK download URL may change or require authentication
**Fix**: Update workflow with correct SDK URL for Linux

### 2. Missing Headers/Libraries
**Potential errors**:
- `pd_api.h: No such file or directory`
- Undefined references to Playdate API functions

**Fix**: Ensure `PLAYDATE_SDK_PATH` is correctly set and SDK structure matches expectations

### 3. Compilation Errors
**Potential**:
- Type mismatches with Playdate API
- Missing ScummVM includes
- C++ vs C linkage issues

**Fix**: Address each error individually, likely needs adjustments to extern "C" blocks

### 4. Linker Errors
**Potential**:
- Missing Playdate library files
- ARM toolchain incompatibilities
- Undefined symbols

**Fix**: May need to adjust LDFLAGS in configure or playdate.mk

## Local Testing (Without Playdate SDK)

You can test compilation of most code without the full SDK:

```bash
# Test configure script
./configure --host=playdate 2>&1 | head -50

# This will fail at SDK check, but validates configure logic
```

## Next Steps After CI Passes

Once the build completes successfully:

1. **Download PDX Artifact**
   - Get `scummvm.pdx` from GitHub Actions artifacts
   - Extract to local machine

2. **Test in Simulator**
   ```bash
   # macOS
   open scummvm.pdx

   # Windows
   PlaydateSimulator.exe scummvm.pdx

   # Linux
   PlaydateSimulator scummvm.pdx
   ```

3. **Add Game Data**
   - Create `scummvm.pdx/games/` directory
   - Add Space Quest 1 or other AGI/SCI game files
   - Rebuild PDX if needed

4. **Test on Device**
   - Upload to Playdate via USB
   - Test actual performance and controls
   - Monitor battery usage and temperature

## Iterating on Failures

When CI fails (it likely will on first run):

1. **Check Build Log**
   - Download build-log artifact
   - Find first error (often subsequent errors are cascading)

2. **Fix Locally If Possible**
   - Make changes
   - Test compile if you have SDK
   - Commit and push

3. **Let CI Validate**
   - CI will rebuild automatically
   - Iterate until clean build

## Current CI Limitations

The current workflow does NOT:
- Run the Playdate simulator (requires GUI/X11)
- Execute the app to check for crashes
- Perform runtime testing

### Future Enhancements

To add simulator testing:

1. Install Xvfb (virtual framebuffer) for headless testing
2. Run Playdate simulator with Xvfb
3. Use expect/script to automate interaction
4. Check logs for crashes or errors

Example enhancement:
```yaml
- name: Test in Simulator
  run: |
    # Install virtual display
    sudo apt-get install -y xvfb

    # Run simulator headless
    xvfb-run --auto-servernum ~/PlaydateSDK/bin/PlaydateSimulator scummvm.pdx &

    # Wait for startup
    sleep 10

    # Check if still running
    pgrep PlaydateSimulator || exit 1

    # Capture logs
    # Kill simulator
    pkill PlaydateSimulator
```

## Monitoring Build Health

Key metrics to watch:

- **Build Time**: Should be < 10 minutes after cache warms up
- **Binary Size**: PDX should be reasonable (< 50MB ideally)
- **Warning Count**: Minimize compiler warnings
- **Cache Hit Rate**: SDK cache should hit after first build

## Troubleshooting

### Cache Issues
If SDK cache is stale or corrupted:
1. Go to repository Settings > Actions > Caches
2. Delete the `playdate-sdk-3.0.1-linux` cache
3. Re-run workflow

### ARM Toolchain Mismatch
If ARM compiler version issues occur:
- Try specific version: `gcc-arm-none-eabi-10-2020-q4-major`
- Or use Playdate's bundled toolchain if available

### Configure Failures
- Check environment variables are set correctly
- Verify host detection logic in configure
- Test locally with verbose output: `./configure --host=playdate --verbose`

---

**Note**: This CI setup is for build verification only. Full QA requires actual Playdate hardware testing.
