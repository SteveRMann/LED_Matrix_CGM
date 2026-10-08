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

## Libraries (Library Manager)
- PubSubClient (Nick O'Leary)
- Adafruit NeoMatrix (pulls in Adafruit GFX + Adafruit NeoPixel)
