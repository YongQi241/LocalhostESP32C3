# Build the Project

Set the WiFi, MQTT, Firebase, and Discord values in
`src/env_config.cpp` before building. These settings are compiled into
the firmware.

Using the PlatformIO toolbar in VS Code, select Build.

Alternatively, run:

pio run

After a successful build, PlatformIO generates files similar to:

.pio/build/esp32dev/firmware.bin
.pio/build/esp32dev/firmware.elf

# Run the Wokwi Simulation

Build the project before starting Wokwi.

Then:

Open the VS Code Command Palette:
Linux/Windows: Ctrl+Shift+P
macOS: Cmd+Shift+P
Run:
Wokwi: Start Simulator


# Live MQTT Settings

The device listens for one retained MQTT message that can change its
behavior on the fly, no reflashing needed. This document explains how that
works.

## Topic

```
xiao/esp32c3/sensors/threshold
```

This is the same topic name as the constant `MQTT_TOPIC_THRESHOLD` in
`main.cpp`. The device subscribes to it every time it wakes up and connects
to the broker.

## How to send a setting

Publish a JSON object to the topic above. Use the retain flag so the
setting survives broker restarts and gets picked up again if the device
reboots.

```
mosquitto_pub -h broker.hivemq.com -r -t xiao/esp32c3/sensors/threshold \
    -m '{"distance_mm":50,"light":100,"awake_ms":5000}'
```

You do not need to send every field at once. Any subset is fine, only the
fields you include get changed, everything else keeps its current value.

## Fields

### distance_mm

Distance delta, in millimeters, that counts as a "sudden change" for the
distance sensor. If the reading jumps by more than this amount between
wakes, the device treats it as sudden.

Default: 100

### light

Light reading delta, in raw ADC counts, that counts as a "sudden change"
for the light sensor. Same idea as distance_mm, just for light.

Default: 200

### awake_ms

How long, in milliseconds, the device stays awake and watching after the
last sudden change before it goes back to deep sleep. This is an idle
timeout, not a fixed window, so every additional sudden change resets the
clock.

Default: 3000

### zones

An array of distance zones, each one a label plus a cutoff. This is what
turns a raw distance reading into a message like "Close!" or "Far..".

```
{"zones":[{"max_mm":300,"label":"Close!"},
          {"max_mm":600,"label":"Medium"},
          {"max_mm":65535,"label":"Far.."}]}
```

Rules for this field:

- List zones in ascending order by `max_mm`.
- The last zone in the list always acts as the catch all for anything
  beyond it, so its own `max_mm` value barely matters as long as it is the
  largest one, a value like 65535 is a safe choice.
- Sending a `zones` array replaces the whole table, not just the entries
  you list. If you send 2 zones, the device now has 2 zones, the previous
  ones are gone.
- Up to 5 zones are kept (`MAX_ZONES` in main.cpp). Extra entries beyond
  that are ignored, with a note in the serial log.
- Each label can be up to 23 characters (`ZONE_LABEL_LEN - 1` in
  main.cpp), longer labels get cut off.
- Entries missing `max_mm` or `label` are skipped rather than rejecting
  the whole message.

Default zones: under 300mm is "Close!", anything past that is "Far..".

## What happens when a setting changes

- The device applies the change immediately and keeps it in RTC memory, so
  it survives deep sleep (but not a full power loss or reflash).
- The onboard LED blinks 3 times as a visual confirmation that a change
  was picked up.
- Changing `zones` also resets the device's memory of which zone it was
  last in, so the very next reading gets freshly evaluated against the new
  table and reported, even if the actual distance has not moved.

## Payload size

The MQTT packet limit is 512 bytes (`MQTT_MAX_PACKET_SIZE` in
`network.h`). A message with all 5 zones plus distance_mm, light, and
awake_ms comfortably fits. Raise that limit if the accepted payload
format grows beyond it.

Subscribing to the device's messages

Two topics carry information out of the device:

xiao/esp32c3/sensors/data, the sensor readings, published every time the device wakes up and connects. Not retained, so a new subscriber only sees new readings from that point on, not the last one that was sent.
xiao/esp32c3/sensors/threshold, the same settings topic described above. It is retained, so subscribing to it (even without publishing anything yourself) immediately gets you the last settings that were sent, which is a handy way to check what is currently active.
Quick look with mosquitto_sub
mosquitto_sub -h broker.hivemq.com -t xiao/esp32c3/sensors/data -v

The -v flag prints the topic name alongside each message, useful once you subscribe to more than one topic at a time, for example with a wildcard that covers both:

mosquitto_sub -h broker.hivemq.com -t "xiao/esp32c3/sensors/#" -v
Parsing the payloads

Both topics carry a plain JSON object with no extra framing, so any MQTT client library plus a JSON parser is enough to read them.

Data topic fields:

distance_mm, unsigned integer, latest distance reading in millimeters.
light, integer, latest raw light sensor reading.
switch_on, boolean, current slide-switch state. The device sends one final
false value before sleeping when the switch is turned off.
wake, unsigned integer, a wake cycle counter. If it jumps by more than 1 between messages you received, a wake happened that never made it to the broker, useful for spotting dropped connectivity.

Threshold topic fields: the same distance_mm, light, awake_ms, and zones fields described earlier in this document. This topic only carries a message when someone publishes a setting change, or when the broker replays the retained value to a client that just subscribed.

Example subscriber in Python, using paho-mqtt:

python
import json
import paho.mqtt.client as mqtt

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload)
    except json.JSONDecodeError:
        print(f"Could not parse message on {msg.topic}: {msg.payload!r}")
        return

    if msg.topic.endswith("/data"):
        print(f"Distance: {payload.get('distance_mm')} mm, "
              f"Light: {payload.get('light')}, "
              f"Wake: {payload.get('wake')}")
    elif msg.topic.endswith("/threshold"):
        print(f"Current settings: {payload}")

client = mqtt.Client()
client.on_message = on_message
client.connect("broker.hivemq.com", 1883)
client.subscribe("xiao/esp32c3/sensors/#")
client.loop_forever()

Example data topic payload: {"distance_mm":280,"light":512,"wake":42}
