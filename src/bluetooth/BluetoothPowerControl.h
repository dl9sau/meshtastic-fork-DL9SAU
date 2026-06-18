#pragma once

// DL9SAU BLE Power Control — plattform-uebergreifender Wrapper fuer
// "BLE-Controller wirklich aus/ein" jenseits des reinen Advertising-
// Toggles.
//
// Phase 0 (2026-06-18): gegated hinter BLUETOOTH_MAY_SLEEP. Ohne diesen
// Define ist das gesamte Modul ein No-Op und die Funktionen sind nicht
// vorhanden -- Caller muessen ihre Aufrufe ebenfalls #ifdef-gaten.
//
// ESP32: ruft esp_bt_controller_disable/_enable. Host (NimBLE
// bleServer/bleService/Characteristics) bleibt intakt, nur das
// Radio-Modul wird abgeschaltet. Spar-Effekt 70+ mA gemessen in
// MeshCore Phase F (Wunschliste 58, Commit 8ae3b492 / a4293243).
//
// NRF52: Phase 0 = Pass-through (Guard-Flag pflegen, sonst no-op).
// setBluetoothEnable hat dort bereits einen funktionalen disable-Pfad
// in main-nrf52.cpp:190-229. Phase 1 entscheidet ob/wie wir hier
// genauer hooken.
//
// Cross-Ref:
//   Wishlist-DL9SAU.md (Eintrag 2026-06-18)
//   ~/MeshCore-git/Wunschliste-DL9SAU.txt Eintrag 85
//   ~/MeshCore-git/src/helpers/esp32/SerialBLEInterface.cpp:220-279
//     (Referenz-Implementierung mit Phase-F-Lockup-Guard _ctrl_disabled)

#ifdef BLUETOOTH_MAY_SLEEP

namespace BluetoothPowerControl
{
// True wenn der Controller per disableController() abgeschaltet ist.
// Stack-Calls auf NimBLE-Pointer (bleServer->getConnectedCount,
// notify(), Advertising->start) sind in diesem Zustand UNSICHER und
// koennen lockup-aehnliches Verhalten ausloesen.
// Wird von NimbleBluetooth-Methoden als Guard geprueft, bevor sie in
// den Stack rufen.
bool isControllerDisabled();

// Schaltet den BLE-Controller ab. Idempotent.
// Reihenfolge wichtig: Guard-Flag wird ZUERST gesetzt, damit parallel
// laufende Loops (PowerFSM-Tick, OSThread::runOnce) sofort returnen
// koennen statt in den gleich abgeschalteten Stack zu fassen. Genau
// diese Lockup-Falle hat MeshCore in Phase F erlebt (Commit 8ae3b492
// revertiert in a4293243, dann mit Guard re-implementiert).
//
// ESP32: esp_bt_controller_disable() wenn status == ENABLED.
// NRF52:   no-op (Guard wird trotzdem gesetzt fuer Symmetrie).
void disableController();

// Schaltet den BLE-Controller wieder an. Idempotent.
// Returns true bei Erfolg, false bei API-Fehler -- Guard bleibt dann
// gesetzt, damit Caller weiss dass der Stack noch nicht safe ist.
//
// ESP32: esp_bt_controller_enable(ESP_BT_MODE_BLE) wenn status == INITED.
//   Status ENABLED = Boot-Zustand wo NimBLEDevice::init() den
//   Controller bereits hochgefahren hat -- dort kein Re-Enable noetig.
// NRF52:   no-op.
bool enableController();

} // namespace BluetoothPowerControl

#endif // BLUETOOTH_MAY_SLEEP
