// FSCommon.h must be included before TextBucketStore.h so the FSCom macro is
// already defined when our header decides whether to declare the flash helpers.
#include "FSCommon.h"

#include "TextBucketStore.h"

#if DL9SAU_TOPHONE_TEXT_MESSAGE_BUCKETS_FOR_STORE_RAM_AND_FLASH

#include "MemoryPool.h"
#include "MeshTypes.h"
#include "Router.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "concurrency/LockGuard.h"
#include "mesh/Channels.h"
#include "mesh/mesh-pb-constants.h"
#include <Arduino.h>
#include <string.h>

namespace dl9sau
{

// Persistence file format:
//   [magic:4 "TB01"][entry]*
// Each entry: [seq_no:4 LE][len:1][packet_bytes:len]
static const char TB_MAGIC[4] = {'T', 'B', '0', '1'};

TextBucketStore &TextBucketStore::instance()
{
    static TextBucketStore inst;
    return inst;
}

TextBucketStore::TextBucketStore() {}

uint8_t TextBucketStore::capacityFor(TextBucketId b) const
{
    switch (b) {
    case BUCKET_PRIMARY:
        return DL9SAU_BUCKET_PRIMARY_SLOTS;
    case BUCKET_HASHTAG:
        return DL9SAU_BUCKET_HASHTAG_SLOTS;
    case BUCKET_PRIVATE:
        return DL9SAU_BUCKET_PRIVATE_SLOTS;
    case BUCKET_DM:
        return DL9SAU_BUCKET_DM_SLOTS;
    default:
        return 0;
    }
}

bool TextBucketStore::flashFor(TextBucketId b) const
{
    switch (b) {
    case BUCKET_PRIVATE:
        return DL9SAU_BUCKET_PRIVATE_FLASH != 0;
    case BUCKET_DM:
        return DL9SAU_BUCKET_DM_FLASH != 0;
    default:
        return false;
    }
}

TextBucketSlot *TextBucketStore::slotsFor(TextBucketId b)
{
    switch (b) {
    case BUCKET_PRIMARY:
        return slots_primary;
    case BUCKET_HASHTAG:
        return slots_hashtag;
    case BUCKET_PRIVATE:
        return slots_private;
    case BUCKET_DM:
        return slots_dm;
    default:
        return nullptr;
    }
}

const TextBucketSlot *TextBucketStore::slotsFor(TextBucketId b) const
{
    switch (b) {
    case BUCKET_PRIMARY:
        return slots_primary;
    case BUCKET_HASHTAG:
        return slots_hashtag;
    case BUCKET_PRIVATE:
        return slots_private;
    case BUCKET_DM:
        return slots_dm;
    default:
        return nullptr;
    }
}

TextBucketId TextBucketStore::classify(const meshtastic_MeshPacket &p) const
{
    // 1) DM
    if (!isBroadcast(p.to))
        return BUCKET_DM;

    // 2) PRIMARY (channel 0)
    if (p.channel == 0)
        return BUCKET_PRIMARY;

    // 3) HASHTAG = same PSK as channel 0 (covers AQ== chains and shared-key
    //    closed-network chains alike)
    if (p.channel >= channels.getNumChannels())
        return BUCKET_PRIVATE; // out-of-range: safest non-evicting bucket

    const auto &p0 = channels.getByIndex(0).settings.psk;
    const auto &pn = channels.getByIndex(p.channel).settings.psk;
    if (p0.size == pn.size && p0.size != 0 && memcmp(p0.bytes, pn.bytes, p0.size) == 0)
        return BUCKET_HASHTAG;

    return BUCKET_PRIVATE;
}

void TextBucketStore::insertCopyIntoBucket(TextBucketId b, const meshtastic_MeshPacket &src)
{
    TextBucketSlot *arr = slotsFor(b);
    const uint8_t cap = capacityFor(b);
    if (!arr || cap == 0)
        return;

    // Find empty slot first
    int target = -1;
    for (uint8_t i = 0; i < cap; ++i) {
        if (arr[i].seq_no == 0) {
            target = i;
            break;
        }
    }
    // Otherwise overwrite oldest in this bucket
    if (target < 0) {
        uint32_t minSeq = UINT32_MAX;
        for (uint8_t i = 0; i < cap; ++i) {
            if (arr[i].seq_no < minSeq) {
                minSeq = arr[i].seq_no;
                target = i;
            }
        }
        if (target >= 0)
            LOG_INFO("TextBucket %u full, evicting oldest (seq=%u)", (unsigned)b, (unsigned)minSeq);
    }
    if (target < 0)
        return;

    arr[target].seq_no = nextSeq++;
    if (nextSeq == 0)
        nextSeq = 1; // wrap-around safety; 0 is reserved
    arr[target].packet = src;

    dirty[b] = true;
    uint32_t now = millis();
    if (firstDirtyMs[b] == 0)
        firstDirtyMs[b] = now;
}

bool TextBucketStore::enqueue(meshtastic_MeshPacket *p)
{
    if (!p)
        return false;
    TextBucketId b;
    {
        concurrency::LockGuard g(&lock);
        b = classify(*p);
        insertCopyIntoBucket(b, *p);
    }
    packetPool.release(p);
    return true;
}

uint32_t TextBucketStore::minSeqAcrossAll()
{
    concurrency::LockGuard g(&lock);
    uint32_t bestSeq = UINT32_MAX;
    for (uint8_t b = 0; b < BUCKET_COUNT; ++b) {
        const TextBucketSlot *arr = slotsFor((TextBucketId)b);
        const uint8_t cap = capacityFor((TextBucketId)b);
        for (uint8_t i = 0; i < cap; ++i) {
            if (arr[i].seq_no != 0 && arr[i].seq_no < bestSeq)
                bestSeq = arr[i].seq_no;
        }
    }
    return bestSeq;
}

bool TextBucketStore::popMinSeqInto(meshtastic_MeshPacket *out)
{
    if (!out)
        return false;
    concurrency::LockGuard g(&lock);
    uint32_t bestSeq = UINT32_MAX;
    TextBucketId bestBucket = BUCKET_PRIMARY;
    int bestIdx = -1;
    for (uint8_t b = 0; b < BUCKET_COUNT; ++b) {
        TextBucketSlot *arr = slotsFor((TextBucketId)b);
        const uint8_t cap = capacityFor((TextBucketId)b);
        for (uint8_t i = 0; i < cap; ++i) {
            if (arr[i].seq_no != 0 && arr[i].seq_no < bestSeq) {
                bestSeq = arr[i].seq_no;
                bestBucket = (TextBucketId)b;
                bestIdx = i;
            }
        }
    }
    if (bestIdx < 0)
        return false;
    TextBucketSlot *arr = slotsFor(bestBucket);
    *out = arr[bestIdx].packet;
    arr[bestIdx].seq_no = 0;
    memset(&arr[bestIdx].packet, 0, sizeof(arr[bestIdx].packet));
    dirty[bestBucket] = true;
    if (firstDirtyMs[bestBucket] == 0)
        firstDirtyMs[bestBucket] = millis();
    return true;
}

uint32_t TextBucketStore::allocSeqNo()
{
    concurrency::LockGuard g(&lock);
    uint32_t v = nextSeq++;
    if (nextSeq == 0)
        nextSeq = 1;
    return v;
}

#ifdef FSCom

const char *TextBucketStore::flashPathFor(TextBucketId b) const
{
    switch (b) {
    case BUCKET_PRIVATE:
        return "/msgs/text_private.dat";
    case BUCKET_DM:
        return "/msgs/text_dm.dat";
    default:
        return nullptr;
    }
}

bool TextBucketStore::saveBucketToFlash(TextBucketId b)
{
    const char *path = flashPathFor(b);
    if (!path)
        return false;

    {
        concurrency::LockGuard g(spiLock);
        FSCom.mkdir("/msgs");
    }

    SafeFile f(path, false);
    f.write(reinterpret_cast<const uint8_t *>(TB_MAGIC), sizeof(TB_MAGIC));

    // Walk the bucket and write each occupied slot.
    // We do not pre-sort by seq_no — restore will re-pack into the array
    // and the min-seq scan handles ordering.
    {
        concurrency::LockGuard g(&lock);
        const TextBucketSlot *arr = slotsFor(b);
        const uint8_t cap = capacityFor(b);
        uint8_t buf[meshtastic_MeshPacket_size];
        for (uint8_t i = 0; i < cap; ++i) {
            if (arr[i].seq_no == 0)
                continue;
            size_t numbytes = pb_encode_to_bytes(buf, sizeof(buf), &meshtastic_MeshPacket_msg, &arr[i].packet);
            if (numbytes == 0 || numbytes > 255)
                continue;
            uint32_t seq = arr[i].seq_no;
            uint8_t header[5];
            header[0] = (uint8_t)(seq & 0xff);
            header[1] = (uint8_t)((seq >> 8) & 0xff);
            header[2] = (uint8_t)((seq >> 16) & 0xff);
            header[3] = (uint8_t)((seq >> 24) & 0xff);
            header[4] = (uint8_t)numbytes;
            f.write(header, sizeof(header));
            f.write(buf, numbytes);
        }
    }

    bool ok = f.close();
    if (!ok)
        LOG_ERROR("TextBucket save failed: %s", path);
    return ok;
}

bool TextBucketStore::loadBucketFromFlash(TextBucketId b)
{
    const char *path = flashPathFor(b);
    if (!path)
        return false;

    concurrency::LockGuard guard(spiLock);
    if (!FSCom.exists(path))
        return true; // nothing to load is fine

    auto f = FSCom.open(path, FILE_O_READ);
    if (!f)
        return false;

    char magic[4] = {0, 0, 0, 0};
    if (f.readBytes(magic, sizeof(magic)) != sizeof(magic) || memcmp(magic, TB_MAGIC, 4) != 0) {
        LOG_WARN("TextBucket %s magic mismatch, ignoring", path);
        f.close();
        return false;
    }

    TextBucketSlot *arr = slotsFor(b);
    const uint8_t cap = capacityFor(b);
    uint8_t written = 0;
    uint8_t scratch[meshtastic_MeshPacket_size];

    while (f.available() > 0 && written < cap) {
        uint8_t header[5];
        if (f.readBytes(reinterpret_cast<char *>(header), 5) != 5)
            break;
        uint32_t seq =
            (uint32_t)header[0] | ((uint32_t)header[1] << 8) | ((uint32_t)header[2] << 16) | ((uint32_t)header[3] << 24);
        uint8_t len = header[4];
        if (len == 0 || len > sizeof(scratch))
            break;
        if (f.readBytes(reinterpret_cast<char *>(scratch), len) != len)
            break;

        meshtastic_MeshPacket pkt = meshtastic_MeshPacket_init_default;
        if (!pb_decode_from_bytes(scratch, len, &meshtastic_MeshPacket_msg, &pkt))
            continue;

        arr[written].seq_no = seq;
        arr[written].packet = pkt;
        ++written;
        if (seq >= nextSeq)
            nextSeq = seq + 1;
    }
    f.close();

    if (written > 0)
        LOG_INFO("TextBucket restored %u entries from %s", (unsigned)written, path);
    return true;
}

#endif // FSCom

void TextBucketStore::init()
{
    if (initialized)
        return;
    initialized = true;
#ifdef FSCom
    if (flashFor(BUCKET_PRIVATE))
        loadBucketFromFlash(BUCKET_PRIVATE);
    if (flashFor(BUCKET_DM))
        loadBucketFromFlash(BUCKET_DM);
#endif
    LOG_INFO("TextBucketStore init done, nextSeq=%u", (unsigned)nextSeq);
}

void TextBucketStore::loopTick()
{
#ifdef FSCom
    uint32_t now = millis();
    for (uint8_t b = 0; b < BUCKET_COUNT; ++b) {
        if (!dirty[b] || !flashFor((TextBucketId)b))
            continue;
        // Debounce: at least DL9SAU_BUCKET_FLASH_DEBOUNCE_MS since first dirty
        if (firstDirtyMs[b] != 0 && (now - firstDirtyMs[b]) < DL9SAU_BUCKET_FLASH_DEBOUNCE_MS)
            continue;
        if (saveBucketToFlash((TextBucketId)b)) {
            dirty[b] = false;
            firstDirtyMs[b] = 0;
            lastFlushMs[b] = now;
        }
    }
#endif
}

void TextBucketStore::flushNow()
{
#ifdef FSCom
    for (uint8_t b = 0; b < BUCKET_COUNT; ++b) {
        if (!dirty[b] || !flashFor((TextBucketId)b))
            continue;
        if (saveBucketToFlash((TextBucketId)b)) {
            dirty[b] = false;
            firstDirtyMs[b] = 0;
            lastFlushMs[b] = millis();
        }
    }
#endif
}

} // namespace dl9sau

#endif // DL9SAU_TOPHONE_TEXT_MESSAGE_BUCKETS_FOR_STORE_RAM_AND_FLASH
