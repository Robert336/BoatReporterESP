# BoatReporterESP — User Guide

This guide is for the person who installed the device in a boat, and for the boat owner who'll be receiving the alerts. It covers day-to-day operation: what the lights mean, how to read the dashboard, how to silence an alert, and what to do when something looks wrong.

The installer reads the whole thing once during setup. The boat owner reads only the last page.

---

## What you have

A small electronics box installed in the bilge. It measures how much water is down there, and sends a text message and a Discord alert when the level crosses a threshold you set. It also streams live readings to a web dashboard.

**What's already been done for you:**

- The firmware is installed and configured.
- The factory dlso efaults are: warning at 30 cm, urgent alert at 50 cm, alerts every 15 minutes during an active flood.
- The sensor, wiring, and enclosure are assembled.

**What you still need to do, once:**

1. Connect the device to the boat's WiFi (or a nearby network that reaches the boat).
2. Enter your phone number and Discord webhook.
3. Set the alert thresholds to match this boat.
4. (Optional) Run the two-point calibration if the reading looks off.

Everything after that is automatic. The device runs in the background.

---

## First power-on: what to expect in the first 30 seconds

When you connect 12 V power to the box:

1. Within about 2 seconds, the blue LED comes on.
2. If the device has never been configured, the LED begins to **blink slowly** and the box starts broadcasting a WiFi network called `ESP32-BilgeRise-Setup`.
3. If the device has been configured before, it tries to connect to the saved WiFi. The LED goes **off** when connected, or **double-blinks** if it cannot reach the network.

You have about 5 minutes from power-on to connect to the setup network. If you don't, the device keeps running and the setup network stays available until you press the button.

---

## Connecting to the device

**From a phone or laptop, while sitting near the boat:**

1. On your phone, open Settings → WiFi.
2. Look for the network `ESP32-BilgeRise-Setup`. Connect to it.
3. When prompted for a password: the password is unique to each device. On most installations, it's printed on a sticker on the side of the enclosure. If there is no sticker, the password is in the serial log of the device on first boot (a USB cable and a serial-monitor program at 115200 baud will show it). Contact whoever set the device up if neither is available.
4. Once connected, your phone should automatically open the device's setup page. If it doesn't, open a browser and go to `http://192.168.4.1` or type any domain like `http://this.com`. Make sure it's `http` and not `https`.

The setup page is a small website served by the device itself. It does not need an internet connection. It does not use HTTPS (it cannot — it has no certificate), so your browser may show a warning. The connection is between your phone and the box a few feet away, so the warning is fine to ignore.

From the setup page, you can:

- Connect the box to the boat's WiFi.
- Enter your phone number for SMS alerts if you are using Twillio.
- Enter the Discord webhook URL, or setup Twillio, or a custom webhook.
- Set the two alert thresholds.
- Calibrate the water-level reading.
- See the live water level.
---

## The marina WiFi problem (and the fix)

Marina guest networks usually have a **captive portal** — a sign-in page that opens in your browser the first time you connect. The device cannot sign in to that page for you. It has no browser, no screen, and no way to type a username and password into a web form.

When the device is connected to a captive-portal network, it will:

- Associate with the WiFi and show as connected.
- Detect that it cannot reach the internet, and **defer outbound alerts** (they would only fail anyway).
- Show a **"portal blocked"** banner on the WiFi page in the setup interface.
- Re-check every 2 minutes. As soon as the network lets real traffic through, the banner clears and alerts resume.

**Two ways to fix this:**

1. **Set a custom MAC address.** Most marinas authenticate devices by their MAC address (the unique hardware ID of the WiFi radio). If a device with a different MAC is already authenticated on the marina network — your phone or laptop, for example — enter that MAC into the **Custom MAC** field on the WiFi page. The device will then present itself as that already-trusted device.

2. **Ask the marina to whitelist the device's MAC.** The device's current MAC is shown on the same WiFi page, near the top. Forward it to the marina's network administrator and ask them to allow it. Once the device is on the allowed list, the captive portal is bypassed.

Option 1 is faster. Option 2 is more durable.

If the device can't reach the boat's WiFi at all (out of range), it has no way to send alerts. The only fix is to move the device closer to the access point, or to use a network extender.

---

## The web interface

The setup page has a few sections:

- **Dashboard** — the current water level, the system state, and the recent alert history. This is the page you'll look at most often when you're at the boat.
- **WiFi** — connect to a network, set a custom MAC, see the current signal strength and connection state.
- **Notifications** — the phone number for SMS, the Discord webhook URL, the alert repeat interval, and the MQTT broker (if used).
- **Settings** — the two alert thresholds, the units (cm / in), the timezone, and a few less-used toggles.
- **Calibration** — the two-point water-level calibration. Use this when the reading looks off.
- **Updates** — firmware updates over the air.

You don't need to visit any of these after the first setup unless something changes.

---

## The status light

The blue LED in the box tells you what the device is doing.

| What you see | What it means | What to do |
|---|---|---|
| Off | Normal. Water level is OK, WiFi is connected, nothing to report. | Nothing. This is the idle state. |
| Double-blink (two quick flashes, pause, repeat) | Normal, but the device has lost the WiFi connection. Alerts cannot be sent right now. | Check that the boat's network is up. If the device is far from the access point, move it closer or use a WiFi extender. |
| Slow blink (about once per second) | Setup mode. The device is broadcasting its WiFi network. | Connect to the network and open the setup page. |
| Fast blink (about 3 times per second) | Sensor error. The device cannot read the water level. | Open the enclosure and check the sensor wiring. See the troubleshooting section below. |
| Off during a flood | The device is sending alerts. The LED deliberately goes off during a flood so the alerts get your attention, not the light. | Wait for the alert. Silence it with a 5-second button hold if you need to. |

If the LED is doing something not in this list, power the device off and back on. If it still doesn't match, contact support.

---

## The button

The button is on the inside of the enclosure, near the LEDs.

**Single press:** the device enters setup mode. The blue LED starts to slow-blink, and the `ESP32-BilgeRise-Setup` WiFi network becomes available. Use this whenever you need to change a setting.

The single press is also a useful first move whenever something looks wrong: it opens the setup page, where you can see the live reading, the current state, and the connection status.

**5-second hold (during an active flood only):** the device silences its alerts. The water-level reading is still being taken, and the device is still watching for the level to drop, but no more texts or Discord messages will go out until the water level returns to normal. When the level drops, silence is automatically cleared — next time there's a flood, the alerts come back on.

This is the one you'll use when you're testing the device and you don't want the customer's phone to light up with test alerts.

**Short press during a flood:** ignored. The device assumes you may have bumped the button by accident and won't silently enter setup mode in the middle of an emergency.

---

## Calibrating the reading

The water-level reading is calibrated at the factory, but every boat's bilge is a little different. If the dashboard shows a reading that doesn't match the actual water level, calibrate.

1. Press the button once to enter setup mode.
2. Connect to the `ESP32-BilgeRise-Setup` network and open the setup page.
3. Go to the **Calibration** tab.
4. **Zero point:** with the bilge dry (or with the sensor at the lowest water level it will normally see), read the millivolt value shown on the page. Enter it and save.
5. **Second point:** with the bilge filled to a known depth — even 10 cm of water in a bucket is enough — read the millivolt value, enter it, and enter the actual depth in centimetres.
6. Save.

The device now reads correctly across that range.

You only need to do this once, unless the sensor is moved or replaced.

---

## Setting the alert thresholds

By default, the device sends a warning at **30 cm** and an urgent alert at **50 cm**. You may want to change them.

**The trade-off:** set the threshold too low and you'll get texts every time it rains or there's a bit of condensation. Set it too high and the alert arrives after water has already started to damage things.

A good rule of thumb: the warning threshold should be a few centimetres above the highest normal water level in this boat's bilge, and the urgent threshold should be the level at which damage starts.

Change the thresholds in the **Settings** tab of the setup page.

---

## "What's normal" at idle

A correctly installed device in a dry bilge should show:

- **Water level:** 0 cm, with small variations of less than 1 cm.
- **State:** NORMAL.
- **Status LED:** off.
- **Last alert:** never (or however long ago the last test was).

A bilge with a small amount of residual water — a few centimetres — is also normal. The reading should be stable. If it jumps around by more than a centimetre every few seconds, the sensor may be loose, the water may be choppy, or the calibration may be off.

---

## If something looks wrong

**The blue LED is fast-blinking (sensor error).**

1. Open the enclosure.
2. Check that the sensor cable is firmly seated in the green terminal block on the ADS1115 board.
3. Check that the small current-to-voltage converter board has its two potentiometers still set (the white dabs of nail polish or paint on top of them should be intact).
4. Check that the wires between the level shifter and the ADS1115 are in place.
5. Power-cycle the device.

If the fast-blink returns within a minute, the sensor itself may be damaged and needs replacement.

**The dashboard shows a reading but no alerts are going out.**

Check the WiFi page in the setup interface. If a **"portal blocked"** banner is showing, see the marina WiFi section above. Otherwise, check that the phone number and Discord webhook are still filled in on the Notifications page.

**The setup network (`ESP32-BilgeRise-Setup`) doesn't appear.**

Press the button once. Wait up to 10 seconds. The LED should begin to slow-blink and the network should appear. If it doesn't, power-cycle the device.

**You forgot the setup-network password.**

The password is unique to each device. On most installations, it's printed on a sticker on the side of the enclosure. If not, it's in the device's serial log on first boot (USB cable, 115200 baud, look for the `AP password:` line). Contact whoever set the device up if neither is available.

**The dashboard is empty or the page won't load.**

Check that your phone is still connected to the `ESP32-BilgeRise-Setup` WiFi. Some phones switch back to the marina's network automatically when the signal is weak. Move closer to the box and try again.

**The device is unresponsive — the LED is doing something not in the table above, or nothing at all.**

Power-cycle it (disconnect 12 V for 10 seconds and reconnect). If the problem persists, contact support.

---

## Silencing an active alert

If the device is in a flood state and you want to stop it from sending more texts — usually because you're testing, or because the customer has acknowledged the situation and is dealing with it — **press and hold the button for 5 seconds**. The LED will briefly flash to confirm.

The device stays in flood-watch mode but stops sending alerts. When the water level returns to normal, silence is cleared automatically.

---

## For the boat owner

*Print this page and hand it to the customer. The installer reads everything before this; the owner reads only this.*

> ### What this device does
>
> The box in your bilge watches the water level continuously. When the water rises above the warning level set during installation, you'll get a text message and a Discord alert. When it rises further, you'll get a more urgent alert.
>
> ### Your installation
>
> | Setting | Value |
> |---|---|
> | Warning level | _____ cm |
> | Urgent level | _____ cm |
> | SMS goes to | _____ |
> | Discord webhook | _____ |
> | Repeat interval during a flood | every _____ minutes |
> | Dashboard URL | _____ |
> | Installer contact | _____ |
>
> ### When you'll hear from it
>
> During normal operation, the box is silent. The little blue light is off, and you don't get any messages. This is correct — it means everything is fine.
>
> If the water level rises above the warning level, you'll get a text at the phone number above. The first text will tell you the current water level and how fast it's rising. You'll get another text at the repeat interval until the water goes back down.
>
> If the water level goes above the urgent level, you'll get the urgent message immediately.
>
> ### The dashboard
>
> The dashboard URL above shows the current water level and a history. You don't need to check it — the device will text you if anything needs your attention — but it's there if you want to see what's going on.
>
> ### If the device is alarming and you want to silence it
>
> Press and hold the small button on the side of the box for 5 seconds. The light will flash briefly to confirm. The device will stop sending alerts until the water level returns to normal. Next time there's a flood, the alerts come back automatically.
>
> ### If something looks wrong
>
> - The blue light is blinking fast: the device can't read the water level. Call the installer.
> - The blue light is doing two quick flashes and a pause: the device has lost the WiFi connection and can't send alerts. Check the boat's network.
> - The blue light is off, but you're not getting texts: the device is in normal mode, but its network is probably blocking alerts. Call the installer.

---

## When you need help

If the device is misbehaving in a way this guide doesn't cover, or if a component needs to be replaced, contact the installer or the person who set the device up for you. They will have a record of the device's MAC address, its firmware version, and its configuration, which will make diagnosis faster.

For firmware updates, the device checks for new versions automatically. You don't need to do anything unless you want to force a check — in which case, open the setup page, go to **Updates**, and tap **Check for Updates**.

---

*This guide corresponds to firmware version 1.1.8.*
