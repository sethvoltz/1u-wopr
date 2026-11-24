# 1U WOPR on ESPHome

The same blinkenlights as `main.py`, running on an ESP32 under [ESPHome](https://esphome.io) instead of MicroPython on a Pico. The animations are a port of the original sections (shifter, programs A and B with their counters, Game of Life, random); on top of that the panel shows up in Home Assistant:

- a light entity for on/off and brightness
- a text entity that scrolls a message across the display once (up to 8 queue up), then the animation carries on
- a switch for the fast bursts of the random section, a slider for the Game of Life speed and a button for a fresh Life board
- Wi-Fi signal, uptime, IP address, restart button and over-the-air updates

Home Assistant is optional. Without it the panel just runs the animations.

## Supported boards

| Board | Status |
| --- | --- |
| AI-Thinker ESP32-CAM | Tested, the config is written for it as is |
| Other classic ESP32 boards (DevKit / WROOM-32, D1 Mini ESP32, ...) | Should work after the changes below, not tested |
| ESP32-S2 / S3 / C3 / C6 | Should work after the changes below, not tested |
| ESP8266 | Not supported by this config (it uses the ESP-IDF framework) |

For a board other than the ESP32-CAM, change three things in `wopr.yaml`:

1. `esp32: board:` to your board, for example `esp32dev` or `esp32-s3-devkitc-1`.
2. The three display pins (`spi: clk_pin`, `spi: mosi_pin`, `display: cs_pin`) to any free GPIOs. The `ignore_strapping_warning` line can go if your CS pin is not a strapping pin.
3. The two on-board LEDs, which are specific to the ESP32-CAM: `status_led` on GPIO33 (active low) and the white flash LED on GPIO4. Point `status_led` at your board's LED, or delete the `interval:` block together with the `status_led` output. Delete the `flash_led` output together with the second `light:` entry.

## Wiring

The ESP32 takes the place of the Pico on the first matrix of the chain:

| MAX7219 | ESP32-CAM |
| --- | --- |
| CLK | GPIO14 |
| DIN | GPIO13 |
| CS | GPIO15 |
| VCC | 5 V, straight from the power supply, not through the ESP32 board |
| GND | GND, shared with the ESP32 |

Twelve matrices draw more than a USB port or the board's regulator should carry, so feed the chain from the supply directly. Brightness is capped at MAX7219 level 12 of 15, which keeps a full panel within a 2.4 A supply; the cap is one line in the `wopr_brightness` output if your supply is larger.

The MAX7219 runs at 5 V and the ESP32 talks to it at 3.3 V. That works without a level shifter on my panel. If the first module shows garbage or ignores commands, put a 74AHCT125 on CLK, DIN and CS.

## Build and flash

You need Python 3 and the two files of this directory, `wopr.yaml` and `wopr.h`, kept next to each other.

1. Install ESPHome:

   ```sh
   pip install esphome
   ```

2. Create `secrets.yaml` next to `wopr.yaml`:

   ```yaml
   wifi_ssid: "your network"
   wifi_password: "your password"
   wifi2_ssid: "a second network"
   wifi2_password: "its password"
   api_key: "paste the output of: openssl rand -base64 32"
   ```

   The second network is there for a panel that moves between two places. With only one, delete the second entry under `wifi: networks:` in `wopr.yaml` and leave the `wifi2_*` lines out.

3. Flash over a serial port the first time:

   ```sh
   esphome run wopr.yaml
   ```

   The ESP32-CAM has no USB port of its own. Use an ESP32-CAM-MB base board, or a 3.3 V USB-serial adapter with GPIO0 tied to GND while the board powers up (remove the jumper and reset afterwards).

4. Later updates go over Wi-Fi with the same command; ESPHome finds the device as `wopr.local`. After an over-the-air update leave the panel powered for a minute. A reset right after the update makes the bootloader roll back to the previous firmware.

On boot the panel scrolls `W.O.P.R` and `DEFCON 5`, then starts the animations.

## Home Assistant

The device is discovered on its own (Settings → Devices & services). Home Assistant asks for the encryption key: that is the `api_key` from `secrets.yaml`.

## Troubleshooting

**Picture mirrored, upside down, or modules in the wrong order.** Uncomment `rotate_chip`, `reverse_enable` or `flip_x` under `display:` in `wopr.yaml` instead of rewiring.

**The small red LED on the ESP32-CAM** tells where the connection stands: solid means no Wi-Fi, blinking means Wi-Fi is up but Home Assistant is not connected, off means all good.

**No Wi-Fi inside a rack.** The ESP32-CAM's PCB antenna does poorly behind metal. The board has a u.FL connector for an external antenna, selected by moving a 0 Ω resistor next to it. When no configured network is in reach, the panel opens its own access point with a setup page.
