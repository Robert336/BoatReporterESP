# OTA Update Testing Guide

## Overview
This guide walks through testing the complete OTA (Over-The-Air) firmware update system for the Boat Monitor ESP32 project.

## Prerequisites

1. **Hardware**: ESP32 with WiFi connectivity
2. **GitHub Repository**: Public or private repo for hosting firmware releases
3. **PlatformIO**: Installed and configured
4. **WiFi Network**: ESP32 must be connected to WiFi with internet access

## Step 1: Initial Setup

### 1.1 Build Initial Firmware (v1.0.0)

```bash
# From project root
pio run -e prod

# Firmware binary will be at:
# .pio/build/prod/firmware.bin
```

### 1.2 Upload to ESP32

```bash
# Via USB
pio run -e prod --target upload

# Monitor serial output
pio device monitor -b 115200
```

### 1.3 Configure Device

1. Connect to WiFi AP: `ESP32-BoatMonitor-Setup` (password shown in serial output)
2. Navigate to `192.168.4.1` in browser
3. Configure WiFi network credentials
4. Set up SMS/Discord notifications (for OTA alerts)
5. Verify device connects to WiFi and shows IP address

## Step 2: Configure OTA Settings

### 2.1 Access OTA Settings Page

1. Connect to device IP address (or via AP mode)
2. Click "Firmware Updates (OTA)" button
3. Current version should show: `1.0.0`

### 2.2 Configure GitHub Repository

1. **GitHub Repository**: Enter `your-username/your-repo`
2. **GitHub Token** (if private repo): Enter personal access token
   - Create at: https://github.com/settings/tokens
   - Required scopes: `repo` (for private repos) or none (for public repos)
3. **Update Password** (optional): Set password for update authorization
4. **Automatic Updates**:
   - ☑ Enable automatic update checks
   - Check interval: 24 hours (or custom)
   - ☑ Enable OTA notifications
5. Click "Save Settings"

## Step 3: Create Test Release v1.1.0

### 3.1 Update Version Number

Edit `include/Version.h`:

```cpp
#define FIRMWARE_VERSION "1.1.0"
```

Edit `platformio.ini`:

```ini
[env:prod]
build_flags = 
    -D PRODUCTION_BUILD
    -D FIRMWARE_VERSION=\"1.1.0\"
```

### 3.2 Make a Small Test Change

Add a test log message in `src/main.cpp` setup():

```cpp
LOG_SETUP("[TEST] This is version 1.1.0 - OTA test update");
```

### 3.3 Build New Firmware

```bash
pio run -e prod
```

### 3.4 Create GitHub Release

1. Go to your GitHub repo: `https://github.com/your-username/your-repo/releases`
2. Click "Draft a new release"
3. **Tag version**: `v1.1.0`
4. **Release title**: `Version 1.1.0 - OTA Test`
5. **Description**: 
   ```
   Test release for OTA update functionality
   
   Changes:
   - Test OTA update system
   - Minor logging improvements
   ```
6. **Upload assets**: Click "Attach binaries" and upload `.pio/build/prod/firmware.bin`
7. **Important**: The file MUST be named exactly `firmware.bin`
8. Click "Publish release"

## Step 4: Test Manual Update Check

### 4.1 Trigger Manual Check

1. In browser, navigate to OTA settings page on ESP32
2. Click "Check for Updates" button
3. **Expected Results**:
   - Status shows "Checking GitHub for updates..."
   - After 2-5 seconds: "Update Available! Version 1.1.0 is ready to install"
   - If notifications enabled: Receive SMS/Discord notification:
     ```
     Boat Monitor: Firmware update available v1.0.0 → v1.1.0
     ```

### 4.2 Verify Serial Logs

Watch serial monitor for:

```
[OTA] Checking for updates from GitHub...
[OTA] Boat Monitor: Firmware update available v1.0.0 → v1.1.0
[OTA] Download URL: https://github.com/...
[OTA] Size: XXXXX bytes
```

## Step 5: Install Update

### 5.1 Start Update Process

1. In browser OTA page, enter update password (if configured)
2. Click "Install Update" button
3. Confirm in popup dialog
4. **Expected Results**:
   - Status shows "Downloading and installing update... Device will reboot shortly."
   - If notifications enabled: Receive SMS/Discord:
     ```
     Boat Monitor: Starting firmware update from v1.0.0 to v1.1.0. 
     Device may be offline for 1-2 minutes.
     ```

### 5.2 Monitor Update Progress

Watch serial output:

```
[OTA] Downloading firmware: XXXXX bytes
[OTA] Progress: 10%
[OTA] Progress: 20%
...
[OTA] Progress: 100%
[OTA] Download complete: XXXXX bytes
[OTA] Update successfully written to flash
[OTA] Update successful! Rebooting in 3 seconds...
```

### 5.3 Verify Successful Update

After reboot, check:

1. **Serial Output**:
   ```
   [SETUP] OTAManager initialized - version 1.1.0
   [SETUP] Firmware: v1.1.0
   [TEST] This is version 1.1.0 - OTA test update
   [OTA] New firmware validated successfully
   ```

2. **Notification Received**:
   ```
   Boat Monitor: Firmware updated successfully! v1.0.0 → v1.1.0. System online.
   ```

3. **Web Interface**: OTA page shows current version: `1.1.0`

## Step 6: Test Automatic Update Checking

### 6.1 Create v1.2.0 Release

1. Update version to `1.2.0` in code
2. Build and create GitHub release (same process as Step 3)

### 6.2 Verify Automatic Check

- Wait for automatic check interval (or modify to 1 hour for faster testing)
- Watch serial logs for automatic check
- **Expected**: Notification received about v1.2.0 availability
- Update remains pending until user manually installs

## Step 7: Test Rollback Scenario (Advanced)

### 7.1 Create Intentionally Bad Firmware

Create v1.3.0 that crashes immediately:

```cpp
void setup() {
    Serial.begin(115200);
    // Intentional crash for testing
    ESP.restart(); // Immediate restart loop
}
```

### 7.2 Release and Install

1. Build and release v1.3.0
2. Install via OTA
3. **Expected Behavior**:
   - Device boots into v1.3.0
   - Crashes and reboots (3 times)
   - ESP32 bootloader automatically rolls back to v1.2.0
   - On successful rollback boot, receive notification:
     ```
     Boat Monitor: New firmware v1.3.0 failed to boot. 
     Rolled back to v1.2.0. System stable.
     ```

## Step 8: Test Error Scenarios

### 8.1 Invalid GitHub Repo

1. Set GitHub repo to non-existent repo
2. Click "Check for Updates"
3. **Expected**: Error message shown in web interface

### 8.2 No Internet Connection

1. Disconnect WiFi router from internet
2. Try to check for updates
3. **Expected**: "GitHub API request failed" error

### 8.3 Wrong Update Password

1. Set update password in settings
2. Try to install update with wrong password
3. **Expected**: "Invalid password" error, update blocked

### 8.4 No firmware.bin in Release

1. Create release without uploading firmware.bin
2. Check for updates
3. **Expected**: "No firmware.bin found in release" error

## Verification Checklist

- [ ] Manual update check works
- [ ] Update notifications sent (SMS/Discord)
- [ ] Firmware download and installation successful
- [ ] Device reboots and validates new firmware
- [ ] Success notification received
- [ ] Automatic update checks work on schedule
- [ ] Update password protection works (if enabled)
- [ ] Rollback works for bad firmware
- [ ] Rollback notification received
- [ ] Error handling works for various failure scenarios
- [ ] OTA web interface displays correct status
- [ ] Version information accurate throughout process

## Troubleshooting

### Update Check Fails

- **Check**: WiFi connected and has internet
- **Check**: GitHub repo name correct (owner/repo format)
- **Check**: Release tag format is `vX.Y.Z` (e.g., `v1.1.0`)
- **Check**: Asset file named exactly `firmware.bin`

### Download Fails

- **Check**: File size reasonable (<2MB for 4MB flash)
- **Check**: GitHub asset is public or token provided
- **Check**: Sufficient free heap memory (run `/ota/status` to check)

### Installation Fails

- **Check**: Firmware built for correct board (upesy_wroom)
- **Check**: Partition scheme supports OTA (default.csv)
- **Check**: Serial logs for specific error messages

### No Notifications

- **Check**: SMS/Discord credentials configured
- **Check**: OTA notifications enabled in settings
- **Check**: WiFi connected (notifications require internet)

### Device Won't Boot After Update

- **Wait**: ESP32 will auto-rollback after 3 failed boots (~30 seconds)
- **Manual Recovery**: Flash via USB if rollback fails
- **Prevention**: Always test firmware via USB before OTA release

## Best Practices

1. **Always test via USB first** before creating OTA release
2. **Use semantic versioning** (major.minor.patch)
3. **Keep release notes** describing changes
4. **Test rollback** periodically to ensure safety mechanism works
5. **Monitor serial logs** during first few OTA updates
6. **Verify checksums** in production (future enhancement)
7. **Schedule updates** during low-usage times
8. **Keep backup firmware** .bin files for all released versions

## Next Steps

After successful testing:

1. Set realistic auto-check interval (24 hours recommended)
2. Document your release process for team members
3. Consider adding MD5 checksum verification
4. Set up automated build/release pipeline
5. Monitor update success rates across fleet

## Support

For issues or questions:
- Check serial logs for detailed error messages
- Verify GitHub release format matches requirements
- Review ESP32 partition table configuration
- Ensure sufficient free memory before updates
