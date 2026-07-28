<div align="center" markdown="1">

<img src=".github/meshtastic_logo.png" alt="Meshtastic Logo" width="80"/>
<h1>Meshtastic Firmware</h1>

![GitHub release downloads](https://img.shields.io/github/downloads/meshtastic/firmware/total)
[![CI](https://img.shields.io/github/actions/workflow/status/meshtastic/firmware/main_matrix.yml?branch=master&label=actions&logo=github&color=yellow)](https://github.com/meshtastic/firmware/actions/workflows/ci.yml)
[![CLA assistant](https://cla-assistant.io/readme/badge/meshtastic/firmware)](https://cla-assistant.io/meshtastic/firmware)
[![Fiscal Contributors](https://opencollective.com/meshtastic/tiers/badge.svg?label=Fiscal%20Contributors&color=deeppink)](https://opencollective.com/meshtastic/)
[![Vercel](https://img.shields.io/static/v1?label=Powered%20by&message=Vercel&style=flat&logo=vercel&color=000000)](https://vercel.com?utm_source=meshtastic&utm_campaign=oss)

<a href="https://trendshift.io/repositories/5524" target="_blank"><img src="https://trendshift.io/api/badge/repositories/5524" alt="meshtastic%2Ffirmware | Trendshift" style="width: 250px; height: 55px;" width="250" height="55"/></a>

</div>

</div>

<div align="center">
	<a href="https://meshtastic.org">Website</a>
	-
	<a href="https://meshtastic.org/docs/">Documentation</a>
</div>

## DL9SAU-Fork von Meshtastic

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

---

## Overview

This repository contains the official device firmware for Meshtastic, an open-source LoRa mesh networking project designed for long-range, low-power communication without relying on internet or cellular infrastructure. The firmware supports various hardware platforms, including ESP32, nRF52, RP2040/RP2350, and Linux-based devices.

Meshtastic enables text messaging, location sharing, and telemetry over a decentralized mesh network, making it ideal for outdoor adventures, emergency preparedness, and remote operations.

### Get Started

- 🔧 **[Building Instructions](https://meshtastic.org/docs/development/firmware/build)** – Learn how to compile the firmware from source.
- ⚡ **[Flashing Instructions](https://meshtastic.org/docs/getting-started/flashing-firmware/)** – Install or update the firmware on your device.

Join our community and help improve Meshtastic! 🚀

## Stats

![Alt](https://repobeats.axiom.co/api/embed/8025e56c482ec63541593cc5bd322c19d5c0bdcf.svg "Repobeats analytics image")
