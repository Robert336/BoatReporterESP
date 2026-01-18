# OTA Updates - Quick Start Guide

## 🚀 Get Started in 5 Minutes

### 1. Build and Upload Current Firmware (v1.0.0)

```bash
# Connect ESP32 via USB
pio run -e prod --target upload
```

### 2. Configure OTA Settings (Optional)

**Note:** Automatic updates are **enabled by default** and pre-configured to check `Robert336/BoatReporterESP`!

The device is ready to receive updates out of the box. Optionally, you can verify settings:

1. Connect to ESP32 (via web browser)
2. Go to **"Firmware Updates (OTA)"** page
3. Verify GitHub repository is set to: `Robert336/BoatReporterESP` ✅ (default)
4. Verify automatic checks are enabled ✅ (default: ON, 24 hour interval)
5. Verify automatic installation is enabled ✅ (default: ON)
6. Verify OTA notifications are enabled ✅ (default: ON)

### 3. Create Your First Release (v1.1.0)

#### Update version in code:

**`include/Version.h`:**
```cpp
#define FIRMWARE_VERSION "1.1.0"
```

**`platformio.ini`:**
```ini
[env:prod]
build_flags = 
    -D PRODUCTION_BUILD
    -D FIRMWARE_VERSION=\"1.1.0\"
```

#### Build firmware:

```bash
pio run -e prod
# Binary created at: .pio/build/prod/firmware.bin
```

#### Create GitHub Release:

1. Go to: `https://github.com/your-username/your-repo/releases/new`
2. **Tag**: `v1.1.0`
3. **Title**: `Version 1.1.0`
4. **Upload**: `.pio/build/prod/firmware.bin` (must be named exactly `firmware.bin`)
5. Click **"Publish release"**

### 4. Test Update

**Option A: Automatic Installation (Recommended)**
1. Wait for automatic check (or click "Check for Updates")
2. Device automatically downloads and installs update
3. You'll receive notifications throughout the process
4. Device reboots with new firmware automatically
5. You'll receive success notification!

**Option B: Manual Installation**
1. In ESP32 web interface, go to OTA page
2. Click **"Check for Updates"**
3. Should show: **"Update Available! Version 1.1.0"**
4. You'll receive notification on phone/Discord
5. Click **"Install Update"**
6. Device will download, install, and reboot
7. You'll receive success notification!

## 📱 Expected Notifications

**When update found:**
```
Boat Monitor: Firmware update available v1.0.0 → v1.1.0
```

**When installing:**
```
Boat Monitor: Starting firmware update from v1.0.0 to v1.1.0. 
Device may be offline for 1-2 minutes.
```

**After successful update:**
```
Boat Monitor: Firmware updated successfully! v1.0.0 → v1.1.0. System online.
```

## ⚙️ Configuration Options

| Setting | Recommended | Purpose |
|---------|-------------|---------|
| **GitHub Repo** | `owner/repo` | Where to check for releases |
| **Auto-check** | ✅ Enabled | Automatically check for updates |
| **Auto-install** | ✅ Enabled | Automatically install updates (perfect for remote boats!) |
| **Check Interval** | 24 hours | How often to check |
| **Notifications** | ✅ Enabled | Get SMS/Discord alerts |
| **Update Password** | Optional | Require password for manual installs |

## 🛡️ Safety Features

- ✅ **Automatic Rollback**: If update fails, device reverts to previous version
- ✅ **Automatic Installation**: Updates install without physical access (configurable)
- ✅ **Password Protection**: Optional password for manual installations
- ✅ **Notifications**: Always know what's happening with your device

## 📖 More Information

- **Detailed Testing**: See `OTA_TESTING_GUIDE.md`
- **Implementation Details**: See `OTA_IMPLEMENTATION_SUMMARY.md`
- **Troubleshooting**: Check the guides above

## 🆘 Quick Troubleshooting

| Problem | Solution |
|---------|----------|
| "No updates available" | Check GitHub tag format: `vX.Y.Z` |
| Download fails | Verify WiFi has internet access |
| Update fails | Check firmware built for correct board |
| Device won't boot | Wait 30s for auto-rollback, or flash via USB |

## ✅ Checklist

- [ ] Built and uploaded v1.0.0
- [ ] Configured GitHub repo in OTA settings
- [ ] Created v1.1.0 GitHub release with firmware.bin
- [ ] Successfully checked for updates
- [ ] Received update available notification
- [ ] Installed update and device rebooted
- [ ] Received success notification
- [ ] Verified new version in web interface

**That's it! You now have remote firmware updates working! 🎉**
