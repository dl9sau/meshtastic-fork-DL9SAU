#include "GeoPresetSwitcher.h"

#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "configuration.h"
#include "main.h"

GeoPresetSwitcher geoPresetSwitcher;

// User-override magic that survives a software reboot (e.g. the
// LoRa-config-change reboot) but is cleared by a hardware reset / power
// cycle. The implementation is platform-specific because plain .noinit
// RAM is not reliably retained on nRF52 with SoftDevice — the Adafruit
// bootloader or the C runtime appears to zero the RAM at startup.
//
// nRF52: NRF_POWER->GPREGRET2 — hardware-guaranteed retention across
//        NVIC_SystemReset, hardware-guaranteed zero on power-on / pin
//        reset. (GPREGRET is already used for DFU_MAGIC_SKIP and
//        NRF52_MAGIC_LFS_IS_CORRUPT — we use the second register.)
// ESP32: RTC_NOINIT_ATTR — places the variable in RTC slow memory,
//        same semantics as GPREGRET2 (survives soft reboot / deep
//        sleep, undefined after power-on).
// Other: best-effort .noinit; portduino has no need for this.
//
// 8-bit magic is enough — GPREGRET2 is 8 bits wide. Pick a value that
// doesn't collide with the GPREGRET DFU / LFS markers and isn't 0.
static constexpr uint8_t AUTO_OVERRIDE_MAGIC = 0xA5;

#if defined(ARCH_NRF52)
#include <nrf.h>
#include <nrf_soc.h>
// On nRF52 with an active SoftDevice, direct writes to NRF_POWER->GPREGRET
// registers are masked — you must go through the SoftDevice SVC calls. The
// gpregret_id argument selects which register: 0 = GPREGRET, 1 = GPREGRET2.
// (Upstream main-nrf52.cpp uses the same pattern for the LFS-corrupt and
// DFU-skip magics on GPREGRET.)
static inline void writeOverrideMagic(uint8_t v)
{
    if (sd_power_gpregret_clr(1, 0xFF) != NRF_SUCCESS || sd_power_gpregret_set(1, v) != NRF_SUCCESS) {
        // SoftDevice not initialised — direct register access is the right
        // fallback in that case.
        NRF_POWER->GPREGRET2 = v;
    }
}
static inline uint8_t readOverrideMagic()
{
    uint32_t v = 0;
    if (sd_power_gpregret_get(1, &v) != NRF_SUCCESS) {
        v = NRF_POWER->GPREGRET2;
    }
    return (uint8_t)v;
}
#elif defined(ARCH_ESP32)
RTC_NOINIT_ATTR static uint8_t s_autoModeOverrideMagic;
static inline void writeOverrideMagic(uint8_t v)
{
    s_autoModeOverrideMagic = v;
}
static inline uint8_t readOverrideMagic()
{
    return s_autoModeOverrideMagic;
}
#else
__attribute__((section(".noinit"))) static uint8_t s_autoModeOverrideMagic;
static inline void writeOverrideMagic(uint8_t v)
{
    s_autoModeOverrideMagic = v;
}
static inline uint8_t readOverrideMagic()
{
    return s_autoModeOverrideMagic;
}
#endif

// Cadence: re-check region at most every 20 min; cooldown after a switch
// is the same 20 min so we don't flap if the user is right on a border.
// Hold off the very first evaluation until 2 min after boot so the GPS has
// time to fix and the user has time to (re)configure without auto kicking
// in immediately.
static constexpr uint32_t EVALUATE_INTERVAL_MS = 20UL * 60UL * 1000UL;
static constexpr uint32_t COOLDOWN_AFTER_SWITCH_MS = 20UL * 60UL * 1000UL;
static constexpr uint32_t FIRST_EVAL_AFTER_BOOT_MS = 2UL * 60UL * 1000UL;

// 5 km inward boundary margin, expressed in 1e-7 degrees. 1° latitude ≈
// 111 km, so 5 km ≈ 4.5e-2 ° → 450 000 in 1e-7. For longitude we use a
// fixed conservative value sized for ~50 °N (cos 50° ≈ 0.64), giving
// 5 km ≈ 7.0e-2 ° → 700 000 in 1e-7. Doesn't need to be exact — the
// margin only exists to keep us from flipping on the boundary.
static constexpr int32_t LAT_MARGIN_I = 450000;
static constexpr int32_t LON_MARGIN_I = 750000;

static const GeoPresetSwitcher::Region REGION_PRESETS[] = {
    // Berlin — city proper (Brandenburg deliberately excluded).
    // 52.35°–52.70° N × 13.10°–13.70° E
    {523500000, 527000000, 131000000, 137000000, meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, "Berlin"},
    // Stuttgart / Reutlingen / Tübingen corridor.
    // 48.25°–48.80° N × 8.90°–9.35° E
    {482500000, 488000000, 89000000, 93500000, meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST, "Stuttgart/Tübingen"},
    // Rheinland (Cologne / Bonn area), per meshrheinland.de.
    // 50.50°–51.20° N × 6.50°–7.50° E
    {505000000, 512000000, 65000000, 75000000, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW, "Rheinland"},
};

void GeoPresetSwitcher::markUserOverride()
{
    writeOverrideMagic(AUTO_OVERRIDE_MAGIC);
    LOG_INFO("GeoPresetSwitcher: user override engaged (magic=0x%02x, "
             "survives soft-reboot, clears on power-button-off)",
             (unsigned)AUTO_OVERRIDE_MAGIC);
}

void GeoPresetSwitcher::clearUserOverride()
{
    writeOverrideMagic(0);
    LOG_INFO("GeoPresetSwitcher: user override cleared (next boot will arm auto-switch again)");
}

bool GeoPresetSwitcher::prerequisitesOk() const
{
    // Feature is only active in EU868 and only with use_preset=true. Users
    // running custom SF/BW are intentionally exempt.
    if (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_EU_868)
        return false;
    if (!config.lora.use_preset)
        return false;
    // Infrastructure roles are deliberately deployed at fixed locations
    // with a chosen modem preset; auto-switch must never silently re-tune
    // them. Mobile / "user" roles (CLIENT / CLIENT_BASE / TRACKER /
    // SENSOR / ...) get the auto-switch treatment so they stay
    // compatible with whichever local mesh they roam into.
    const meshtastic_Config_DeviceConfig_Role role = config.device.role;
    if (role == meshtastic_Config_DeviceConfig_Role_REPEATER || role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
        role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE)
        return false;
    // User override active → stay out of the way.
    if (readOverrideMagic() == AUTO_OVERRIDE_MAGIC)
        return false;
    return true;
}

const GeoPresetSwitcher::Region *GeoPresetSwitcher::findRegionFor(int32_t lat_i, int32_t lon_i) const
{
    for (const auto &r : REGION_PRESETS) {
        if (lat_i >= r.lat_min_i + LAT_MARGIN_I && lat_i <= r.lat_max_i - LAT_MARGIN_I &&
            lon_i >= r.lon_min_i + LON_MARGIN_I && lon_i <= r.lon_max_i - LON_MARGIN_I) {
            return &r;
        }
    }
    return nullptr;
}

void GeoPresetSwitcher::evaluate()
{
    const uint32_t now = millis();

    // One-time boot log so the user can see the override-magic state. Done
    // before any early-return so it always appears once per boot.
    if (!bootLogDone) {
        const uint8_t mg = readOverrideMagic();
        LOG_INFO("GeoPresetSwitcher: boot — override magic=0x%02x (%s)", (unsigned)mg,
                 mg == AUTO_OVERRIDE_MAGIC ? "user override ACTIVE, auto-switch disabled until power cycle"
                                           : "auto-switch armed");
        bootLogDone = true;
    }

    // 2-min grace period after boot — GPS needs time to fix, user may
    // still be (re)configuring the device.
    if (now < FIRST_EVAL_AFTER_BOOT_MS)
        return;

    // 20-min cadence between full evaluations. Note: we ONLY advance
    // lastEvaluateMs once we've actually done a real evaluation
    // (= GPS fix was present). If GPS isn't ready yet we want to retry
    // on the next runOnce tick, not wait another 20 min.
    if (lastEvaluateMs != 0 && (now - lastEvaluateMs) < EVALUATE_INTERVAL_MS)
        return;

    // Cooldown after a recently fired switch (we'll be rebooting anyway,
    // but be defensive in case the reboot is deferred).
    if (lastSwitchMs != 0 && (now - lastSwitchMs) < COOLDOWN_AFTER_SWITCH_MS)
        return;

    if (!prerequisitesOk())
        return;

    // Need a valid position. (0,0) is a sentinel for "no fix yet" — do
    // NOT bump lastEvaluateMs in that case, keep trying every runOnce.
    if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0)
        return;

    // From here we've actually got something to evaluate against — engage
    // the 20-min throttle.
    lastEvaluateMs = now;

    // Pick the target preset: matching region, or LongFast as the fallback
    // when the device sits outside all known regions. The fallback ensures
    // someone driving out of e.g. Berlin gets switched back to the global
    // default rather than staying on MediumFast forever.
    const Region *target = findRegionFor(localPosition.latitude_i, localPosition.longitude_i);
    const meshtastic_Config_LoRaConfig_ModemPreset targetPreset =
        target ? target->preset : meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    const char *targetName = target ? target->name : "outside-all-regions";

    if (targetPreset == config.lora.modem_preset)
        return; // already on the right preset

    LOG_INFO("GeoPresetSwitcher: region '%s', switching modem preset %d -> %d", targetName, (int)config.lora.modem_preset,
             (int)targetPreset);
    triggerSwitch(targetPreset);
}

void GeoPresetSwitcher::triggerSwitch(meshtastic_Config_LoRaConfig_ModemPreset newPreset)
{
    // Mark in-progress so AdminModule doesn't misread our own config write
    // as a user override.
    autoSwitchInProgress = true;

    config.lora.modem_preset = newPreset;
    // Mirror the preset's bandwidth/SF/CR back into the explicit fields, same
    // as AdminModule does — keeps the iOS app happy after the auto-switch.
    if (myRegion) {
        float presetBwKHz = 0;
        uint8_t presetSf = 0, presetCr = 0;
        modemPresetToParams(newPreset, myRegion->wideLora, presetBwKHz, presetSf, presetCr);
        config.lora.bandwidth = bwKHzToCode(presetBwKHz);
        config.lora.spread_factor = presetSf;
        if (config.lora.coding_rate < LORA_CR_MIN || config.lora.coding_rate > LORA_CR_MAX) {
            config.lora.coding_rate = presetCr;
        }
    }

    int saveWhat = SEGMENT_CONFIG;
    if (channels.renamePrimaryForPresetChange(newPreset))
        saveWhat |= SEGMENT_CHANNELS;

    service->reloadConfig(saveWhat);

    lastSwitchMs = millis();
    LOG_INFO("GeoPresetSwitcher: switch committed, rebooting in %d s", DEFAULT_REBOOT_SECONDS);
    rebootAtMsec = millis() + (uint32_t)DEFAULT_REBOOT_SECONDS * 1000UL;

    autoSwitchInProgress = false;
}
