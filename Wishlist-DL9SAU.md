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

- [x] **DM-Vorzug + Pro-Channel-Quotas** — geplant am 2026-05-30, siehe naechster Abschnitt.
- [ ] Runtime-Toggle fuer `ENABLE_MESSAGE_PERSISTENCE` ueber AdminMessage/userprefs, damit Privacy-bewusste User ohne Custom-Build deaktivieren koennen?
- [ ] `MAX_RX_TOPHONE` auf ESP32 classic von 8 hochziehen? (RAM-Budget pruefen) — Asymmetrie 8 vs. 32 wirkt willkuerlich.
- [ ] `MessageStore` ohne Screen verfuegbar machen (Headless-Devices mit App-Replay-History)?

### Verweise
- Vorherige Session (MeshCore-Recherche): `offline_queue[16]` in `MyMesh.cpp:221+`, ~3.2 KB RAM total.

---

## 2026-05-30 — Plan: typisierte Text-Buckets fuer `toPhoneQueue`

**Status:** Spec + Architektur final, Implementation **pending**.
**Naechste Session:** mit "Starte Implementation gemaess Wishlist 2026-05-30" wiederaufnehmen.

### Ziel

Text-Nachrichten (`TEXT_MESSAGE_APP`) aus der gemeinsamen `MeshService::toPhoneQueue` heraussplitten in **4 typisierte Queues**, damit DMs nicht durch Channel-Chatter verdraengt werden. Restliche Pakete (Telemetry, Position, Admin, Routing, …) bleiben in der bestehenden Legacy-Queue.

### Finale Konfig

| Param | Wert |
|---|---|
| Master-Define (default 1) | `DL9SAU_TOPHONE_TEXT_MESSAGE_BUCKETS_FOR_STORE_RAM_AND_FLASH` |
| PRIMARY Slots | **8** (RAM only) |
| HASHTAG Slots | **24** (RAM only) |
| PRIVATE Slots | **24** (RAM + Flash) |
| DM Slots | **24** (RAM + Flash) |
| Flash-Debounce | `DL9SAU_BUCKET_FLASH_DEBOUNCE_MS = 300000` (5 min), Force-Flush beim Shutdown |
| Concurrency-Lock | ja, analog `PhoneAPI::nodeInfoMutex` |
| Magic-Header pro `.dat` | 4 Bytes `"TB01"` (Schema-Versioning) |
| Slot-Inhalt | voller `meshtastic_MeshPacket` via `pb_encode_to_bytes` |
| Drain-Reihenfolge | global chronologisch via monotone `uint32_t toPhoneSeqNo` |

Sub-Defines: `DL9SAU_BUCKET_PRIMARY_SLOTS`, `_HASHTAG_SLOTS`, `_PRIVATE_SLOTS`, `_DM_SLOTS`, `_PRIVATE_FLASH`, `_DM_FLASH`, `_FLASH_DEBOUNCE_MS`.

### Klassifikation (in Reihenfolge)

1. `!isBroadcast(p->to)` → **BUCKET_DM**
2. `p->channel == 0` → **BUCKET_PRIMARY**
3. PSK von `channels.getByIndex(p->channel)` byte-gleich mit PSK von ch0 → **BUCKET_HASHTAG**
   (deckt sowohl AQ== open-network als auch closed-network shared-key ab)
4. Sonst → **BUCKET_PRIVATE**

Edge: `p->channel >= channels.getNumChannels()` → PRIVATE (sicherste nicht-evicting Bucket).
**Keine** Re-Klassifikation wenn Admin spaeter eine PSK aendert (lazy, einfach).

### Architektur-Entscheidungen

- **Statische BSS-Arrays pro Bucket** (kein heap): `TextBucketSlot slots_primary[8]` usw.
- **Side-Ring `uint32_t legacySeqRing[MAX_RX_TOPHONE]`** parallel zur Legacy-`toPhoneQueue` — damit auch Telemetry/Position eine seq_no haben fuer Merge-Sort beim Drain.
- **`isBucketOwnedPacket(p)` Pointer-Range-Check** in `PhoneAPI::releasePhonePacket` verhindert dass Bucket-Slots faelschlich an `packetPool` zurueckgegeben werden (waere Heap-Korruption).
- **`MessageStore` bleibt unangetastet** (orthogonal, lebt unter `#if HAS_SCREEN`).
- **StoreForward-Server-Pfad bleibt unveraendert** (`MeshService.cpp:308-317`) — wenn SF aktiv, sieht der Bucket-Store den Text nie, SF-DB ist Source of Truth.

### Files

**Neu:**
- `src/mesh/TextBucketStore.h` — Klassendeklaration, Bucket-Enum, Slot-Struct, statische Arrays, alle Methoden in Master-Define gewrapped
- `src/mesh/TextBucketStore.cpp` — Implementation: `classify()`, `enqueue()`, `peekMinSeq()`, `popSlot()`, `saveToFlash()`, `loadAllFromFlash()`, `flushIfDirty()`

**Modifiziert:**
- `src/configuration.h` — neuer DL9SAU-Defines-Block vor `#include "DebugConfiguration.h"`
- `src/mesh/MeshService.h` — Include + neue Member: `getForPhone()`, `releaseBucketSlotForPhone()`, `isBucketOwnedPacket()`, `legacySeqRing[]`, `ringHead/ringTail`
- `src/mesh/MeshService.cpp`:
  - `sendToPhone()` ~Zeile 317-319: Text-Split-Punkt vor Legacy-Enqueue
  - Neue Definition `getForPhone()`: Merge-Sort ueber Legacy + 4 Buckets nach seq_no
  - `init()` ~Zeile 82: `textBucketStore.init()` Aufruf
  - `getNodenumFromRequestId()` ~Zeile 159: Side-Ring mit-rotieren (kritisch, leicht zu uebersehen)
  - `loop()`: `textBucketStore.flushIfDirty()` aufrufen
- `src/mesh/PhoneAPI.cpp` ~Zeile 626-632: `releasePhonePacket()` mit Ownership-Check
- `src/Power.cpp` (optional): Force-Flush-Hook beim Shutdown

### RAM-Verbrauch

80 Slots × `sizeof(meshtastic_MeshPacket)` ≈ 425 B = **~34 KB BSS** + Side-Ring 128 B.
T1000e: 88 KB heute → ~122 KB von 256 KB ≈ **48 %**. Komfortabel.
ESP32 classic (320 KB RAM, MAX_RX_TOPHONE=8): Slot-Zahlen koennen ueber Sub-Defines halbiert werden falls noetig.

### Flash-Layout

```
/msgs/text_private.dat
/msgs/text_dm.dat
```
Format pro Entry: `[seq_no:4LE][len:1][packet_bytes:len]`, mit 4-Byte-Prefix `"TB01"` am Datei-Anfang. Atomarer Rewrite via `SafeFile`. Bei Master-Define = 0 keine Reads/Writes.

Debounce 5 min via `flushIfDirty()` aus `MeshService::loop()`. Bei DM-flutiger Sitzung max. ~150 KB/h geschrieben (statt ~160 KB/h bei jedem Insert). Force-Flush bei Shutdown.

### Verifikations-Plan (post-build T1000e)

1. **Master-off smoke:** `=0` recompiliert, .text/.bss Diff ~ 0 (modulo Side-Ring 128 B)
2. **Master-on smoke:** Boot-Log "TextBucketStore init: restored N+M entries"
3. **DM-Persistenz:** DM senden → Reboot → Reconnect → DM kommt im Replay
4. **HASHTAG:** ch1 mit selbem PSK wie ch0 → Nachricht landet im HASHTAG-Bucket, nicht PRIMARY
5. **PRIVATE-Eviction:** 40 Nachrichten auf Custom-PSK-Channel → nur die letzten 24 in `/msgs/text_private.dat`
6. **Cross-Bucket-Isolation:** PRIMARY volllaufen, dann DMs senden → keine PRIMARY-Eviction
7. **Chronologische Drain:** Telemetry + DMs + Chats mischen → App sieht Original-Reihenfolge
8. **Channel-PSK-Rewrite:** keine Re-Klassifikation, alte HASHTAG-Eintraege bleiben dort

### Build-Size-Schaetzung

- +3-5 KB `.text` (TextBucketStore + Edits)
- +34 KB BSS (Slot-Arrays)
- Code-Volumen: ~400-500 Zeilen neu, ~50 Zeilen geaendert

### Risiken / Edge Cases

1. `getNodenumFromRequestId`-Round-Trip-Loop muss seq_no mit-rotieren — subtiler Bug-Vektor
2. Concurrency: Radio-RX-Task vs. BLE-Task → `concurrency::Lock` zwingend
3. `MeshPacket`-Pool-Ownership: Bucket-Slots NIE an `packetPool.release()` zurueckgeben — Ownership-Check kritisch
4. Magic-Header `"TB01"` bei Schema-Bruch fail-soft (Datei loeschen statt mis-decoden)
5. ESP32 classic: bei Default-Slot-Zahlen RAM-Budget pruefen, ggf. halbieren
