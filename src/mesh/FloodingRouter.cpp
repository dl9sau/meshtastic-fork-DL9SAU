#include "FloodingRouter.h"
#include "MeshRadio.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "meshUtils.h"
#include "modules/TextMessageModule.h"
#if !MESHTASTIC_EXCLUDE_TRACEROUTE
#include "modules/TraceRouteModule.h"
#endif

FloodingRouter::FloodingRouter() {}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error
 */
ErrorCode FloodingRouter::send(meshtastic_MeshPacket *p)
{
    // Add any messages _we_ send to the seen message list (so we will ignore all retransmissions we see)
    p->relay_node = nodeDB->getLastByteOfNodeNum(getNodeNum()); // First set the relayer to us
    wasSeenRecently(p);                                         // FIXME, move this to a sniffSent method

    return Router::send(p);
}

bool FloodingRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    bool wasUpgraded = false;
    bool seenRecently =
        wasSeenRecently(p, true, nullptr, nullptr, &wasUpgraded); // Updates history; returns false when an upgrade is detected

    // Handle hop_limit upgrade scenario for rebroadcasters
    if (wasUpgraded && perhapsHandleUpgradedPacket(p)) {
        return true; // we handled it, so stop processing
    }

    if (!seenRecently && !wasUpgraded && textMessageModule) {
        seenRecently = textMessageModule->recentlySeen(p->id);
    }

    if (seenRecently) {
        printPacket("Ignore dupe incoming msg", p);
        rxDupe++;

        /* If the original transmitter is doing retransmissions (hopStart equals hopLimit) for a reliable transmission, e.g., when
        the ACK got lost, we will handle the packet again to make sure it gets an implicit ACK. */
        bool isRepeated = p->hop_start > 0 && p->hop_start == p->hop_limit;
        if (isRepeated) {
            LOG_DEBUG("Repeated reliable tx");
            // Check if it's still in the Tx queue, if not, we have to relay it again
            if (!findInTxQueue(p->from, p->id)) {
                reprocessPacket(p);
                perhapsRebroadcast(p);
            }
        } else {
            perhapsCancelDupe(p);
        }

        return true;
    }

    return Router::shouldFilterReceived(p);
}

bool FloodingRouter::perhapsHandleUpgradedPacket(const meshtastic_MeshPacket *p)
{
    // isRebroadcaster() is duplicated in perhapsRebroadcast(), but this avoids confusing log messages
    if (isRebroadcaster() && iface && p->hop_limit > 0) {
        // If we overhear a duplicate copy of the packet with more hops left than the one we are waiting to
        // rebroadcast, then remove the packet currently sitting in the TX queue and use this one instead.
        uint8_t dropThreshold = p->hop_limit; // remove queued packets that have fewer hops remaining
        if (iface->removePendingTXPacket(getFrom(p), p->id, dropThreshold)) {
            LOG_DEBUG("Processing upgraded packet 0x%08x for rebroadcast with hop limit %d (dropping queued < %d)", p->id,
                      p->hop_limit, dropThreshold);

            reprocessPacket(p);
            perhapsRebroadcast(p);

            rxDupe++;
            // We already enqueued the improved copy, so make sure the incoming packet stops here.
            return true;
        }
    }

    return false;
}

void FloodingRouter::reprocessPacket(const meshtastic_MeshPacket *p)
{
    if (nodeDB)
        nodeDB->updateFrom(*p);

#if !MESHTASTIC_EXCLUDE_TRACEROUTE
    if (traceRouteModule && p->which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        // If we got a packet that is not decoded, try to decode it so we can check for traceroute.
        auto decodedState = perhapsDecode(const_cast<meshtastic_MeshPacket *>(p));
        if (decodedState == DecodeState::DECODE_SUCCESS) {
            // parsing was successful, print for debugging
            printPacket("reprocessPacket(DUP)", p);
        } else {
            // Fatal decoding error, we can't do anything with this packet
            LOG_WARN(
                "FloodingRouter::reprocessPacket: Fatal decode error (state=%d, id=0x%08x, from=%u), can't check for traceroute",
                static_cast<int>(decodedState), p->id, getFrom(p));
            return;
        }
    }

    if (traceRouteModule && p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        p->decoded.portnum == meshtastic_PortNum_TRACEROUTE_APP) {
        traceRouteModule->processUpgradedPacket(*p);
    }
#endif
}

bool FloodingRouter::roleAllowsCancelingDupe(const meshtastic_MeshPacket *p)
{
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
        config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE) {
        // ROUTER, ROUTER_LATE should never cancel relaying a packet (i.e. we should always rebroadcast),
        // even if we've heard another station rebroadcast it already.
        return false;
    }

    if (config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_BASE) {
#if DL9SAU_STAGE5_BROADCAST_RELAY_DELAY
        // DL9SAU Stage 5b: CLIENT_BASE never self-cancels broadcasts. The
        // Stage 5 delay was meant to land our hop_limit=0 copy on empty
        // neighbour queues, but perhapsCancelDupe was still trimming our
        // own delayed copy when we heard another relay during the wait —
        // exactly the inside-CLIENT_MUTE outage scenario we were trying
        // to prevent. Broadcasts go out unconditionally; DMs keep upstream
        // (favorite-protected) behaviour.
        if (isBroadcast(p->to))
            return false;
#endif
        // CLIENT_BASE: if the packet is from or to a favorited node,
        // we should act like a ROUTER and should never cancel a rebroadcast (i.e. we should always rebroadcast),
        // even if we've heard another station rebroadcast it already.
        return !nodeDB->isFromOrToFavoritedNode(*p);
    }

    // All other roles (such as CLIENT) should cancel a rebroadcast if they hear another station's rebroadcast.
    return true;
}

void FloodingRouter::perhapsCancelDupe(const meshtastic_MeshPacket *p)
{
    if (p->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA && roleAllowsCancelingDupe(p)) {
        // cancel rebroadcast of this message *if* there was already one, unless we're a router!
        // But only LoRa packets should be able to trigger this.
        if (Router::cancelSending(p->from, p->id))
            txRelayCanceled++;
    }
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE && iface) {
        iface->clampToLateRebroadcastWindow(getFrom(p), p->id);
    }
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_BASE && iface && nodeDB &&
        nodeDB->isFromOrToFavoritedNode(*p)) {
        iface->clampToLateRebroadcastWindow(getFrom(p), p->id);
    }
}

bool FloodingRouter::isRebroadcaster()
{
    return config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE &&
           config.device.rebroadcast_mode != meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
}

bool FloodingRouter::applyClientRepeatPolicy(meshtastic_MeshPacket *tosend)
{
    // Stage 2 policy only applies to the two "user" client roles. Other
    // roles (REPEATER, ROUTER, ROUTER_LATE, ...) keep upstream behaviour.
    const meshtastic_Config_DeviceConfig_Role role = config.device.role;
    if (role != meshtastic_Config_DeviceConfig_Role_CLIENT &&
        role != meshtastic_Config_DeviceConfig_Role_CLIENT_BASE) {
        return true;
    }

    // B1 drop: refuse to repeat anything we couldn't decode.
    if (tosend->which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        LOG_DEBUG("DL9SAU B1: drop rebroadcast of undecoded packet 0x%08x", tosend->id);
        return false;
    }

    const meshtastic_PortNum portnum = tosend->decoded.portnum;

    // B1 drop: refuse telemetry explicitly even though it's "core".
    if (portnum == meshtastic_PortNum_TELEMETRY_APP) {
        LOG_DEBUG("DL9SAU B1: drop rebroadcast of telemetry 0x%08x", tosend->id);
        return false;
    }

    // B1 drop: refuse anything outside the core repeat-whitelist (matches
    // upstream's CORE_PORTNUMS_ONLY core list minus telemetry).
    const bool inWhitelist = IS_ONE_OF(portnum, meshtastic_PortNum_TEXT_MESSAGE_APP,
                                       meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP, meshtastic_PortNum_POSITION_APP,
                                       meshtastic_PortNum_NODEINFO_APP, meshtastic_PortNum_ROUTING_APP,
                                       meshtastic_PortNum_ADMIN_APP, meshtastic_PortNum_ALERT_APP,
                                       meshtastic_PortNum_KEY_VERIFICATION_APP, meshtastic_PortNum_WAYPOINT_APP,
                                       meshtastic_PortNum_STORE_FORWARD_APP, meshtastic_PortNum_TRACEROUTE_APP,
                                       meshtastic_PortNum_STORE_FORWARD_PLUSPLUS_APP);
    if (!inWhitelist) {
        LOG_DEBUG("DL9SAU B1: drop rebroadcast of non-core portnum %d (id 0x%08x)", (int)portnum, tosend->id);
        return false;
    }

    // B3 exceptions — repeat at full configured power and configured CR.
    // TRACEROUTE_APP and ROUTING_APP must remain debug-grade reliable.
    if (portnum == meshtastic_PortNum_TRACEROUTE_APP || portnum == meshtastic_PortNum_ROUTING_APP) {
        return true;
    }
    // Direct-DM where we are the requested next_hop AND the destination is
    // one of our known direct neighbours within the last 12 h.
    const uint8_t ourRelayId = nodeDB ? nodeDB->getLastByteOfNodeNum(getNodeNum()) : 0;
    if (!isBroadcast(tosend->to) && tosend->next_hop != NO_NEXT_HOP_PREFERENCE && tosend->next_hop == ourRelayId && nodeDB) {
        const meshtastic_NodeInfoLite *dst = nodeDB->getMeshNode(tosend->to);
        if (dst && dst->has_hops_away && dst->hops_away == 0) {
            const uint32_t now = getValidTime(RTCQualityFromNet);
            constexpr uint32_t TWELVE_HOURS = 12UL * 60UL * 60UL;
            if (now != 0 && dst->last_heard != 0 && (now - dst->last_heard) <= TWELVE_HOURS) {
                return true; // B3 direct-DM exception
            }
        }
    }
    // B3 extension: packet still carries its original hop_limit
    // (hop_start - hop_limit == 0), which means we are the first relay
    // in the chain — the originator was a direct neighbour. Give it full
    // power so the next hop on the path actually hears us. getHopsAway
    // returns -1 when hop_start isn't reliable; in that case we fall
    // through to the B2 default.
    if (getHopsAway(*tosend) == 0) {
        return true;
    }

    // B2 default: CR=5, TX-power = configured - 6 dB (floor 10 dBm).
    tosend->has_tx_cr_override = true;
    tosend->tx_cr_override = 5;
    tosend->has_tx_power_override = true;
    tosend->tx_power_override = reducedTxPowerForStage2();
    // CLIENT_BASE extra: kill the rebroadcast chain at one hop past us.
    // Rationale: a base station typically sits among other infrastructure
    // — once it has done its courtesy relay, propagating further is
    // redundant and burns shared airtime. Mobile CLIENT keeps the
    // standard hop budget because it may sit at a bridge position.
    if (role == meshtastic_Config_DeviceConfig_Role_CLIENT_BASE) {
        tosend->hop_limit = 0;
#if DL9SAU_STAGE5_BROADCAST_RELAY_DELAY
        // Stage 5: delay our hop_limit=0 broadcast relay so it lands on the
        // neighbours' TX queues *after* they have already relayed (or
        // dropped) the original. perhapsCancelDupe on an empty queue is a
        // no-op, so we no longer suppress legitimate downstream propagation.
        // DMs keep immediate forwarding — interactivity wins over the
        // narrower cancel-dupe corner case.
        // No self-cancel: even if we hear another relay during the window
        // we still transmit, because CLIENT_BASE may be the only relay
        // path for an inside CLIENT_MUTE that doesn't hear the other one.
        if (isBroadcast(tosend->to)) {
            const uint32_t delayMs = stage5DelayForPreset();
            tosend->tx_after = millis() + delayMs;
            LOG_DEBUG("DL9SAU Stage 5: delay broadcast relay 0x%08x by %ums", tosend->id, (unsigned)delayMs);
        }
#endif
    }
    return true;
}

#if DL9SAU_STAGE5_BROADCAST_RELAY_DELAY
uint32_t FloodingRouter::stage5DelayForPreset() const
{
    switch (config.lora.modem_preset) {
    case meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW:
        return DL9SAU_STAGE5_DELAY_MS_VLONGSLOW;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        return DL9SAU_STAGE5_DELAY_MS_LONGSLOW;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        return DL9SAU_STAGE5_DELAY_MS_LONGMOD;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
        return DL9SAU_STAGE5_DELAY_MS_LONGFAST;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        return DL9SAU_STAGE5_DELAY_MS_MEDIUMSLOW;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        return DL9SAU_STAGE5_DELAY_MS_MEDIUMFAST;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        return DL9SAU_STAGE5_DELAY_MS_SHORTSLOW;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        return DL9SAU_STAGE5_DELAY_MS_SHORTFAST;
    default:
        return DL9SAU_STAGE5_DELAY_MS_DEFAULT;
    }
}
#endif

void FloodingRouter::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    bool isAckorReply = (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) &&
                        (p->decoded.request_id != 0 || p->decoded.reply_id != 0);
    if (isAckorReply && !isToUs(p) && !isBroadcast(p->to)) {
        // do not flood direct message that is ACKed or replied to
        LOG_DEBUG("Rxd an ACK/reply not for me, cancel rebroadcast");
        Router::cancelSending(p->to, p->decoded.request_id); // cancel rebroadcast for this DM
    }

    perhapsRebroadcast(p);

    // handle the packet as normal
    Router::sniffReceived(p, c);
}
