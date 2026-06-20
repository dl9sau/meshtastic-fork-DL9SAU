#pragma once

// DL9SAU BLE Power Cycler -- Phase 1 (2026-06-18) der Portierung aus
// MeshCore (MyMesh::manageBlePower). State-Machine die periodisch BT
// an/aus schaltet, um Strom zu sparen, ohne die User-Pairing-Erfahrung
// massiv zu verschlechtern.
//
// Layered auf Phase-0-Primitive (BluetoothPowerControl::enable/disable
// Controller + setBluetoothEnable). Phase 0 stellt sicher dass disable/
// enable funktioniert; Phase 1 entscheidet *wann*.
//
// Aktivierung: hinter BLUETOOTH_MAY_SLEEP gegated. Ohne diesen Define
// ist das Modul ein No-Op und PowerFSM/sleep.cpp behalten ihre eigene
// (de facto nicht funktionale auf ESP32) BT-Lifecycle-Hoheit.
//
// State-Machine (Strikt vereinfacht vs MeshCore-Original):
//
//   BOOT        -> 10 min BT auf, User muss pairen koennen
//   AWAKE       -> BLE verbunden, BT bleibt sticky on
//   HOT_START   -> gerade disconnected, 5 min on (User koennte gleich
//                  wiederkommen)
//   WAKE / SLEEP -> Cycle 20s on / 40s off
//   PERMANENT_OFF -> User hat BT explizit aus (config.bluetooth.enabled)
//                    oder Rolle ist ROUTER/ROUTER_LATE
//
// Hardcoded Konstanten (analog MeshCore-Defaults aus MyMesh.cpp):
//   BOOT_GRACE_MS       = 10 * 60 * 1000
//   HOT_START_MS        =  5 * 60 * 1000
//   WAKE_MS             = 20 * 1000
//   SLEEP_DEFAULT_MS    = 40 * 1000
//
// Cross-Ref:
//   Wishlist-DL9SAU.md (Eintrag 2026-06-18)
//   ~/MeshCore-git/src/MyMesh.cpp manageBlePower() (Original-Algorithmus)
//   src/bluetooth/BluetoothPowerControl.{h,cpp} (Phase 0)

#ifdef BLUETOOTH_MAY_SLEEP

namespace BLEPowerCycler
{
// Init beim Boot. Setzt initiale State = BOOT, startet Boot-Grace-Timer.
// Idempotent -- mehrfacher Aufruf hat keine Wirkung.
void setup();

// Periodischer Tick aus dem Main-Loop. Intern rate-limited auf
// TICK_INTERVAL_MS, kann also bedenkenlos in jeder Loop-Iteration
// gerufen werden.
void tick();

// Aktueller Zustand, fuer Diagnostik. Returnt einen kurzen Klartext-
// Namen wie "BOOT", "SLEEP", "PERMANENT_OFF". Pointer auf statischen
// String -- nicht freigeben.
const char *currentStateName();

// Event-getriebener Wake-Trigger fuer Situationen wo der User
// wahrscheinlich gleich seine App oeffnet (eingehende Text-Message,
// eingehender Admin-Befehl). Setzt State auf HOT_START mit frischem
// 5-min-Timer -- gibt dem User ein 5-Minuten-Fenster zum App-Connect
// ohne auf den naechsten 20s WAKE-Cycle warten zu muessen.
//
// Idempotent. Hat KEINE Wirkung in States wo BT eh schon an ist
// (BOOT, AWAKE) -- dort wird nur der HOT_START-Timer fuer einen
// spaeteren Disconnect vorbereitet, BT bleibt sowieso an.
// Hat KEINE Wirkung in PERMANENT_OFF -- respektiert User-explizites
// BT-Off via config.bluetooth.enabled bzw. Headless-Role.
//
// reason: kurzer Klartext fuer LOG_INFO ("rx text", "admin cmd", ...).
void wakeForUserAttention(const char *reason);

} // namespace BLEPowerCycler

#endif // BLUETOOTH_MAY_SLEEP
