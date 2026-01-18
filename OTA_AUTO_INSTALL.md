# Automatic OTA Installation Feature

## Overview

The OTA system now supports **fully automatic firmware installation** without requiring any user intervention. This is perfect for remote boat monitoring where physical access is difficult or impossible.

**Automatic updates are ENABLED BY DEFAULT** - your device will automatically check for and install updates once you configure your GitHub repository.

## How It Works

When automatic installation is enabled:

1. **Device checks for updates** on your configured schedule (e.g., every 24 hours)
2. **Update detected**: Device sends you a notification about available update
3. **Automatic download**: Firmware is downloaded from GitHub in the background
4. **Automatic installation**: Update is installed to the OTA partition
5. **Automatic reboot**: Device restarts with new firmware
6. **Success notification**: You receive confirmation that update completed successfully

**Total time offline: 1-2 minutes**

## Configuration

### Step 1: Configure GitHub Repository

**Note:** Automatic updates are **enabled by default**. You just need to configure your GitHub repository.

1. Open your ESP32 web interface
2. Navigate to **"Firmware Updates (OTA)"**
3. Enter your GitHub repository: `your-username/your-repo`
4. (Optional) Verify **"Enable automatic update checks"** is checked ✅ (default: ON)
5. (Optional) Verify **"Enable automatic update installation"** is checked ✅ (default: ON)
6. (Optional) Adjust check interval (default: 24 hours)
7. Click **"Save Settings"**

### Step 2: Configure Notifications

To stay informed about updates:

1. Ensure **"Enable OTA notifications"** is checked ✅
2. Configure SMS (Twilio) and/or Discord notifications
3. You'll receive messages at each stage:
   - Update available
   - Installation starting
   - Installation successful
   - Installation failed (with rollback notification)

## Notifications You'll Receive

### 1. Update Available
```
Boat Monitor: Firmware update available v1.0.0 → v1.1.0
```

### 2. Installation Starting (Auto-Install)
```
Boat Monitor: Starting firmware update from v1.0.0 to v1.1.0. 
Device may be offline for 1-2 minutes.
```

### 3. Success
```
Boat Monitor: Firmware updated successfully! v1.0.0 → v1.1.0. System online.
```

### 4. Failure (with Rollback)
```
Boat Monitor: Firmware update FAILED - {reason}. Still running v1.0.0.
```
or
```
Boat Monitor: New firmware v1.1.0 failed to boot. Rolled back to v1.0.0. System stable.
```

## Safety Features

### Automatic Rollback Protection

If a new firmware fails to boot properly:
- ESP32 bootloader automatically detects boot failure
- After 3 failed boot attempts, device rolls back to previous working firmware
- You receive notification about the rollback
- Your device stays operational with the old version

### No Password Required

When auto-install is enabled, updates proceed without password verification. This is intentional for unattended operation. If you need security:

- Use a **private GitHub repository** with a token
- Only you can publish releases that will be installed
- The update password is only used for manual installations via web interface

## Use Cases

### Perfect For:
- ✅ Remote boat monitoring (limited physical access)
- ✅ Production deployments where updates must be seamless
- ✅ Critical bug fixes that need to deploy quickly
- ✅ Regular maintenance updates

### Consider Manual Mode If:
- ⚠️ Testing experimental features
- ⚠️ Update might require configuration changes
- ⚠️ Update includes breaking changes
- ⚠️ You want to review release notes first

## Switching Between Modes

You can enable/disable auto-install at any time:

**To Enable Auto-Install:**
1. Go to OTA page
2. Check ✅ "Enable automatic update installation"
3. Click "Save Settings"

**To Disable Auto-Install:**
1. Go to OTA page
2. Uncheck ☐ "Enable automatic update installation"
3. Click "Save Settings"
4. Updates will still be checked automatically, but you'll need to click "Install Update" manually

**Note:** Auto-install is enabled by default to ensure your remote device stays up-to-date without physical access.

## Best Practices

### 1. Test Updates First
- If possible, test new firmware on a bench unit first
- Once verified, publish to GitHub for automatic deployment

### 2. Schedule Updates Wisely
- Set check interval during low-activity periods
- Device will be offline for 1-2 minutes during update

### 3. Monitor Notifications
- Keep your phone/Discord active to receive update notifications
- If you don't receive success notification, check device status

### 4. Use Semantic Versioning
- Follow `vX.Y.Z` format for GitHub release tags
- Device only installs updates with higher version numbers
- Example: `v1.0.0` → `v1.0.1` → `v1.1.0` → `v2.0.0`

### 5. Include Rollback Information
- Add rollback instructions in release notes
- If update causes issues, you can always flash previous firmware via USB

## Configuration Summary

| Setting | Recommended Value | Purpose |
|---------|------------------|---------|
| **Auto-check** | ✅ Enabled | Automatically check GitHub for updates |
| **Auto-install** | ✅ Enabled | Install updates without user action |
| **Check Interval** | 24 hours | Balance between responsiveness and bandwidth |
| **Notifications** | ✅ Enabled | Stay informed about update status |
| **GitHub Repo** | `owner/repo` | Your firmware repository |
| **GitHub Token** | Set if private | Access token for private repositories |
| **Update Password** | Optional | Only affects manual installations |

## Troubleshooting

### Auto-Install Not Working?

1. **Check auto-install is enabled**
   - Go to OTA page, verify checkbox is checked

2. **Verify auto-check is enabled**
   - Auto-install requires auto-check to be enabled too

3. **Check last check time**
   - Shows when device last checked for updates
   - May need to wait for next scheduled check

4. **Force manual check**
   - Click "Check for Updates" to check immediately
   - If auto-install is enabled, it will install automatically

5. **Check notifications**
   - Review SMS/Discord for any error messages
   - Errors will show installation failure reasons

### Update Failed?

- Device automatically rolls back to previous version
- No action required from you
- Check logs or error messages in notifications
- Consider disabling auto-install and reviewing the update manually

## Implementation Details

The auto-install feature adds these changes to the OTA system:

1. **New configuration field**: `autoInstallEnabled` (stored in NVS)
2. **Enhanced loop logic**: Automatically calls `startUpdate()` when update is detected
3. **Web interface**: New checkbox for enabling/disabling feature
4. **API endpoint**: Settings endpoint accepts `auto_install` parameter

All existing safety features (rollback, notifications, version checking) remain intact.

## Summary

Automatic OTA installation provides:
- ✅ **Zero-touch updates** for remote devices
- ✅ **Automatic rollback** if updates fail
- ✅ **Full notification** at every step
- ✅ **Enabled by default** - works out of the box once GitHub repo is configured
- ✅ **Configurable** - can be disabled anytime if manual control is preferred
- ✅ **Safe** - only installs from your GitHub repository

Perfect for your boat monitoring system where physical access is limited!
