LED Matrix MQTT display
  Board  : Wemos D1 Mini (ESP8266)
  Matrix : 8x32 WS2812B (NeoPixel) panel, DIN on D4 (GPIO2)
  Shows the plain-text payload from MQTT topic "cgm/bg",
  followed by a trend symbol from "cgm/trend" (1-7, 0 = none).
  Brightness set by "cgm/bright" (0-9, scaled to BRIGHT_MIN..BRIGHT_MAX).

  Libraries (Library Manager):
    - PubSubClient (Nick O'Leary)
    - Adafruit NeoMatrix  (pulls in Adafruit GFX + Adafruit NeoPixel)
