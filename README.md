# ESP32 buttons for Home Assistant over Wi-Fi/MQTT

Firmware for **ESP32-WROOM-32D / ESP32-D0WD** using **ESP-IDF v5.5.4**.
The original ESP32 has no Zigbee radio, so this version connects to Home Assistant
through its **MQTT integration**, rather than ZHA.

Each enabled physical button becomes a discovered **binary sensor**:
**pressed = on**, **released = off**. Supports 1–4 buttons, 30 ms default
press/release debounce, retained state messages, retained discovery, and an
availability topic with an MQTT last will. Wi-Fi and MQTT reconnect automatically;
Home Assistant's default `homeassistant/status` birth message refreshes discovery
and current states after HA restarts.

Power the board continuously over USB or suitable DC. Sleep, OTA updates and
click/double-click gesture events are not implemented. This is state reporting,
not a guaranteed click counter: short pulses below the debounce threshold are
ignored, and disconnected transitions coalesce to the latest physical state.
Previously submitted QoS 1 messages may be retransmitted by MQTT.

## Buttons and wiring

Default: **one active-low button on GPIO0**, with an internal pull-up. GPIO0 is
BOOT on many ESP32 development boards; check your board schematic. The module
name does not identify a specific development board or guarantee that it has
this button. **RESET/EN cannot be reported**: it resets the CPU. Holding BOOT
while resetting enters download mode; release it to run the firmware.

For external buttons, connect the configured GPIO to **GND** through a button.
In `idf.py menuconfig` → **Home Assistant MQTT buttons**, select:

| Setting | Default | Purpose |
| --- | --- | --- |
| Number of buttons | 1 | Enables 1–4 sensors |
| Button 1 GPIO | 0 | Common BOOT pin |
| Button 2/3/4 GPIO | 25/26/27 | Used only when enabled |
| Buttons are active high | Off | Enable for buttons to 3.3V with internal pull-downs |
| Debounce interval | 30 ms | Stable time needed on both edges |

Never connect a GPIO to 5V. GPIO6–11 are flash pins and are rejected. GPIO34–39
lack internal pull resistors and are unsupported by this configuration. Invalid
and duplicate GPIOs are rejected. Avoid UART pins 1/3 and board-specific
peripherals; consider boot strapping requirements when choosing other pins.

## Home Assistant and broker setup

1. Install/configure an MQTT broker reachable from the board. With Home Assistant
   OS, the Mosquitto broker app/add-on is a common choice. Create a broker user.
2. Add/configure Home Assistant's **MQTT integration** against that broker and
   leave MQTT discovery enabled with its default prefix `homeassistant`.
3. Configure these firmware settings in `idf.py menuconfig`:
   - **Wi-Fi SSID** and **Wi-Fi password** (2.4 GHz).
   - **MQTT broker URI**, e.g. `mqtt://192.168.1.10:1883` for a trusted local LAN,
     or `mqtts://broker.example.com:8883` with TLS.
   - **MQTT username/password**, matching the broker user. Enter them in their
     separate fields, rather than embedding credentials in the URI.
4. Build and flash. A firmware built with empty SSID or URI logs a configuration
   reminder and reports button changes only to the serial monitor.
5. Look for a device named `esp32_buttons_<12-digit-MAC>` under the MQTT
   integration. Each enabled button has an entity named `Button 1`, `Button 2`, etc.
   Rename entities as desired; actual entity IDs are chosen by Home Assistant.
6. Press and hold a button, then release it. Check `on`/`off` and that other buttons
   stay unchanged. Serial logs should show Wi-Fi connected, MQTT connected, and
   pressed/released transitions.

Broker permissions must allow publishing to `esp32_buttons/<MAC>/#` and
`homeassistant/binary_sensor/esp32_buttons_<MAC>/#`, plus subscribing to
`homeassistant/status`. The board and Home Assistant need to use the same broker.
Do not use `localhost` as the broker address on the board.

`mqtt://` is plaintext, including broker authentication. For encrypted brokers,
`mqtts://` verifies the server certificate and hostname against the IDF public
root bundle. For a private CA, place its **public CA certificate** in
`main/broker_ca.pem` and enable **Use main/broker_ca.pem...** in menuconfig.
Certificate verification stays enabled. MQTT TLS also needs an accurate clock;
configure the NTP server if `pool.ntp.org` is unreachable on your network.

Credentials are compiled into the firmware. Keep `sdkconfig`, build artifacts,
and firmware binaries private; these generated paths are ignored by Git.
Do not commit credentials to `sdkconfig.defaults` or paste them into chat.

## Build and flash

Install and activate
[ESP-IDF v5.5.4 for ESP32](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/get-started/index.html),
including its Git submodules and ESP32 toolchain. All application dependencies
(Wi-Fi, MQTT and cJSON) are included in ESP-IDF.

From this repository:

```bash
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace the port with your actual board port (for example `COM3`). Exit the monitor
with Ctrl+]. The partition layout fits 2MB flash. Set the target before entering
credentials: `set-target` resets the generated sdkconfig. Change settings with
`menuconfig`, rebuild, and flash to update credentials or button assignments.

On the prepared Codex cloud machine:

```bash
export IDF_TOOLS_PATH=/workspace/tools/espressif
export XDG_CACHE_HOME=/workspace/tools/cache
export IDF_PYTHON_CHECK_CONSTRAINTS=no
source /workspace/tools/esp-idf/export.sh
cd /workspace/zigbee
idf.py build
bash tests/run.sh
```

Espressif's hosted Python version constraints were blocked by the cloud network
policy. Python dependencies were installed from PyPI using IDF's supported
`--no-constraints` option and frozen in
`/workspace/tools/zigbee-python-requirements.txt`. TLS and toolchain checksum
verification were retained. A normal local IDF installation should use its
standard installation flow and hosted constraints.

The cloud machine cannot flash your locally attached board or validate its
Wi-Fi/MQTT connection. Enter your actual connection settings locally before
flashing; the default cloud artifact has empty connection settings.

## MQTT topics

For a board with MAC `aabbcc112233`:

| Topic | Payload |
| --- | --- |
| `esp32_buttons/aabbcc112233/button_1/state` | `ON` / `OFF` |
| `esp32_buttons/aabbcc112233/availability` | `online` / `offline` |
| `homeassistant/binary_sensor/esp32_buttons_aabbcc112233/button_1/config` | Discovery JSON |

Discovery, availability and states use **QoS 1 and retained messages**. A last will
marks the board offline if it loses its MQTT connection; detection is delayed by
the broker's keepalive timeout. State snapshots refresh every 30 seconds as well
as on changes/reconnects. Reducing the button count clears the disabled buttons'
retained discovery records on the next connection.

Example automation (replace both entity IDs):

```yaml
alias: Button 1 pressed
triggers:
  - trigger: state
    entity_id: binary_sensor.replace_with_your_button_entity
    from: "off"
    to: "on"
actions:
  - action: light.toggle
    target:
      entity_id: light.replace_with_your_light
mode: single
```

Use `from: "on"` / `to: "off"` for release actions. This firmware does not emit
`zha_event` events.

## Validation

`bash tests/run.sh` runs host tests against the actual debounce and discovery
helpers, including bounce, held buttons, boot states, timer wrap, independent
buttons, unique discovery identifiers, payloads and topic bounds. Set `IDF_PATH`
to your IDF checkout for its cJSON sources.

Hardware acceptance still requires the board and broker: discovery in HA,
press/release for every button, power-cycle reconnection, broker/HA restart,
Wi-Fi outage recovery, and (if enabled) TLS certificate validation. Compile checks
and host tests cannot establish that these hardware checks pass.
