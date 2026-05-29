# Wishlist DL9SAU

Sammlung offener Ideen / Pendenzen aus Recherche-Sessions. Nicht-priorisiert.
Auf den hier festgehaltenen Stand spaeter zurueckkommen.

---

## 2026-05-29 — Message-Storage / Phone-Replay vs. MeshCore

### Status quo (Meshtastic HEAD)

**Zwei Speicherorte fuer Nachrichten:**

1. **`MeshService::toPhoneQueue`** — App-Replay
   - Datei: `src/mesh/MeshService.h:48`, gefuettert in `MeshService.cpp:304-339`
   - Kapazitaet: `MAX_RX_TOPHONE` = **8** (ESP32 classic) / **32** (nRF52, S3, …) Pakete
     - Definiert in `src/mesh/mesh-pb-constants.h:16-22`
   - **RAM only**, kein Flash. Reboot → Queue weg.
   - TODO-Kommentar im Code (`MeshService.h:44`): "FIXME - save this to flash on deep sleep" — nie implementiert.
   - Drop-Policy bei voll: aelteste `TEXT_MESSAGE_APP`/`RANGE_TEST_APP` weg. Andere Pakete (Telemetry, Admin, Position) werden direkt verworfen.
   - **Keine** Unterscheidung Broadcast vs. DM — beides ist `TEXT_MESSAGE_APP`.
   - **Kein** User-Setting, **kein** Build-Flag.

2. **`MessageStore`** — lokale Display-History
   - Datei: `src/MessageStore.cpp/.h`
   - Nur fuer `TEXT_MESSAGE_APP` (Broadcast + DM)
   - Wrapped in `#if HAS_SCREEN` — gibt's nur auf Geraeten mit Display.
   - Kapazitaet: `MESSAGE_HISTORY_LIMIT` = **10** (ESP32 classic ohne PSRAM) / **20** (sonst)
   - RAM (`std::deque<StoredMessage> liveMessages` + Text-Pool `LIMIT × 220 B`)
   - **Optional Flash**: `ENABLE_MESSAGE_PERSISTENCE`, Default **= 1** (`MessageStore.h:13`)
     - Autosave-Intervall `MESSAGE_AUTOSAVE_INTERVAL_SEC` Default **2 h** (`MessageStore.cpp:18`), Minimum 60 s
     - Speichert in `/Messages_*.msgs`
   - Eine Variante deaktiviert Persistenz: `heltec_mesh_solar` (`variants/nrf52840/heltec_mesh_solar/platformio.ini:60`, "space-limited")
   - **Kein** Runtime-Toggle — pure Build-Flags.

### Vergleich MeshCore

- MeshCore: 1 Queue (`offline_queue[16]` Frames, RAM), DM bevorzugt vor Channel beim Overflow.
- Begruendung MeshCore: Privacy (Reboot loescht alles), Flash-Wear, Latency, Code-Einfachheit.
- Meshtastic: 2 Layer, optional Flash, **kein** DM-vs-Public-Vorzug (nur Text vs. Telemetry).

### Offene Wuensche / Diskussionspunkte

- [ ] Soll `toPhoneQueue` einen Drop-Vorzug fuer DM (`to == nodenum`) vor Broadcast bekommen, wenn beides `TEXT_MESSAGE_APP` ist? (MeshCore-Aequivalent)
- [ ] Runtime-Toggle fuer `ENABLE_MESSAGE_PERSISTENCE` ueber AdminMessage/userprefs, damit Privacy-bewusste User ohne Custom-Build deaktivieren koennen?
- [ ] `MAX_RX_TOPHONE` auf ESP32 classic von 8 hochziehen? (RAM-Budget pruefen) — Asymmetrie 8 vs. 32 wirkt willkuerlich.
- [ ] `MessageStore` ohne Screen verfuegbar machen (Headless-Devices mit App-Replay-History)?
- [ ] Pro-Channel-Quotas in `toPhoneQueue` damit ein Spam-Channel nicht alle Slots frisst?

### Verweise
- Vorherige Session (MeshCore-Recherche): `offline_queue[16]` in `MyMesh.cpp:221+`, ~3.2 KB RAM total.
