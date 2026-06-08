#pragma once
#include "Default.h"
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"

/**
 * Position module for sending/receiving positions into the mesh
 */
class PositionModule : public ProtobufModule<meshtastic_Position>, private concurrency::OSThread
{
    CallbackObserver<PositionModule, const meshtastic::Status *> nodeStatusObserver =
        CallbackObserver<PositionModule, const meshtastic::Status *>(this, &PositionModule::handleStatusUpdate);

    /// The id of the last packet we sent, to allow us to cancel it if we make something fresher
    PacketId prevPacketId = 0;

    /// We limit our GPS broadcasts to a max rate
    uint32_t lastGpsSend = 0;

    // Store the latest good lat / long
    int32_t lastGpsLatitude = 0;
    int32_t lastGpsLongitude = 0;

    /// We force a rebroadcast if the radio settings change
    uint32_t currentGeneration = 0;

  public:
    /** Constructor
     * name is for debugging output
     */
    PositionModule();

    /**
     * Send our position into the mesh
     */
    void sendOurPosition(NodeNum dest, bool wantReplies = false, uint8_t channel = 0);
    void sendOurPosition();

    void handleNewPosition();

  protected:
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Position *p) override;

    virtual void alterReceivedProtobuf(meshtastic_MeshPacket &mp, meshtastic_Position *p) override;

    /** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
     * so that subclasses can (optionally) send a response back to the original sender.  */
    virtual meshtastic_MeshPacket *allocReply() override;

    /** Does our periodic broadcast */
    virtual int32_t runOnce() override;

  private:
    meshtastic_MeshPacket *allocPositionPacket();
    struct SmartPosition getDistanceTraveledSinceLastSend(meshtastic_PositionLite currentPosition);
    meshtastic_MeshPacket *allocAtakPli();
    void trySetRtc(meshtastic_Position p, bool isLocal, bool forceUpdate = false);
    uint32_t precision;
    void sendLostAndFoundText();
    bool hasQualityTimesource();
    bool hasGPS();
    uint32_t lastSentReply = 0; // Last time we sent a position reply (used for reply throttling only)

    /** DL9SAU Stage 4: timestamp (millis) of the last companion beacon.
     *  Used to gate the once-per-hour cadence. 0 = never sent since
     *  boot. */
    uint32_t lastLongFastBeaconMs = 0;

#if DL9SAU_STAGE6_ALTERNATING_COMPANION
    /** DL9SAU Stage 6: counter that alternates the companion preset
     *  between LongFast and the region-default when we are in a non-LF
     *  region AND our current preset differs from the region default
     *  (e.g. Berlin = MediumFast, we are on MediumSlow). Reset to 0 on
     *  reboot — fair enough, the first companion after boot will be on
     *  LongFast. */
    uint8_t companionAlternateCount = 0;

    /** DL9SAU Stage 6: pick the next companion preset based on the
     *  4-case decision matrix. Returns LongFast in the classic cases
     *  (Fall 2 + 3); alternates between LongFast and region-default
     *  only in Fall 4. */
    meshtastic_Config_LoRaConfig_ModemPreset pickCompanionPreset();
#endif

    /** DL9SAU Stage 4: send a companion position beacon if the current
     *  preset is not LongFast and at least one hour has passed since
     *  the last companion. Called at the end of sendOurPosition().
     *  positionHopLimit carries the hop_limit the regular position was
     *  sent with (already role-capped for CLIENT, default for others),
     *  so the companion inherits the same reach the user asked for.
     *  Channel handling: defaults to the same channel as the normal
     *  position, EXCEPT when that channel's name matches a known modem
     *  preset name AND its PSK is the default (AQ==) — in which case
     *  the companion is sent on the virtual public default channel of
     *  the chosen preset so any random finder can decode it.
     *  Stage 6 may alternate the chosen preset; see pickCompanionPreset. */
    void maybeSendLongFastCompanion(NodeNum dest, uint8_t positionChannel, uint8_t positionHopLimit);

#if USERPREFS_EVENT_MODE
    // In event mode we want to prevent excessive position broadcasts
    // we set the minimum interval to 5m
    const uint32_t minimumTimeThreshold =
        max(uint32_t(300000), Default::getConfiguredOrDefaultMs(config.position.broadcast_smart_minimum_interval_secs,
                                                                default_broadcast_smart_minimum_interval_secs));
#else
    const uint32_t minimumTimeThreshold = Default::getConfiguredOrDefaultMs(config.position.broadcast_smart_minimum_interval_secs,
                                                                            default_broadcast_smart_minimum_interval_secs);
#endif
};

struct SmartPosition {
    float distanceTraveled;
    uint32_t distanceThreshold;
    bool hasTraveledOverThreshold;
};

extern PositionModule *positionModule;