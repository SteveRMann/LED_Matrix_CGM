# LED Matrix CGM Display

Shows a CGM reading from MQTT on an 8x32 LED matrix.

## Hardware
- **Board:** Wemos D1 Mini (ESP8266)
- **Matrix:** 8x32 WS2812B (NeoPixel) panel, DIN on D4 (GPIO2)

## MQTT topics
| Topic | Payload | Purpose |
|---|---|---|
| `cgm/bg` | plain text | Value shown on the matrix |
| `cgm/trend` | 1–7 (0 = none) | Trend symbol after the value |
| `cgm/bright` | 0–9 | Brightness, scaled to `BRIGHT_MIN`..`BRIGHT_MAX` |

Tip: publish `cgm/trend` and `cgm/bright` with **retain** set, so a board that has just rebooted or reconnected picks up the current values right away instead of waiting for the next change.

## Libraries (Library Manager)
- PubSubClient (Nick O'Leary)
- Adafruit NeoMatrix (pulls in Adafruit GFX + Adafruit NeoPixel)

## Setup
1. Copy `secrets_sample.h` to `secrets.h` and fill in WiFi and MQTT values (`secrets.h` is not in git).
2. Flash the board and open Serial at 115200. The chip ID is printed at boot.
3. To give a new board a friendly name, add its chip ID to `DEVICE_NAMES` in the sketch. Unlisted boards are named `LEDMatrix-<chipid>`.

## Settings (top of the sketch)
| Setting | Default | What it does |
|---|---|---|
| `BRIGHTNESS` | 1 | Startup brightness (0–255) until `cgm/bright` arrives |
| `BRIGHT_MIN` / `BRIGHT_MAX` | 1 / 4 | `cgm/bright` 0 maps to `BRIGHT_MIN`, 9 maps to `BRIGHT_MAX` |
| `BG_URGENT_LOW` / `BG_LOW` / `BG_HIGH` / `BG_URGENT_HIGH` | 55 / 70 / 180 / 250 | Color bands (mg/dL) |
| `TREND_GREY` | 200 | Grey level for the trend symbol (≥200 stays visible at brightness 1) |
| `STALE_MS` | 15 min | No `cgm/bg` for this long: value greys out, trend is hidden |
| `REBOOT_MS` | 2 min | No `cgm/bg` for this long: board reboots. `0` disables |
| `SCROLL_MS` | 60 ms | Scroll speed when text is wider than 32 px |

## How it works

### Boot
- The matrix shows **WiFi** in blue while connecting. The board waits here until WiFi connects.
- Serial prints the chip ID, device name and **reset reason** (useful to tell a watchdog reboot from a crash or power dip).
- The device name is used as the WiFi hostname, the NetBIOS name, the mDNS name (`<name>.local`) and the MQTT client ID.

### MQTT
- Connects using the device name as client ID, so each board must have a unique name or the broker will kick one off when the other connects.
- If the connection drops, it retries every 5 s.
- Keepalive is 60 s and the socket timeout 30 s, so a single late ping on busy WiFi doesn't drop the connection.
- Before the first reading: **---** in grey means connected and waiting for data; **MQTT** in red means the connection attempt failed.

### Display
- The value is colored by the bands:

  | Value | Color |
  |---|---|
  | ≤ 55 or ≥ 250 | Red |
  | < 70 or > 180 | Amber |
  | 70–180 | Green |
  | Not a number | White |

- The trend symbol follows the value, always in grey:

  | `cgm/trend` | Meaning | Symbol |
  |---|---|---|
  | 0 | Unknown | none |
  | 1 | Rising quickly | ⇈ (bitmap) |
  | 2 | Rising | ↑ (bitmap) |
  | 3 | Rising slightly | `/` |
  | 4 | Steady | `-` |
  | 5 | Falling slightly | `\` |
  | 6 | Falling | ↓ (bitmap) |
  | 7 | Falling quickly | ⇊ (bitmap) |

- Text up to 32 px wide is centered; anything wider scrolls.
- The display only redraws when something changes (new value, trend, brightness, stale or connection state), unless it is scrolling.

### Red dot (bottom-right pixel)
- Lit whenever MQTT is **not connected**. It does not necessarily mean WiFi is down.
- To tell which, check Serial: in the `MQTT failed, rc=... WiFi=...` line, `WiFi=3` means WiFi was still connected (broker or network problem); any other number means WiFi had dropped.
- A short blip lights the dot and clears on reconnect, usually within about 5 s.

### Stale data
- After `STALE_MS` with no `cgm/bg`, the last value is shown in grey with no trend.

### Reboot watchdog
- `cgm/bg` normally arrives every 10 s. If none arrives within `REBOOT_MS`, the board prints a line to Serial (WiFi status, RSSI, MQTT state) and reboots.
- The timer starts at boot, so a board that never gets data after starting up also reboots every `REBOOT_MS` until data comes back.
- Only `cgm/bg` resets the timer; `cgm/trend` and `cgm/bright` do not.
- **Interaction with stale:** if `REBOOT_MS` is shorter than `STALE_MS` (the default), the grey stale display never appears. If the data source stops, the board keeps rebooting and shows **---** instead. Set `REBOOT_MS` above `STALE_MS` if you'd rather see the last value greyed out.

## License
No license. All rights reserved.
