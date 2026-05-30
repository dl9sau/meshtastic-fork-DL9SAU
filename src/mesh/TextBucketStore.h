#pragma once

#include "configuration.h"

#if DL9SAU_TOPHONE_TEXT_MESSAGE_BUCKETS_FOR_STORE_RAM_AND_FLASH

#include "concurrency/Lock.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <stdint.h>

namespace dl9sau
{

enum TextBucketId : uint8_t {
    BUCKET_PRIMARY = 0,
    BUCKET_HASHTAG = 1,
    BUCKET_PRIVATE = 2,
    BUCKET_DM = 3,
    BUCKET_COUNT = 4
};

// seq_no == 0 marks an empty slot.
struct TextBucketSlot {
    uint32_t seq_no;
    meshtastic_MeshPacket packet;
};

class TextBucketStore
{
  public:
    TextBucketStore();

    // Wire flash restore. Must be called after fsInit() and after the channels
    // table has been populated. Safe to call multiple times.
    void init();

    // Classify and enqueue. Packet is COPIED into the bucket's slot, then
    // released back to packetPool. Returns true if accepted (always, in
    // practice — eviction happens silently when the target bucket is full).
    bool enqueue(meshtastic_MeshPacket *p);

    // Smallest non-zero seq_no across all buckets, or UINT32_MAX if all empty.
    // Used by the chronological merge-sort in MeshService::getForPhone().
    uint32_t minSeqAcrossAll();

    // Atomically (under the bucket lock) find the slot with the smallest
    // non-zero seq_no, COPY its packet into *out, clear the slot, mark the
    // bucket dirty for flash debounce. Returns false if all buckets empty.
    bool popMinSeqInto(meshtastic_MeshPacket *out);

    // Called from MeshService::loop(). Flushes dirty PRIVATE/DM buckets to
    // flash if the debounce window has elapsed.
    void loopTick();

    // Force-flush all dirty buckets immediately (e.g. on shutdown).
    void flushNow();

    // Singleton accessor.
    static TextBucketStore &instance();

    // Allocate the next monotonic seq_no. Used both internally (on enqueue)
    // and by MeshService for tagging legacy toPhoneQueue entries so the
    // chronological merge-sort during drain works across both sources.
    uint32_t allocSeqNo();

  private:
    TextBucketId classify(const meshtastic_MeshPacket &p) const;

    // Per-bucket helpers
    TextBucketSlot *slotsFor(TextBucketId b);
    const TextBucketSlot *slotsFor(TextBucketId b) const;
    uint8_t capacityFor(TextBucketId b) const;
    bool flashFor(TextBucketId b) const;

    // FIFO insert: find empty slot, else overwrite slot with lowest seq_no
    // inside the same bucket. Assigns next seq_no.
    void insertCopyIntoBucket(TextBucketId b, const meshtastic_MeshPacket &src);

#ifdef FSCom
    bool saveBucketToFlash(TextBucketId b);
    bool loadBucketFromFlash(TextBucketId b);
    const char *flashPathFor(TextBucketId b) const;
#endif

    // Static slot arrays (BSS, zero-init at boot).
    TextBucketSlot slots_primary[DL9SAU_BUCKET_PRIMARY_SLOTS];
    TextBucketSlot slots_hashtag[DL9SAU_BUCKET_HASHTAG_SLOTS];
    TextBucketSlot slots_private[DL9SAU_BUCKET_PRIVATE_SLOTS];
    TextBucketSlot slots_dm[DL9SAU_BUCKET_DM_SLOTS];

    uint32_t nextSeq = 1;     // monotonic seq_no source (0 reserved as empty)
    bool initialized = false; // guard against double-init

    bool dirty[BUCKET_COUNT] = {false, false, false, false};
    uint32_t lastFlushMs[BUCKET_COUNT] = {0, 0, 0, 0};
    uint32_t firstDirtyMs[BUCKET_COUNT] = {0, 0, 0, 0};

    concurrency::Lock lock;
};

} // namespace dl9sau

#endif // DL9SAU_TOPHONE_TEXT_MESSAGE_BUCKETS_FOR_STORE_RAM_AND_FLASH
