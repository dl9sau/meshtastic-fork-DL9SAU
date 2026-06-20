#pragma once
#include "BluetoothCommon.h"

class NimbleBluetooth : BluetoothApi
{
  public:
    void setup();
    void shutdown();
    void deinit();
    void clearBonds();
    bool isActive();
    bool isConnected();
    int getRssi();
    void sendLog(const uint8_t *logMessage, size_t length);
    // DL9SAU 2026-06-18 Phase 0: public auf beiden Pfaden, damit der
    // BLUETOOTH_MAY_SLEEP-Wake-Code in setBluetoothEnable() Advertising
    // nach esp_bt_controller_enable() neu starten kann.
    void startAdvertising();

    // DL9SAU 2026-06-18 Phase 1 Strategy B: NimBLE-Host deinit fuer den
    // BLE-Power-Cycler. Variante B aus dem urspruenglichen Plan -- volle
    // Strom-Ersparnis, deutlich mehr Bug-Surface als Phase 0 (Variante B
    // = esp_bt_controller_disable allein hat bei NimBLE den Host in
    // inkonsistenten State hinterlassen, Re-Connects flackerten sofort
    // weg). powerSleep teart komplett ab; der Wake-Pfad laeuft ueber
    // setBluetoothEnable(true) -> isActive()==false -> setup() neu, das
    // bleServer/Service/Characteristic-Pointer frisch erzeugt.
    // Nur fuer non-NIMBLE_TWO ESP32 implementiert.
    void powerSleep();
    bool isDeInit = false;

  private:
    void setupService();
};

#ifdef BLUETOOTH_MAY_SLEEP
// DL9SAU 2026-06-19 Q&D Reconnect-Bug-Workaround. Phase-1-Cycler's
// deinit/reinit bricht NimBLE-Reconnect fuer bonded Peers (siehe
// Wishlist-DL9SAU.md Known-Issue + Loesungs-Pfade A/B/C/D).
// Heuristik: wenn nach erstem SLEEP-Cycle ein Disconnect kommt OHNE
// dass vorher Authentication-Complete feuerte (= broken-reconnect-Pattern),
// triggern wir einen ESP-Reboot. Phone reconnectet dann in BOOT_GRACE
// gegen frischen NimBLE-State -> klappt.
// Public Query: BLEPowerCycler::tick() (main-task) prueft das Flag
// und reagiert mit ESP.restart() statt mid-onDisconnect-callback.
bool blePendingRebootForReconnectFix();
#endif

void setBluetoothEnable(bool enable);
void clearNVS();