# DL9SAU-Fork von Meshtastic

Original-Projekt / Original project: **[meshtastic/firmware](https://github.com/meshtastic/firmware)**

**Deutsch**

Dies ist eine erweiterte Version des originalen Meshtastic-Projekts. Mein Fokus liegt auf zuverlässigerem Companion-Betrieb (Smartphone-App + Tracker-Gerät) mit Stromspar-Mechanismen für mobile Nutzung und Komfort-Funktionen für den täglichen Einsatz.

Wesentliche Änderungen:

- **BLE-Toggle ohne Reboot** auf ESP32 und NRF52 (inkl. Fix des NimBLE-Reconnect-Bugs reason 531 beim Deinit/Reinit-Zyklus).
- **BLE Sleep/Awake-Zyklus** auf ESP32 für Companion-Betrieb (Menu-Option „Cycle"), reduziert BT-Verbrauch im Idle erheblich.
- **Automatischer LoRa-Mode-Wechsel bei Regions-Wechsel** (GPS-basiert, region-standard Preset für EU868-Zonen, mit User-Override). Der Preset-Wechsel wird über einen automatischen Kanal-Rename an die Smartphone-Apps (iOS/Android) durchgereicht, damit diese das Update mitbekommen.
- **CR=8-Präferenz:** Der Fork upgraded `coding_rate = 5 → 8` automatisch (LongFast / Medium* / Short* haben default CR=5 → bekommen jetzt die redundantere 4/8-Kodierung; LongSlow/Moderate/Turbo haben ohnehin CR=8). Explizit gesetzte CR=6 oder CR=7 bleiben unangetastet — dort steckt die Opt-out-Möglichkeit.
- **Client-Rollen-Verhalten** überarbeitet (per-Paket CR/TX-Power-Overrides für CLIENT/CLIENT_BASE, delay-buffered Broadcast-Relay auf CLIENT_BASE).
- **Zusätzliche LongFast-Bake** ~1× pro Stunde, damit auch bei abweichendem Preset regelmäßig eine Position im Default-Mode zu sehen ist.
- **Offline-Textnachrichten-Speicher** — puffert eingehende Text-Nachrichten (Primär-Kanal, Hashtag-Kanäle, DMs, Private) mit Flash-Persistenz, damit nichts verloren geht wenn die Smartphone-App gerade nicht verbunden ist.

Details zu den einzelnen Änderungen sind in den Commit-Nachrichten dokumentiert.

Fertige Binaries für gängige Boards stehen unter [Releases](https://github.com/dl9sau/meshtastic-fork-DL9SAU/releases).

Aktiv getestet auf Heltec Wireless Tracker und SenseCAP T1000-E.

Build und Flash funktionieren wie beim Original-Projekt.

**English**

This is an enhanced version of the original Meshtastic project. My focus is on more reliable Companion operation (smartphone app + tracker device) with power-saving mechanisms for mobile use and convenience features for everyday use.

Notable changes:

- **BLE toggle without reboot** on ESP32 and NRF52 (includes fix for the NimBLE reason 531 reconnect bug on the deinit/reinit cycle).
- **BLE sleep/wake cycle** on ESP32 for Companion operation (menu option "Cycle"), substantially reduces BT power draw when idle.
- **Automatic LoRa mode switch on region change** (GPS-based, region-standard preset for EU868 zones, with user override). The preset change is propagated to the smartphone apps (iOS/Android) via an automatic channel rename so they pick up the update.
- **CR=8 preference:** the fork automatically upgrades `coding_rate = 5 → 8` (LongFast / Medium* / Short* default to CR=5 → get the more redundant 4/8 coding; LongSlow/Moderate/Turbo already default to CR=8). Explicitly set CR=6 or CR=7 are left as-is — that's the opt-out.
- **Reworked client role behavior** (per-packet CR / TX-power overrides for CLIENT/CLIENT_BASE, delay-buffered broadcast relay on CLIENT_BASE).
- **Additional LongFast beacon** approximately once per hour, so a position is regularly visible on the default preset even when running a different regional preset.
- **Offline text-message storage** — buffers incoming text messages (primary channel, hashtag channels, DMs, private) with Flash persistence, so nothing is lost when the smartphone app is not currently connected.

Details of the individual changes are documented in the commit messages.

Prebuilt binaries for common boards are available at [Releases](https://github.com/dl9sau/meshtastic-fork-DL9SAU/releases).

Actively tested on Heltec Wireless Tracker and SenseCAP T1000-E.

Build and flash work the same as in the original project.
