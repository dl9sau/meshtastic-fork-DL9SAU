#pragma once

#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include <stdint.h>

/**
 * DL9SAU: Geo-fenced auto modem-preset switch.
 *
 * Watches the local GPS position and, if the device enters one of a small
 * hardcoded set of bounding boxes (Berlin, Stuttgart/Tübingen, Rheinland,
 * ...), switches config.lora.modem_preset to the region's community
 * standard. Reuses the AdminModule's auto-rename + save + reboot path so
 * the primary channel name follows the preset.
 *
 * Active only when:
 *   - config.lora.region == EU868
 *   - config.lora.use_preset == true
 *
 * User-override: any manual modem_preset change made via AdminModule (e.g.
 * from the phone app) sets a magic-word in noinit RAM. The auto-switch
 * skips while this magic is set. The noinit RAM survives a software
 * reboot (so our own auto-reboot doesn't reset the override either) but
 * is lost on a power cycle or hardware reset — so power-cycling the
 * device re-enables auto-mode, as requested.
 */
class GeoPresetSwitcher
{
  public:
    /** Region descriptor — axis-aligned bounding box in 1e-7 degrees,
     *  matching meshtastic_Position.latitude_i / longitude_i. */
    struct Region {
        int32_t lat_min_i;
        int32_t lat_max_i;
        int32_t lon_min_i;
        int32_t lon_max_i;
        meshtastic_Config_LoRaConfig_ModemPreset preset;
        const char *name; // for log output only
    };

    /** Call periodically (cheap; internal cadence throttle filters). */
    void evaluate();

    /** Marker set by AdminModule when the user changes modem_preset
     *  manually. Survives soft-reset, cleared on power-cycle. */
    static void markUserOverride();

    /** Clear the user-override magic explicitly. Called from
     *  Power::shutdown() so that a user-driven power-off + power-on
     *  cycle re-enables auto-switching even on hardware (like T1000-E)
     *  where "off" is actually System OFF mode and the GPREGRET2
     *  register would otherwise survive it. */
    static void clearUserOverride();

    /** True if the auto-switch is currently in progress (used by
     *  AdminModule so it does NOT mark a user-override when the
     *  config change actually originates from us). */
    bool isAutoSwitchInProgress() const { return autoSwitchInProgress; }

    /** DL9SAU Stage 6: return the region-default modem preset for the
     *  current GPS position. Returns LongFast when the device is
     *  outside all known regions, or when no GPS fix is yet available.
     *  Read-only; does not trigger any switch. */
    meshtastic_Config_LoRaConfig_ModemPreset regionDefaultPreset() const;

  private:
    bool prerequisitesOk() const;
    const Region *findRegionFor(int32_t lat_i, int32_t lon_i) const;
    void triggerSwitch(meshtastic_Config_LoRaConfig_ModemPreset newPreset);

    uint32_t lastEvaluateMs = 0;
    uint32_t lastSwitchMs = 0;
    bool autoSwitchInProgress = false;
    bool bootLogDone = false;
};

extern GeoPresetSwitcher geoPresetSwitcher;
