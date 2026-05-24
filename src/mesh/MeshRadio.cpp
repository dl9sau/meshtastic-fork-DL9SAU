#include "MeshRadio.h"

#include "NodeDB.h" // for extern config

/**
 * DL9SAU: out-of-line definitions for the Stage 2 power helpers. Kept
 * here so MeshRadio.h stays free of the NodeDB.h dependency.
 */

int8_t effectiveConfiguredTxPower()
{
    int p = config.lora.tx_power;
    if (myRegion) {
        if (p == 0 || p > myRegion->powerLimit)
            p = myRegion->powerLimit;
    }
    if (p == 0)
        p = 17;
    return (int8_t)p;
}

int8_t reducedTxPowerForStage2()
{
    int p = (int)effectiveConfiguredTxPower() - 6;
    if (p < 10)
        p = 10;
    return (int8_t)p;
}
