#include "configuration.h"

#ifdef BLUETOOTH_MAY_SLEEP

#include "bluetooth/BLEPowerCycler.h"
#include "bluetooth/BluetoothPowerControl.h"
#include "mesh/NodeDB.h"     // global config.bluetooth.enabled / config.device.role
#include "target_specific.h" // setBluetoothEnable

#include <Arduino.h> // millis()

#if !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "nimble/NimbleBluetooth.h"
extern NimbleBluetooth *nimbleBluetooth;
#endif

namespace BLEPowerCycler
{

// --- Konstanten ---------------------------------------------------

// Tick-Rate aus dem Main-Loop. 1s ist ein guter Kompromiss: schnell
// genug fuer State-Reaktion, langsam genug um den Loop nicht zu
// belasten.
static const uint32_t TICK_INTERVAL_MS = 1000UL;

// Wie lange nach Boot BT garantiert an bleibt -- App muss in Ruhe
// pairen koennen.
static const uint32_t BOOT_GRACE_MS = 10UL * 60 * 1000;

// Wie lange nach Disconnect BT noch an bleibt -- User koennte gleich
// wiederkommen, dann ist sticky-on angenehmer als Cycle.
static const uint32_t HOT_START_MS = 5UL * 60 * 1000;

// Cycle-Phasen.
//
// Recency-Bonus aus MyMesh.cpp:11131-11138: in den ersten 10 min nach
// letztem Disconnect laeuft der schnellere 20/20-Cycle (User wird
// vermutlich bald reconnecten wollen, kuerzeres Sleep = schnelleres
// Wiederfinden). Danach faellt es auf den sparsameren 20/40-Standard.
static const uint32_t WAKE_MS = 20UL * 1000;
static const uint32_t SLEEP_DEFAULT_MS = 40UL * 1000;
static const uint32_t SLEEP_RECENCY_MS = 20UL * 1000;
static const uint32_t RECENCY_WINDOW_MS = 10UL * 60 * 1000;

// --- State --------------------------------------------------------

enum State { S_BOOT, S_AWAKE, S_HOT_START, S_WAKE, S_SLEEP, S_PERMANENT_OFF };

static State s_state = S_BOOT;
static uint32_t s_lastTick = 0;
static uint32_t s_bootGraceUntil = 0;
static uint32_t s_hotStartUntil = 0;
static uint32_t s_wakeUntil = 0;
static uint32_t s_sleepUntil = 0;

// 0 = nie disconnected. Wird auf millis() gesetzt sobald wir
// AWAKE -> HOT_START gehen (echter Disconnect). BOOT -> HOT_START
// triggert das NICHT (User hat nie verbunden, Recency macht da keinen
// Sinn). Bleibt fuer die Recency-Berechnung erhalten ueber spaetere
// Reconnects hinweg -- wird beim naechsten Disconnect ueberschrieben.
static uint32_t s_lastDisconnectAt = 0;

// Tracking ob unser letzter "BT sollte an/aus sein"-Befehl schon ans
// setBluetoothEnable durchgereicht wurde -- vermeidet Spam-Calls
// jeden Tick. Phase 0 selber ist idempotent, das hier ist nur Code-
// Hygiene.
static bool s_desiredOn = true;
static bool s_lastIssuedOn = false;

// --- Helpers ------------------------------------------------------

static bool isConnected()
{
#if !MESHTASTIC_EXCLUDE_BLUETOOTH
    if (!nimbleBluetooth)
        return false;
    return nimbleBluetooth->isConnected();
#else
    return false;
#endif
}

static void ensureOn()
{
    s_desiredOn = true;
    if (!s_lastIssuedOn) {
        // setBluetoothEnable(true) checkt isActive() -- nach powerSleep ist
        // bleServer=nullptr, also isActive()=false, also wird setup() neu
        // gerufen. Re-init kommt out-of-the-box.
        setBluetoothEnable(true);
        s_lastIssuedOn = true;
    }
}

static void ensureOff()
{
    s_desiredOn = false;
    if (s_lastIssuedOn) {
        // Strategy B: NimBLE-Host deinit statt setBluetoothEnable(false).
        // Letzteres wuerde nur den Controller togglen (Phase 0), was bei
        // NimBLE in inkonsistentem Host-State endet -- Re-Connect-Drama.
        // powerSleep teart Host + Server + Pointer komplett ab.
#if !MESHTASTIC_EXCLUDE_BLUETOOTH
        if (nimbleBluetooth) {
            nimbleBluetooth->powerSleep();
        }
#endif
        s_lastIssuedOn = false;
    }
}

static void transition(State next)
{
    s_state = next;
    uint32_t now = millis();
    switch (next) {
    case S_HOT_START:
        s_hotStartUntil = now + HOT_START_MS;
        break;
    case S_WAKE:
        s_wakeUntil = now + WAKE_MS;
        break;
    case S_SLEEP: {
        // Recency-Check: innerhalb 10 min nach letztem Disconnect ->
        // schneller Cycle (20s sleep statt 40s), damit reconnect-
        // willige User schneller wiedergefunden werden.
        bool recency = (s_lastDisconnectAt != 0) && ((now - s_lastDisconnectAt) < RECENCY_WINDOW_MS);
        s_sleepUntil = now + (recency ? SLEEP_RECENCY_MS : SLEEP_DEFAULT_MS);
        break;
    }
    default:
        break;
    }
    LOG_INFO("BLEPowerCycler: -> %s", currentStateName());
}

static bool roleAllowsBluetooth()
{
    // Headless Infrastructure-Rollen -- nutzen BT nicht.
    //
    //   ROUTER, ROUTER_LATE  -- gewollt headless, Mesh-Backbone
    //   REPEATER             -- deprecated, dumb forwarder. AdminModule.cpp:869
    //                           wandelt es bei der naechsten Config-Aenderung
    //                           zu CLIENT um, aber bis dahin respektieren wir
    //                           die headless-Intention des Users.
    //
    // Alles andere (CLIENT-Familie, TRACKER, SENSOR, TAK, TAK_TRACKER,
    // CLIENT_HIDDEN, LOST_AND_FOUND, CLIENT_BASE, und das deprecated
    // ROUTER_CLIENT das de-facto CLIENT ist) profitiert vom Cycler.
    // Falls eine spezifische Rolle BT gar nicht braucht, kann der User
    // immer noch config.bluetooth.enabled=false setzen -> PERMANENT_OFF.
    auto role = config.device.role;
    if (role == meshtastic_Config_DeviceConfig_Role_ROUTER)
        return false;
    if (role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE)
        return false;
    if (role == meshtastic_Config_DeviceConfig_Role_REPEATER)
        return false;
    return true;
}

// --- Public API ---------------------------------------------------

void setup()
{
    s_state = S_BOOT;
    s_bootGraceUntil = millis() + BOOT_GRACE_MS;
    s_lastIssuedOn = false; // forciert ersten ensureOn() Aufruf
    s_desiredOn = true;
    LOG_INFO("BLEPowerCycler: setup, BOOT-grace %lu ms", (unsigned long)BOOT_GRACE_MS);
}

void tick()
{
    uint32_t now = millis();
    if (now - s_lastTick < TICK_INTERVAL_MS)
        return;
    s_lastTick = now;

    // Universelle Gates: koennen jederzeit den State auf PERMANENT_OFF
    // ziehen.
    bool userWantsBt = (config.bluetooth.enabled == true);
    bool roleOk = roleAllowsBluetooth();

    if (!userWantsBt || !roleOk) {
        if (s_state != S_PERMANENT_OFF) {
            transition(S_PERMANENT_OFF);
        }
        ensureOff();
        return;
    }

    // Aus PERMANENT_OFF zurueckkehren falls User wieder einschaltet
    // bzw Rolle wieder erlaubt.
    if (s_state == S_PERMANENT_OFF) {
        // Re-entry ueber BOOT-Grace statt direkt Cycle -- gibt der App
        // wieder Zeit zum Pairen.
        s_bootGraceUntil = now + BOOT_GRACE_MS;
        transition(S_BOOT);
        // fallthrough zu BOOT-Logik unten
    }

    switch (s_state) {
    case S_BOOT:
        ensureOn();
        if (isConnected()) {
            transition(S_AWAKE);
        } else if (now >= s_bootGraceUntil) {
            transition(S_HOT_START);
        }
        break;

    case S_AWAKE:
        ensureOn();
        if (!isConnected()) {
            // Echter Disconnect -- Timestamp fuer Recency-Bonus setzen.
            // BOOT -> HOT_START (boot-grace abgelaufen ohne Connect)
            // setzt dies bewusst NICHT -> Cycle laeuft dort gleich auf
            // 20/40-Default ohne Recency.
            s_lastDisconnectAt = now;
            transition(S_HOT_START);
        }
        break;

    case S_HOT_START:
        ensureOn();
        if (isConnected()) {
            transition(S_AWAKE);
        } else if (now >= s_hotStartUntil) {
            transition(S_SLEEP);
        }
        break;

    case S_WAKE:
        ensureOn();
        if (isConnected()) {
            transition(S_AWAKE);
        } else if (now >= s_wakeUntil) {
            transition(S_SLEEP);
        }
        break;

    case S_SLEEP:
        ensureOff();
        if (now >= s_sleepUntil) {
            transition(S_WAKE);
        }
        break;

    case S_PERMANENT_OFF:
        // wird oben behandelt
        break;
    }
}

const char *currentStateName()
{
    switch (s_state) {
    case S_BOOT:
        return "BOOT";
    case S_AWAKE:
        return "AWAKE";
    case S_HOT_START:
        return "HOT_START";
    case S_WAKE:
        return "WAKE";
    case S_SLEEP:
        return "SLEEP";
    case S_PERMANENT_OFF:
        return "PERMANENT_OFF";
    }
    return "?";
}

} // namespace BLEPowerCycler

#endif // BLUETOOTH_MAY_SLEEP
