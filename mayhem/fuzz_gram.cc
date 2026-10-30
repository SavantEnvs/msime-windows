// Fuzzes the mmap'd Rime::Grammar (darts double-array) loader and the collocation scorer.
#include <cstdint>
#include <cstring>
#include <string>

#include "ngram/octagram/octagram_gram.h"

#include "harness_util.h"

// GramDb::open checks the Metadata magic and that the unit array fits in the mapping, then hands
// the units to darts-clone, which indexes them with `position ^ offset ^ label` and no bounds
// check. A real double array (darts-clone pads it to whole 256-unit blocks) always keeps every
// child index of a non-leaf unit inside the array, so inputs that break that are structurally
// invalid and are rejected with -1 rather than re-finding the same out-of-bounds read at every
// call site in darts.h.
static bool array_is_well_formed(const uint8_t *data, size_t size)
{
    constexpr size_t kArraySizeOffset = 36;
    constexpr size_t kOffsetPtrOffset = 40;
    constexpr size_t kHeaderBytes = 44;
    if (size < kHeaderBytes)
        return false;
    uint32_t units = 0;
    int32_t rel = 0;
    memcpy(&units, data + kArraySizeOffset, sizeof(units));
    memcpy(&rel, data + kOffsetPtrOffset, sizeof(rel));
    if (rel < 0 || static_cast<size_t>(rel) > size - kOffsetPtrOffset)
        return false;
    const size_t start = kOffsetPtrOffset + static_cast<size_t>(rel);
    if (units == 0 || units % 256 != 0 || units > (size - start) / 4)
        return false;
    // Every query starts at unit 0 and follows its offset, so the root cannot be a leaf either.
    uint32_t root;
    memcpy(&root, data + start, sizeof(root));
    if (root >> 31)
        return false;
    for (uint32_t pos = 0; pos < units; ++pos)
    {
        uint32_t unit;
        memcpy(&unit, data + start + static_cast<size_t>(pos) * 4, sizeof(unit));
        if (unit >> 31)
            continue; // leaf: the low bits are a value, not an offset
        const uint32_t offset = (unit >> 10) << ((unit & (1U << 9)) >> 6);
        if (((pos ^ offset) | 0xFFU) >= units)
            return false;
    }
    return true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 48 || !array_is_well_formed(data, size))
        return -1;
    const ScratchFile file(data, size);
    if (!file.ok())
        return -1;
    gram::GramDb db;
    if (!db.open(file.path()))
        return -1;
    const gram::GrammarConfig config;
    const std::string tail = tail_bytes(data, size, 16);
    const std::string half = tail.substr(0, tail.size() / 2);
    const std::string rest = tail.substr(tail.size() / 2);
    (void)db.query("你", "好", true, config);
    (void)db.query("你好", "世界", true, config);
    (void)db.query("我你好", "世界吗", false, config);
    (void)db.query(half, rest, true, config);
    (void)db.query(tail, tail, false, config);
    gram::QueryStats stats;
    (void)db.query("世", "界", true, config, &stats);
    return 0;
}
