# ESP32 Boat Monitor - Development UI

This folder contains a standalone development environment for the web interface. It allows you to develop and test the UI rapidly without needing to compile and upload to the ESP32 every time you make a change.

## What's Inside

- **index.html** - Main WiFi configuration page
- **debug.html** - Debug and calibration interface
- **mock-server.js** - Node.js server that mimics the ESP32's API endpoints
- **package.json** - Node.js dependencies

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
ESP32 Boat Monitor - Development Mock Server
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
- **Main page**: http://localhost:3000/
- **Debug page**: http://localhost:3000/debug.html

### 5. Develop!

Now you can:
- Edit `index.html` or `debug.html` in your code editor
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

All the ESP32 endpoints are mocked:

**Sensor & Calibration:**
- `GET /read` - Current sensor reading (simulated)
- `GET /calibration` - Calibration settings
- `POST /calibrate/zero` - Set zero point
- `POST /calibrate/point2` - Set second calibration point

**Emergency Settings:**
- `GET /emergency-settings` - Current emergency thresholds
- `POST /calibration/emergency-level` - Set Tier 1 threshold
- `POST /emergency/urgent-level` - Set Tier 2 threshold
- `POST /emergency/test-pin` - Test emergency pin

**Notifications:**
- `GET /notifications` - Current notification settings
- `POST /notifications/phone` - Save phone number
- `POST /notifications/discord` - Save Discord webhook
- `POST /notifications/emergency-freq` - Set notification frequency
- `POST /notifications/test/sms` - Test SMS (simulated)
- `POST /notifications/test/discord` - Test Discord (simulated)

**WiFi:**
- `GET /status` - WiFi connection status
- `POST /config` - Save WiFi credentials

## Development Workflow

### Making UI Changes

1. **Edit HTML/CSS/JavaScript** in `index.html` or `debug.html`
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

When you're happy with your UI changes:

### Option A: Manual Copy-Paste

1. Open `ConfigServer.cpp` in your editor
2. Find the `getConfigPage()` function (around line 236)
3. Replace the content inside `R"HTML(...HTML")` with your `index.html` content
4. Find the `getDebugPage()` function (around line 796)
5. Replace the content inside `R"HTML(...HTML")` with your `debug.html` content
6. Compile and upload to ESP32

### Option B: Build Script (Future Enhancement)

You could create a Python or Node script to automatically convert the HTML files to C++ raw strings. This would go in the project root (not in dev-ui).

Example Python script (not included, but you could create):

```python
def html_to_cpp(html_file, function_name):
    with open(html_file, 'r') as f:
        html = f.read()
    
    print(f"String {function_name}() {{")
    print(f'    String html = R"HTML(')
    print(html)
    print('    )HTML";')
    print('    return html;')
    print('}')

# Usage:
# python build_html.py
```

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

