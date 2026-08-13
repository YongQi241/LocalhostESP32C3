# ESP32-C3 Distance and Light Monitor

This project uses a Seeed Studio XIAO ESP32-C3, a VL53L1X distance sensor,
a TEMT6000 light sensor, MQTT, Firebase Realtime Database, and Discord.
The device normally uses deep sleep to reduce power consumption. A sudden
sensor change or a distance-zone transition starts an active monitoring window.

## Configuration

Edit `src/env_config.cpp` before building:

- `WIFI_SSID` and `WIFI_PASSWORD`
- `MQTT_BROKER`, `MQTT_PORT`, and `MQTT_DEVICE_ID`
- `MQTT_ACCESS_KEY` (optional for the demo)
- `FIREBASE_HOST` and `FIREBASE_AUTH`
- `DISCORD_WEBHOOK_URL`

`MQTT_ACCESS_KEY` is used only to validate incoming settings. If it is an
empty string, access-key validation is disabled.

Timing, thresholds, pin assignments, zone limits, and retry counts are in
`include/config.h` and `src/state.cpp`.

## Build

Use the PlatformIO Build command in VS Code, or run:

```sh
pio run
```

The board environment is `seeed_xiao_esp32c3`. Build output is written under:

```text
.pio/build/seeed_xiao_esp32c3/
```

## Wokwi simulation

Build the project, open the VS Code Command Palette, and run:

```text
Wokwi: Start Simulator
```

The included simulation uses `Wokwi-GUEST` with no Wi-Fi password.

## Device behavior

While the switch is ON, the device wakes approximately every 2 seconds and
checks the sensors. It connects to the network when one of these conditions is
true:

- First reading after boot
- Sudden distance or light change
- Distance-zone change
- Heartbeat due (every 30 wakes, approximately 60 seconds)

After connecting, the device enters an active window. Sensors are checked every
250 ms. Sudden changes and observed zone changes reset the idle timer. When no
further activity occurs for `awakeDurationMs` (default 3000 ms), the device posts
an `idle` status and returns to deep sleep.

While the switch is OFF, the device sends one final `switch_off` status and then
wakes approximately every 5 seconds to check the switch. GPIO wakeup can also
wake it when the switch is turned ON.

## MQTT live readings

Topics include `MQTT_DEVICE_ID`. With the default ID `xiao01`, they are:

```text
xiao/esp32c3/sensors/xiao01/data
xiao/esp32c3/sensors/xiao01/threshold
```

The data topic carries live sensor readings:

```json
{
  "id": "xiao01",
  "distance_mm": 280,
  "light": 512,
  "switch_on": true,
  "wake": 42
}
```

Data messages are retained, so a new subscriber receives the latest state.
The device publishes once when the active session begins and then after each
valid distance sample during the active window (nominally every 250 ms, plus
network-processing time). A final retained message sets `switch_on` to `false`.

Subscribe with:

```sh
mosquitto_sub -h broker.hivemq.com \
  -t "xiao/esp32c3/sensors/xiao01/#" -v
```

## Live MQTT settings

Publish a retained JSON object to the threshold topic. Any subset of fields may
be supplied:

```sh
mosquitto_pub -h broker.hivemq.com -r \
  -t "xiao/esp32c3/sensors/xiao01/threshold" \
  -m '{"distance_mm":50,"light":100,"awake_ms":5000}'
```

If `MQTT_ACCESS_KEY` is non-empty, include the matching key:

```json
{"key":"YOUR_KEY","distance_mm":50}
```

Supported fields:

- `distance_mm`: distance delta that counts as sudden; default 100 mm.
- `light`: light delta that counts as sudden; default 200 ADC counts.
- `awake_ms`: idle duration of the active window; default 3000 ms.
- `zones`: complete replacement distance-zone array.

Settings remain in RTC memory across deep sleep, but not across power loss or a
firmware reset. Accepted changes cause the onboard LED to blink three times.

### Distance zones

Example:

```json
{
  "zones": [
    {"max_mm":300,"label":"Close!"},
    {"max_mm":600,"label":"Medium"},
    {"max_mm":65535,"label":"Far.."}
  ]
}
```

Rules:

- Supply zones in ascending `max_mm` order; the firmware does not sort them.
- Each non-final zone includes its `max_mm` boundary.
- The final zone is a catch-all, so its `max_mm` is not used for matching.
- A zones update replaces the entire table.
- At most `MAX_ZONES` (currently 5) valid entries are stored.
- Labels contain at most `ZONE_LABEL_LEN - 1` (currently 23) characters.
- Malformed entries are skipped; an update with no valid entries is rejected.

The MQTT packet size is configured as 512 bytes in `include/network.h`.

## Firebase status history

Firebase stores an append-only device-status timeline rather than sensor
readings. Records are created with HTTP `POST` at:

```text
/devices/<MQTT_DEVICE_ID>/status/<generated-id>
```

During the active window, one record is attempted every second. One additional
final record is posted when the window becomes idle or the switch turns off.
Final records may be attempted up to `FIREBASE_FINAL_RETRY_COUNT` times
(currently 3); normal active records use one attempt.

Example record:

```json
{
  "device_id": "xiao01",
  "state": "active",
  "switch_on": true,
  "sensor_status": "ok",
  "mqtt_connected": true,
  "discord_status": "sent",
  "wake": 42,
  "timestamp": 1786600123000
}
```

Possible `state` values are `active`, `idle`, and `switch_off`.
Possible `sensor_status` values are `not_checked`, `not_detected`, `timeout`,
and `ok`. Possible `discord_status` values are `not_sent`, `sent`, and `failed`.

The timestamp is generated by Firebase server time in milliseconds. Because
each record has a timestamp and state, this data is suitable for a long-period
status bar chart or activity timeline.

## Discord notifications

Discord alerts are attempted for sudden sensor changes and distance-zone
changes. A message includes the zone label, current distance, distance delta,
current light value, and light delta:

```text
Close! (280 mm, change 125) | Light: 512 (change 40)
```

`lastZoneIndex` is updated only after Discord successfully returns a `2xx`
status, allowing a failed zone notification to be retried.

## Demo security notes

MQTT currently uses an unencrypted public broker connection, and an empty
access key disables command validation. Firebase and Discord call
`setInsecure()`, so HTTPS traffic is encrypted but server certificates are not
verified. These choices may be acceptable for a classroom demo but should be
replaced with authenticated MQTT and trusted CA certificates for production.
