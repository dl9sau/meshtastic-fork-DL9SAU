# Wishlist DL9SAU

Sammlung offener Ideen / Pendenzen aus Recherche-Sessions. Nicht-priorisiert.
Auf den hier festgehaltenen Stand spaeter zurueckkommen.

---

## 2026-06-07 — Stage 6: alternierende Companion-Bake fuer non-LF Region + non-default Preset ✅ DONE

**Status:** Implementiert 2026-06-08 in Commit `9717fb4b1`. Build-Kosten: +144 B Flash, 0 RAM.

Files: `src/configuration.h`, `src/mesh/GeoPresetSwitcher.{h,cpp}` (neuer Accessor `regionDefaultPreset()`), `src/modules/PositionModule.{h,cpp}` (Decision-Matrix + Alternation-Counter), `Changelog-DL9SAU.txt`.

Master-Switch `DL9SAU_STAGE6_ALTERNATING_COMPANION` default 1.

Vor-Ort-Test in Berlin geplant — bei MS-Preset alternieren LF/MF im ~1h-Rhythmus.

### Motivation

Stage 4 sendet aktuell ~1×/h eine Position-Companion-Bake auf LongFast wenn das aktuelle Preset != LongFast ist. Hintergrund: Lost-Device-Recovery — Touristen-Geraete defaulten auf LF, also kann ein zufaelliger LF-User dich hoeren.

**Sonderfall:** Wir sind in einer **nicht-LF Region** (z.B. Berlin = MediumFast) **und** haben aus persoenlichen Gruenden das Preset weiter veraendert (z.B. von MF auf MediumSlow). Dann erreichen unsere normalen Broadcasts **weder** Berlin-Locals (auf MF) **noch** LF-Touristen.

### Entscheidungsmatrix

| Mein Preset | Region-Default | Normale Bake | Stage-4 Companion |
|---|---|---|---|
| LongFast | egal | LF | – (keine) |
| != LF | LongFast | mein Preset | LF |
| == Region-Default | != LF | Region-Default (z.B. MF) | LF (fuer LF-Touristen) |
| **!= Region-Default UND != LF** | **!= LF** | mein Preset (z.B. MS) | **alternierend LF und Region-Default** |

Throttle bleibt 1×/h Gesamtfrequenz Companion-Output. Air-Time identisch zu heute.

### Logik-Skizze

```c
ModemPreset nextCompanionPreset() {
    auto current = config.lora.modem_preset;
    auto region_default = GeoPresetSwitcher::regionDefaultPreset(); // NEU

    if (current == LONG_FAST) return NONE;
    if (region_default == LONG_FAST) return LONG_FAST;       // klassisch
    if (region_default == current)   return LONG_FAST;       // klassisch (Locals erreicht durch normale Bake)

    // Sonderfall: weder Locals noch LF-User erreicht — alterniere
    static uint8_t count = 0;
    return (count++ % 2 == 0) ? LONG_FAST : region_default;
}
```

### Implementierungs-TODO

1. **`GeoPresetSwitcher::regionDefaultPreset()` Accessor** — Stage 3 hat schon eine region→preset Tabelle, brauchen nur eine Lookup-Methode die per aktueller Position das Default-Preset zurueckgibt
2. **Stage-4-Companion-Code generalisieren** — heute hartcoded auf LongFast. `setupCompanionDefaultPresetCrypto(preset)` existiert schon (`Channels.h:110`), funktioniert vermutlich auch fuer andere Presets
3. **State fuer Alternation** — static counter im Stage-4-Module reicht (Persistenz ueber Reboot nicht noetig; nach Reboot startet wieder mit LF, fair)
4. **Edge Cases testen:**
   - GPS noch nicht gelockt → `region_default = LONG_FAST` fallback
   - In Region-Grenzgebiet, Region wechselt → unkritisch, Counter laeuft weiter
   - Crypto-Setup-Bugs aehnlich Stage-4-Follow-ups (Channel-Hash vs Channel-Index) — vorsicht!

### Aufwand

~30-50 Zeilen Code. Sorgfaeltig zu testen wegen Crypto-Setup (siehe `companion_crypto_ready` Logik in Router.cpp aus Stage 4 Follow-ups).

---

## 2026-06-06 — Mobile NodeDB-Aging via Distanz + Zeit ❌ LOHNT NICHT (erledigt)

**Status:** Diskutiert, **nicht implementiert**, vermutlich auch nicht noetig.

### Ursprungsidee

Auf Reisen Knoten loeschen die weit weg sind:
- CLIENT-Familie: > 3 km, seit > 1 h
- ROUTER/REPEATER-Familie: > 10 km, seit > 1 h

### Warum die einfache Distanz-Regel haengt

LoRa-Reichweite ist nicht distanz-basiert sondern terrain-basiert:
- Berg-Repeater 25 km Line-of-Sight: bestens erreichbar
- CLIENT 800 m im Haeuserschatten: unerreichbar
- Stadt-Spaziergang ueberschreitet temporaer 3 km Radius → wuerde gute Nachbarn verlieren

### Refinierter Algorithmus (waere die saubere Variante)

**Trigger:** `numMeshNodes > 0.85 * MAX_NUM_NODES` **UND** mobil (GPS-Speed > 5 km/h oder Position drifted > 5 km/h).

**Eviction-Reihenfolge:**
1. Niemals Favoriten loeschen
2. `last_heard > 6 h` darf weg (Zeit-Backstop, unabhaengig von allem)
3. Mit Position und distanz-fern: CLIENT-Familie > 10 km, ROUTER-Familie > 50 km
4. Ohne Position konservativ behalten

### Warum trotzdem nicht implementieren

- **Meshtastic hat keine eingebaute Aging-Mechanik** (`cleanupMeshDB()` putzt nur incomplete-Eintraege). Insofern ein **echter Gap** im Upstream.
- ABER: bei `MAX_NUM_NODES=80` (nRF52) / 100 (sonst) gibt's selten echten Druck im Normalbetrieb
- **Risiko der Falsch-Eviction** (Berg-Repeater verlieren) > erwarteter Nutzen
- **Real-World-Daten fehlen** — ohne Beobachtung in der Praxis ist's spekulative Optimierung

### Wann doch reaktivieren

Nur wenn auf Reisen tatsaechlich beobachtet wird:
- NodeDB regelmaessig voll (`isFull()` triggert oft)
- Knoten ausgesperrt die eingetragen sein sollten
- Routing-Entscheidungen durch stale Knoten verfaelscht

Dann **ja, refinierte Variante umsetzen**. Aber nicht spekulativ.

---

## 2026-06-05 — Stage 5: Delay-buffered broadcast relay for CLIENT_BASE

### Motivation

Stage 2's B2-default setzt `hop_limit=0` auf CLIENT_BASE-Relays um Downstream-Air-Time zu sparen. Nebeneffekt aus `FloodingRouter::perhapsCancelDupe` (`src/mesh/FloodingRouter.cpp:140-147`): Nachbarn mit Rolle CLIENT (oder CLIENT_BASE-non-favorited) die das Original schon gehoert und fuer Wiederholung eingequeuet hatten, **canceln** ihre Wiederholung wenn sie unseren `hop_limit=0`-Relay hoeren. Folge: Downstream-Nodes die nur ueber jene Nachbarn erreicht werden, sehen das Paket nicht.

### Idee

Statt sofort mit `hop_limit=0` zu relayen: **mode-abhaengig verzoegern**, dann mit `hop_limit=0` senden. Bis dahin haben alle anderen Relays ihren Versuch durchgefuehrt (oder selber gecancelled). Unser nachgeschobener `hop_limit=0`-Relay trifft auf eine leere TX-Queue der Nachbarn → `perhapsCancelDupe` ist harmlos. Downstream-Nodes die nur uns hoeren bekommen das Paket trotzdem, nur spaeter.

**Explizit KEIN Self-Cancel:** Wenn wir waehrend des Delays einen anderen Relay hoeren, senden wir trotzdem. Begruendung: CLIENT_BASE auf Dach/Balkon kann der einzige Relay sein fuer ein CLIENT_MUTE drinnen — Schweigen wegen anderer Outdoor-Relays wuerde Inside-Geraet aushungern.

### Scope

Nur **B2-default Broadcasts** auf **CLIENT_BASE**:

| Pakettyp | Stage-5-Behandlung |
|---|---|
| DM, B3 (favorite + direkter Nachbar) | unveraendert — sofort, full hops |
| DM, B3 (first-relay) | unveraendert — sofort, full hops |
| DM, B2-transit | **unveraendert — sofort, hop_limit=0** (Latenz wichtiger als Cancel-Vermeidung) |
| Broadcast (Text/Position/NodeInfo/Admin/Alert/Waypoint/SF/KeyVerification) | **NEU: delay + hop_limit=0, kein Self-Cancel** |
| Telemetry / non-Whitelist | unveraendert — Drop |

### Delay-Tabelle (mode-abhaengig)

Faustregel: groesser als typischer `getTxDelayMsecWeightedWorst()` der anderen Nodes im selben Preset.

| Modem-Preset | Delay |
|---|---|
| `VLongSlow` (SF12/BW125) | 8000 ms |
| `LongMod` (SF11/BW125) | 5000 ms |
| `LongFast` (SF11/BW250) | 3000 ms |
| `MediumSlow` (SF10/BW250) | 2000 ms |
| `MediumFast` (SF9/BW250) | 1500 ms |
| `ShortFast` (SF7/BW250) | 800 ms |
| `ShortSlow` (default) | 1500 ms |
| Custom / Unknown | 3000 ms (LongFast-Fallback) |

Compile-time tunable via `DL9SAU_STAGE5_DELAY_MS_<preset>` Sub-Defines.

### Implementations-Mechanik

Meshtastic hat schon das richtige Werkzeug: `meshtastic_MeshPacket.tx_after` (millis-Zeitstempel ab dem die TX-Queue das Paket senden darf). ROUTER_LATE nutzt es via `clampToLateRebroadcastWindow` (`src/mesh/RadioLibInterface.cpp:464`).

Wir setzen `tx_after = millis() + stage5DelayForPreset()` direkt in `applyClientRepeatPolicy` (`FloodingRouter.cpp:163-243`) vor dem Return, im B2-default-Pfad fuer Broadcasts auf CLIENT_BASE. Beim Enqueue in den TX-Layer respektiert `RadioLibInterface::startSend` das `tx_after` Field und schedulet via `notifyLater`.

### Bekannte Trade-Offs

- **Broadcast-Latenz** +3 s (bei LongFast) fuer Downstream-Nodes die nur ueber uns erreichbar sind. Akzeptabel fuer Channel-Chat, Position, NodeInfo.
- **DM-Transit:** Sofort-Relay bleibt → seltener Corner-Case wo unser Relay einen besseren Nachbarn-Relay canceln koennte. Bewusst akzeptiert (Interaktivitaet zaehlt mehr; DM hat Ack-Retry).
- **Keine Probe-then-skip:** Wenn jemand anders im Delay-Window relaied, senden wir trotzdem. Bei dichten Mesh-Setups etwas redundant, aber notwendig fuer Inside-Geraete-Versorgung.

### Files (geplant)

- `src/configuration.h` — neue Defines fuer Stage 5
- `src/mesh/FloodingRouter.cpp` (`applyClientRepeatPolicy`) — tx_after setzen
- `src/mesh/FloodingRouter.cpp` oder neuer Helper — `stage5DelayForPreset()`
- `Changelog-DL9SAU.txt` — Stage-5-Eintrag

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

## 2026-05-30 — Plan: typisierte Text-Buckets fuer `toPhoneQueue` ✅ DONE

**Status:** Implementation abgeschlossen am 2026-05-30.
- Files: `src/mesh/TextBucketStore.{h,cpp}` (neu); Edits in `MeshService.{h,cpp}`, `PhoneAPI.cpp`, `configuration.h`, `platformio.ini`, `Changelog-DL9SAU.txt`.
- Build-Kosten T1000e: **+28 KB BSS**, **+2.3 KB Flash**. Sehr nahe am Plan.
- Tunables (Slot-Halving + Flash-off pro Bucket) als Sub-Defines verfuegbar und in `configuration.h` + `platformio.ini` dokumentiert.
- Bei zukuenftigen Sessions: siehe `src/mesh/TextBucketStore.h` und Changelog-Eintrag 2026-05-30 als Source-of-Truth.

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

---

## 2026-06-18 — Portierung BLE-Power-Spar-Mechanismus aus MeshCore (TODO)

**Status:** TODO. Wartet auf User-GO. Recherche + Plan steht, ESP32-Show-Stopper identifiziert, Implementierungs-Variante festgelegt.

**Cross-Ref MeshCore-Tree:** `~/MeshCore-git/Wunschliste-DL9SAU.txt` Eintrag 85 (lange Recherche, Phasen-Plan, Pseudocode). Dort liegt der vollstaendige Hintergrund inkl. Phase-F-Lockup-Erfahrung mit `_ctrl_disabled`-Guard. **Diese Datei hier ist die Meshtastic-Sicht** mit fokussierten Findings aus dem Meshtastic-Tree.

### Ziel

`esp_bt_controller_disable/_enable` Cycler aus MeshCore (`src/helpers/esp32/SerialBLEInterface.cpp:220-279`) nach Meshtastic portieren — der wirklich-Strom-spart-Mechanismus (70+ mA Gewinn bei MeshCore Phase F), nicht nur Advertising-Off.

### Aktivierung

Per Build-Define **`BLUETOOTH_MAY_SLEEP`** (kein CLI, kein Setting). Meshtastic-Philosophie: Power-Strategien sind Hardware-Frage, nicht User-Entscheidung. Default AUS — wer das will, baut mit dem Flag.

Greift nur wenn `config.bluetooth.enabled == true`. Wenn User BT via App/Display ausschaltet: Cycler geht in `PERMANENT_OFF`.

### Show-Stopper-Befund (Phase 0 → muss zuerst!)

`src/platform/esp32/main-esp32.cpp:32-54` — `setBluetoothEnable(bool)` hat **keinen disable-Zweig**. `setBluetoothEnable(false)` ist auf ESP32 ein **No-Op**. Kommentar im Code:

> "For ESP32, no way to recover from bluetooth shutdown without reboot"

Heisst: bevor irgendein Cycler angesetzt werden kann, muss erst ein funktionierender enable↔disable↔enable Zyklus geschaffen werden. NRF52 hat das schon (`main-nrf52.cpp:190-229` — shutdown funktional).

### Entscheidung: Variante B (direkter Controller-Toggle)

Zwei Varianten standen zur Wahl:

**A) NimBLE deinit/reinit** — `NimBLEDevice::deinit(true)` + spaeter `setup()`. Beruehrt `NimbleBluetooth`-Klassen-Interna: `bleServer`, `bleService`, mehrere `NimBLECharacteristic*` Pointer + `BluetoothStatus` Observer-Subscribers (PhoneAPI). Alle Pointer werden ungueltig, alle Subscriber muessten Reset-fest sein. ~10 dynamische Objekte berueht.

**B) `esp_bt_controller_disable/_enable` direkt** — wie MeshCore. NimBLE-Datenstrukturen bleiben intakt, nur Controller-Hardware aus. Bei Re-Enable: Device-Name + Advertising neu starten via `reapplyControllerState()`-Analog. **1 Funktion betroffen, nicht 10 Pointer.**

→ **Entscheidung: Variante B.** Bug-Surface-Vergleich ist gigantisch. Plus: ihr habt mit Variante B bei MeshCore schon den Phase-F-Lockup durchgekaempft, das Wissen ist da. Variante A waere echte Refactor-Arbeit ohne klaren Gewinn.

### Drei Gruende warum MeshCore das einfacher hatte (fuer das Verstaendnis spaeter)

1. **MeshCore hat einen klaren On/Off-Anker** — `SerialBLEInterface::enable()`/`disable()` sind symmetrisch ausgebaut (SerialBLEInterface.cpp:220-279). Meshtastic hat `setBluetoothEnable` aber nur half-implementiert (siehe Phase 0).

2. **MeshCore hat leichtgewichtigen BLE-Wrapper** — eine flache Klasse, ein paar Pointer, klares Restart-Pattern. Meshtastic hat `NimbleBluetooth` mit fetter Objekt-Hierarchie (Server + Service + N Characteristics + Observer-Subscribers in PhoneAPI). Deshalb Variante A so teuer.

3. **MeshCore hat keinen Mit-Eigentuemer der BT-Lebenszeit** — `manageBlePower()` allein entscheidet. Meshtastic hat **PowerFSM** mit eigenem Anspruch: `src/PowerFSM.cpp:85+` ruft `nbEnter()`/`darkEnter()` selber `setBluetoothEnable(...)`. Ohne Guard → Pingpong: Cycler sagt SLEEP→off, PowerFSM sagt darkEnter→on, repeat. **Im `BLUETOOTH_MAY_SLEEP`-Build muessen PowerFSM-BT-Calls `#ifndef`-gegated werden** (Hoheits-Uebergabe an Cycler).

4. **Bonus: Phase-F-Lockup-Erfahrung schon eingebaut** — euer `_ctrl_disabled` Guard-Flag (SerialBLEInterface.cpp:261-266) verhindert dass parallel laufende Loops in den abgeschalteten Stack rufen. Diese Schmerz-Erfahrung steckt im Meshtastic-Code nicht — wuerde dort als erstes wieder gemacht.

### Integration-Points im Meshtastic-Tree (Recherche-Ergebnis)

| Punkt | Datei:Zeile | Was |
|---|---|---|
| `setBluetoothEnable(bool)` ESP32 | `src/platform/esp32/main-esp32.cpp:32-54` | enable-only, disable-Zweig **fehlt** (Phase 0!) |
| `setBluetoothEnable(bool)` NRF52 | `src/platform/nrf52/main-nrf52.cpp:190-229` | enable + funktionaler disable, beide Pfade da |
| ESP32 Loop-Hook | `src/platform/esp32/main-esp32.cpp:187` | `esp32Loop()` — Cycler-Tick hier rein |
| NRF52 Loop-Hook | `src/platform/nrf52/main-nrf52.cpp:331` | `nrf52Loop()` |
| Connect-Callback ESP32 | `src/nimble/NimbleBluetooth.cpp:658-737` | onConnect/onDisconnect — `lastConnectAt`/`lastDisconnectAt` einklinken |
| Connect-Callback NRF52 | `src/platform/nrf52/NRF52Bluetooth.cpp:60-100` | dito mit BluetoothStatus Observer |
| Connection-Status ESP32 | `NimbleBluetooth.cpp:785` | `bleServer->getConnectedCount() > 0` |
| Connection-Status NRF52 | `NRF52Bluetooth.cpp:52` | `Bluefruit.connected(connectionHandle)` |
| **PowerFSM-Kollision** | `src/PowerFSM.cpp:85+` | `nbEnter()`/`darkEnter()` rufen `setBluetoothEnable` — **MUSS gegated werden** |
| User-Toggle persistent | `config.bluetooth.enabled` Protobuf | kein `onSettingsChanged`-Hook → polling (1-2s im Cycler-Tick) |
| ROUTER-Downcast Admin | `src/modules/AdminModule.cpp:869-874` | nicht relevant fuer Cycler, aber: ROUTER-Rolle → `PERMANENT_OFF` (analog MeshCore) |

### Hardcoded Konstanten (analog MeshCore-Defaults)

```c
static const uint32_t BOOT_GRACE_MS     = 10UL * 60 * 1000;  // 10 min — App muss erstmal pairen
static const uint32_t HOT_START_MS      =  5UL * 60 * 1000;  // 5 min — nach Disconnect noch bereit
static const uint32_t WAKE_MS           = 20UL * 1000;       // 20s on
static const uint32_t SLEEP_RECENCY_MS  = 20UL * 1000;       // 20s off bei recency
static const uint32_t SLEEP_DEFAULT_MS  = 40UL * 1000;       // 40s off default
static const uint32_t RECENCY_WINDOW_MS = 10UL * 60 * 1000;  // 10 min Recency-Fenster
```

### State-Machine (analog MeshCore `MyMesh::manageBlePower`)

`BOOT → AWAKE → HOT_START → WAKE ↔ SLEEP / PERMANENT_OFF`

- `BOOT`: erste 10 min nach Power-On — BT bleibt an (User soll erstmal pairen koennen)
- `AWAKE`: BLE-Verbindung aktiv — sticky on, kein Cycle
- `HOT_START`: gerade disconnected — 5 min on (User koennte gleich wieder kommen)
- `WAKE`/`SLEEP`: Cycle 20s on / 40s off (bzw. 20s wenn recent activity)
- `PERMANENT_OFF`: User hat BT aus via App/Display, oder Rolle = ROUTER

### Phasen-Plan (NEU, mit Phase 0)

| Phase | Aufwand | Inhalt |
|---|---|---|
| **0 (KRITISCH)** | 3-5h | ESP32 BLE enable↔disable↔enable funktionsfaehig machen, isoliert. `setBluetoothEnable(false)`-Zweig schreiben, `esp_bt_controller_disable/_enable` einbauen, `_ctrl_disabled` Guard portieren. Smoke-Test: 10× Toggle ohne Crash. **Bevor irgendwas anderes passiert.** |
| 1 | 4-6h | `src/bluetooth/BLEPowerCycler.{cpp,h}` mit State-Machine, hardcoded Konstanten, ESP32 + NRF52 parallel. PowerFSM-Guards via `#ifdef BLUETOOTH_MAY_SLEEP`. Polling `config.bluetooth.enabled`. |
| 2 | 2-3h | Test Heltec WT V1.1: Strommessung 5-10 min disconnect (Baseline 95 mA vs. mit Cycler). Lockup-Test: BT mehrfach via Display togglen waehrend Cycler laeuft. |

**Gesamt: 9-14h.**

### Quellen

- `~/MeshCore-git/Wunschliste-DL9SAU.txt` Eintrag 85 (vollstaendige Recherche + Pseudocode)
- `~/MeshCore-git/src/helpers/esp32/SerialBLEInterface.cpp:220-279` (Referenz-Implementierung)
- `~/MeshCore-git/src/helpers/esp32/SerialBLEInterface.h:17,24,65` (`_ctrl_disabled` Guard-Dokumentation)
- Phase-F-Lockup-Erkenntnis: Wunschliste-MeshCore Z 4667-4697
- Diese Datei: Meshtastic-Tree-Recherche 2026-06-18 (Sub-Agent Explore)
