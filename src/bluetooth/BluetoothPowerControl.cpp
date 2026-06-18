#include "configuration.h"

#ifdef BLUETOOTH_MAY_SLEEP

#include "bluetooth/BluetoothPowerControl.h"

#ifdef ARCH_ESP32
#include "esp_bt.h"
#endif

namespace BluetoothPowerControl
{

namespace
{
// Guard-Flag analog MeshCore SerialBLEInterface::_ctrl_disabled
// (Wunschliste 58 Phase F Retry 2026-06-14).
//
// Verhindert dass Loop-Code (PowerFSM-Tick, BluetoothPhoneAPI runOnce,
// updateBatteryLevel, sendLog) in den abgeschalteten BLE-Stack ruft.
// Ohne diesen Guard im 1. MeshCore-Versuch: Lockup nach
// esp_bt_controller_disable, weil parallele Tasks noch in
// bleServer->getConnectedCount() / Characteristic->notify() rangingen.
bool s_controllerDisabled = false;
} // namespace

bool isControllerDisabled()
{
    return s_controllerDisabled;
}

void disableController()
{
    // Guard ZUERST setzen -- parallele Loops sehen sofort 'aus' und
    // returnen ohne in den gleich abgeschalteten Stack zu fassen.
    s_controllerDisabled = true;

#ifdef ARCH_ESP32
    // Status-Check verhindert Doppel-Disable. Empirisch (2026-06-18):
    // wenn powerSleep zuvor NimBLEDevice::deinit(true) gerufen hat,
    // ist Status hier bereits UNINITIALIZED -> dieser Call ist no-op.
    // Phase 0 / explizite Off-Pfade fallen aber durch hier durch wenn
    // Status ENABLED, und schalten den Controller dann tatsaechlich aus.
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
    }
#endif
    // NRF52: kein Controller-Toggle hier. main-nrf52.cpp
    // setBluetoothEnable(false) ruft bereits NRF52Bluetooth::shutdown()
    // welches Bluefruit korrekt aus-faehrt.
}

bool enableController()
{
#ifdef ARCH_ESP32
    // Status INITED = 'initialisiert aber nicht enabled' (= unser
    // disabled-Zustand). Status ENABLED = Boot-Zustand wo NimBLEDevice
    // ::init() bereits den Controller hochgefahren hat -- dort waere
    // ein erneutes Enable ein API-Fehler.
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_err_t err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (err != ESP_OK) {
            // Guard bleibt gesetzt -- weiterer Stack-Call wuerde
            // sonst hangen weil Controller noch tot ist.
            return false;
        }
    }
#endif

    // Guard erst NACH erfolgreichem Enable loeschen.
    s_controllerDisabled = false;
    return true;
}

} // namespace BluetoothPowerControl

#endif // BLUETOOTH_MAY_SLEEP
