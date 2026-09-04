# Arduino

Firmware for a couple of home builds.

| Project | What it is |
|---|---|
| [coffe-grinder](coffe-grinder/) | Motorized Timemore C3 burr grinder. Arduino Nano, BTS7960 H-bridge, closed-loop speed off a hall encoder, OLED and three lit buttons. |
| [flip-clock](flip-clock/) | Split-flap clock on an ESP32. Two 28BYJ-48 drums, hall homing, NTP, settings page served to a phone. |

`libraries/` holds shared Arduino libraries.

## Licenses

Firmware in this repository is [MIT](LICENSE).

The clock is printed from third-party 3D models published under
**CC BY-NC 4.0** — see [flip-clock/README](flip-clock/README.md) for who made
them. Non-commercial: printing one for yourself or as a gift is fine, selling
assembled clocks is not.
