#include "GeoPresetSwitcher.h"

#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "configuration.h"
#include "main.h"

GeoPresetSwitcher geoPresetSwitcher;

// noinit RAM that survives a software reboot but not a power cycle / reset
// pin. AUTO_OVERRIDE_MAGIC is written by AdminModule when the user changes
// the modem preset manually; the auto-switch reads it and stays out of the
// way until the next power cycle.
#if defined(ARCH_ESP32)
#define DL9SAU_NOINIT_ATTR RTC_NOINIT_ATTR
#elif defined(__GNUC__) && !defined(ARCH_PORTDUINO)
#define DL9SAU_NOINIT_ATTR __attribute__((section(".noinit")))
#else
#define DL9SAU_NOINIT_ATTR
#endif

static DL9SAU_NOINIT_ATTR uint32_t s_autoModeOverrideMagic;
static constexpr uint32_t AUTO_OVERRIDE_MAGIC = 0xA51A5A51UL;

// Cadence: re-check region at most every 20 min; cooldown after a switch
// is the same 20 min so we don't flap if the user is right on a border.
static constexpr uint32_t EVALUATE_INTERVAL_MS = 20UL * 60UL * 1000UL;
static constexpr uint32_t COOLDOWN_AFTER_SWITCH_MS = 20UL * 60UL * 1000UL;

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
    s_autoModeOverrideMagic = AUTO_OVERRIDE_MAGIC;
    LOG_INFO("GeoPresetSwitcher: user override engaged (survives soft-reboot, "
             "clears on power-cycle)");
}

bool GeoPresetSwitcher::prerequisitesOk() const
{
    // Feature is only active in EU868 and only with use_preset=true. Users
    // running custom SF/BW are intentionally exempt.
    if (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_EU_868)
        return false;
    if (!config.lora.use_preset)
        return false;
    // User override active → stay out of the way.
    if (s_autoModeOverrideMagic == AUTO_OVERRIDE_MAGIC)
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

    // Cadence throttle. lastEvaluateMs=0 lets the first call after boot run
    // immediately; subsequent calls are throttled.
    if (lastEvaluateMs != 0 && (now - lastEvaluateMs) < EVALUATE_INTERVAL_MS)
        return;
    lastEvaluateMs = now;

    // Cooldown after a recently fired switch (we'll be rebooting anyway,
    // but be defensive in case the reboot is deferred).
    if (lastSwitchMs != 0 && (now - lastSwitchMs) < COOLDOWN_AFTER_SWITCH_MS)
        return;

    if (!prerequisitesOk())
        return;

    // Need a valid position. (0,0) is a sentinel for "no fix yet".
    if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0)
        return;

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
