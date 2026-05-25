#pragma once
#if RADIOLIB_EXCLUDE_LR11X0 != 1
#include "RadioLibInterface.h"

/**
 * \brief Adapter for LR11x0 radio family. Implements common logic for child classes.
 * \tparam T RadioLib module type for LR11x0: SX1262, SX1268.
 */
template <class T> class LR11x0Interface : public RadioLibInterface
{
  public:
    LR11x0Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                    RADIOLIB_PIN_TYPE busy);

    /// Initialise the Driver transport hardware and software.
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool init() override;

    /// Apply any radio provisioning changes
    /// Make sure the Driver is properly configured before calling init().
    /// \return true if initialisation succeeded.
    virtual bool reconfigure() override;

    /// Prepare hardware for sleep.  Call this _only_ for deep sleep, not needed for light sleep.
    virtual bool sleep() override;

    bool isIRQPending() override { return lora.getIrqFlags() != 0; }

#ifdef LR11X0_AGC_RESET
    void resetAGC() override;
#endif

    /// DL9SAU Stage 2: thin wrappers for per-packet CR / power override.
    /// LR11x0 setCodingRate takes a second arg (longInterleaving) — keep the
    /// reconfigure()'s "long interleaving except CR=4/7" rule consistent here.
    int16_t setRuntimeCodingRate(uint8_t cr) override { return lora.setCodingRate(cr, cr != 7); }
    int16_t setRuntimeTxPower(int8_t dBm) override { return lora.setOutputPower(dBm); }
    /// DL9SAU Stage 4: SF / BW override for the LongFast companion beacon.
    int16_t setRuntimeSpreadFactor(uint8_t sf) override { return lora.setSpreadingFactor(sf); }
    int16_t setRuntimeBandwidth(float bwKHz) override { return lora.setBandwidth(bwKHz); }

  protected:
    /**
     * Specific module instance
     */
    T lora;

    int16_t getCurrentRSSI() override;

    /**
     * Glue functions called from ISR land
     */
    virtual void disableInterrupt() override;

    /**
     * Enable a particular ISR callback glue function
     */
    virtual void enableInterrupt(void (*callback)()) { lora.setIrqAction(callback); }

    /** can we detect a LoRa preamble on the current channel? */
    virtual bool isChannelActive() override;

    /** are we actively receiving a packet (only called during receiving state) */
    virtual bool isActivelyReceiving() override;

    /**
     * Start waiting to receive a message
     */
    virtual void startReceive() override;

    /**
     *  We override to turn on transmitter power as needed.
     */
    virtual void configHardwareForSend() override;

    /**
     * Add SNR data to received messages
     */
    virtual void addReceiveMetadata(meshtastic_MeshPacket *mp) override;

    virtual void setStandby() override;

    uint32_t getPacketTime(uint32_t pl, bool received) override { return computePacketTime(lora, pl, received); }
};
#endif