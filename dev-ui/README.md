# ESP32 BilgeRise - Development UI

This folder contains a standalone development environment for the web interface. It allows you to develop and test the UI rapidly without needing to compile and upload to the ESP32 every time you make a change.

## What's Inside

- **index.html** — Main dashboard page
- **wifi-config.html** — WiFi network configuration page
- **notifications.html** — Notification settings (SMS, Discord, MQTT)
- **settings.html** — Settings hub page
- **debug.html** — Debug and calibration interface
- **mock-server.js** — Node.js server that mimics all ESP32 API endpoints
- **package.json** — Node.js dependencies

## Quick Start

### 1. Install Node.js

If you don't have Node.js installed, download it from [nodejs.org](https://nodejs.org/)

### 2. Install Dependencies

Open a terminal in the `dev-ui` folder and run:

```bash
npm install
```

This will install Express.js and CORS packages needed for the mock server.

### 3. Start the Mock Server

```bash
npm start
```

You should see output like:

```
============================================================
ESP32 BilgeRise - Development Mock Server
============================================================
Server running at: http://localhost:3000

Available pages:
  Main Config:  http://localhost:3000/
  Debug Page:   http://localhost:3000/debug.html
...
============================================================
```

### 4. Open in Browser

Navigate to:
- **Main dashboard**: http://localhost:3000/
- **WiFi config**: http://localhost:3000/wifi-config
- **Notifications**: http://localhost:3000/notifications-page
- **Settings**: http://localhost:3000/settings
- **Debug page**: http://localhost:3000/debug

Note: page URLs match the routes served by the real ESP32 — no `.html` extension.

### 5. Develop!

Now you can:
- Edit any of the HTML files in `dev-ui/` in your code editor
- Save the file
- Refresh your browser to see changes immediately
- No compilation or ESP32 upload needed!

## How It Works

### Mock Server

The `mock-server.js` creates a local web server that:
- Serves the HTML files
- Responds to all the same API endpoints as your ESP32
- Returns realistic mock data
- Simulates sensor readings that change over time (updates every 2 seconds)
- Maintains state (calibration, settings) during your session

### API Endpoints Implemented

All ESP32 endpoints are mocked. These match the routes registered in `src/ConfigServer.cpp`:

**Pages (GET — served by mock as HTML files):**
- `GET /` — Main dashboard (`index.html`)
- `GET /wifi-config` — WiFi config page
- `GET /notifications-page` — Notifications page
- `GET /settings` — Settings hub
- `GET /debug` — Debug & calibration page

**Init (merged JSON for fast page load):**
- `GET /init` — Dashboard init data; includes `sensor.rate_cm_30min` (conditionally — omitted until 5+ minutes of readings exist, i.e. at least 2 snapshots in the rate buffer)
- `GET /settings/init` — Settings init data
- `GET /debug/init` — Combined load for debug page: `{ reading: {...}, calibration: {...} }`

**Sensor & Calibration:**
- `GET /read` — Current sensor reading (simulated)
- `GET /calibration` — Calibration settings
- `POST /calibrate/zero` — Set zero point
- `POST /calibrate/point2` — Set second calibration point

**Emergency Settings:**
- `GET /emergency-settings` — Current thresholds
- `POST /calibration/emergency-level` — Set Tier 1 threshold
- `POST /emergency/urgent-level` — Set Tier 2 threshold
- `POST /emergency/test-pin` — Test emergency pin

**Notifications:**
- `GET /notifications` — Current notification settings
- `POST /notifications/phone` — Save phone number
- `POST /notifications/discord` — Save Discord webhook
- `POST /notifications/mqtt` — Configure MQTT broker
- `POST /notifications/emergency-freq` — Set notification frequency
- `POST /notifications/test/sms` — Test SMS (simulated)
- `POST /notifications/test/discord` — Test Discord (simulated)
- `POST /notifications/test/mqtt` — Test MQTT (simulated)

**WiFi:**
- `GET /status` — WiFi connection status
- `GET /wifi/networks` — List stored networks
- `POST /config` — Save WiFi credentials
- `POST /wifi/remove` — Remove a stored network

**OTA (not in mock-server — see `src/html/ota.html` and `src/ConfigServer.cpp`):**
- `GET /ota-settings` — OTA settings page
- `GET /ota/status` — Current OTA state JSON
- `GET /ota/check` — Trigger update check
- `POST /ota/update` — Start firmware install
- `POST /ota/settings` — Save OTA configuration

## Development Workflow

### Making UI Changes

1. **Edit HTML/CSS/JavaScript** in any of the `dev-ui/*.html` files (or `src/html/ota.html`)
2. **Save** the file
3. **Refresh** your browser (F5 or Ctrl+R)
4. **See changes immediately**

### Testing API Interactions

All buttons and forms work just like on the real ESP32:
- Click "Set Zero Point" → updates mock calibration
- Test SMS/Discord → simulates sending (always succeeds)
- Change emergency levels → validates and saves to mock state
- Sensor readings update automatically every 2 seconds

### Using Browser DevTools

You can now use full browser developer tools:
- **Console** - See JavaScript errors and debug logs
- **Network tab** - Inspect API requests/responses
- **Elements tab** - Experiment with CSS in real-time
- **Application tab** - Check localStorage if you add it

## Deploying Changes to ESP32

When you're happy with your UI changes, just build the firmware — the pipeline handles the rest automatically.

### How the Build Pipeline Works

`scripts/compress_html.py` runs as a PlatformIO pre-script (via `extra_scripts` in `platformio.ini`) before every compile. It:

1. Reads each HTML file from `dev-ui/` and `src/html/` (for `ota.html`)
2. Gzip-compresses each file at level 9
3. Writes them as `const uint8_t` arrays into `src/compressed_pages.h`
4. `ConfigServer.cpp` reads these arrays and serves them with `Content-Encoding: gzip`

### Deploying

```bash
# Edit dev-ui/*.html, then:
pio run -e prod --target upload   # compress, compile, upload in one step
```

No manual copy-paste or conversion needed. The HTML files in `dev-ui/` are the single source of truth for all pages except `ota.html` which lives in `src/html/`.

### Which file to edit?

| Page | Source file |
|------|-------------|
| Main dashboard | `dev-ui/index.html` |
| WiFi config | `dev-ui/wifi-config.html` |
| Notifications | `dev-ui/notifications.html` |
| Settings hub | `dev-ui/settings.html` |
| Debug & calibration | `dev-ui/debug.html` |
| OTA settings | `src/html/ota.html` |

## Customizing the Mock Data

You can modify `mock-server.js` to change the mock data:

**Initial sensor reading:**
```javascript
currentMillivolts: 1234.56,  // Change this
currentLevel_cm: 25.3,        // Change this
```


**Calibration values:**
```javascript
zeroPoint_mv: 500,            // Change this
secondPoint_mv: 2500,         // Change this
secondPoint_cm: 50.0,         // Change this
```

**Emergency thresholds:**
```javascript
emergencyWaterLevel_cm: 30.0,
urgentEmergencyWaterLevel_cm: 50.0,
```

**Simulate sensor errors:**
```javascript
sensorValid: false,  // Set to false to test error handling
```

## Tips & Tricks

### Live Sensor Simulation

The mock server simulates a live sensor by adding random variation every 2 seconds. Watch the debug page and you'll see the millivolts value change realistically.

### Testing Edge Cases

Modify the mock server to test edge cases:
- Set sensor reading near emergency threshold
- Set `sensorValid: false` to test error handling
- Remove phone number to test "not configured" state
- Return errors from endpoints to test error handling

### Multiple Browser Windows

Open both pages side-by-side:
- Left window: Main config page
- Right window: Debug page with auto-updating sensor readings

### Mobile Testing

The pages are responsive. Test on mobile by:
1. Find your computer's IP address
2. Make sure your phone is on the same network
3. Change `localhost` to your IP in the browser URL
4. Or use Chrome DevTools device emulation

## Troubleshooting

**"Cannot GET /"**
- Make sure you're in the `dev-ui` folder when running `npm start`
- Check that all files are present

**"Module not found: express"**
- Run `npm install` first

**Changes not showing**
- Hard refresh: Ctrl+Shift+R (or Cmd+Shift+R on Mac)
- Check browser console for errors
- Make sure you saved the file

**API calls failing**
- Check the terminal where mock-server is running for error messages
- Check browser Network tab to see the actual request/response
- Make sure the mock server is still running

## Future Enhancements

Ideas for improving this development setup:

1. **Live Reload** - Automatically refresh browser when files change (using `nodemon` or `browser-sync`)
2. **Build Script** - Automatically convert HTML to C++ strings
3. **CSS Extraction** - Move CSS to separate file for easier editing
4. **JavaScript Modules** - Split large JavaScript into separate files
5. **Hot Module Replacement** - Update page without full reload
6. **TypeScript** - Add type safety to JavaScript code
7. **Tailwind CSS** - Use utility-first CSS framework

## Need Help?

If you run into issues:
1. Check the terminal output where the mock server is running
2. Check the browser console (F12) for JavaScript errors
3. Try stopping the server (Ctrl+C) and starting again
4. Delete `node_modules` folder and run `npm install` again

