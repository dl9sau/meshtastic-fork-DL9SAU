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
    bool isDeInit = false;

  private:
    void setupService();
};

void setBluetoothEnable(bool enable);
void clearNVS();