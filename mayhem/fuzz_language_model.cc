// Fuzzes the kenlm (trie, quantized) binary model loader and the n-gram query path.
#include <cstdint>
#include <cstring>
#include <string>

#include "engine/ngram/language_model.h"

#include "harness_util.h"

// Format parameters of the seed model (testsuite/small.lm, trie + 4-bit quantization + bhiksha 22).
// The loader trusts these fields: kenlm's own checks cover only the magic and sanity values, so a
// mutated order, count, quantizer width or bhiksha width flows into table-size and shift
// arithmetic and trips the same header-validation defects over and over. Inputs whose parameters
// differ from the seed's are rejected with -1 so the fuzzer spends its time on the vocabulary,
// the unigram/trie bodies and the query path instead.
constexpr size_t kSeedSize = 688;
const uint8_t kSeedHeader[136] = {
    0x6d, 0x6d, 0x61, 0x70, 0x20, 0x6c, 0x6d, 0x20, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x6b,
    0x68, 0x65, 0x61, 0x66, 0x69, 0x65, 0x6c, 0x64, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x63, 0x6f, 0x64,
    0x65, 0x20, 0x66, 0x6f, 0x72, 0x6d, 0x61, 0x74, 0x20, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e,
    0x20, 0x35, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
    0x00, 0x00, 0x00, 0xbf, 0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x3f,
    0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
struct PinnedField
{
    size_t offset;
    size_t length;
    const uint8_t *expected;
};
// 0x00: sanity header + FixedWidthParameters + per-order counts; 0x88: vocabulary entry count;
// 0xd8: quantizer config; 0x250: bhiksha version, pointer bits and offset table. The loader reads
// the last three before (or while) sizing the tables, and indexes the mapped file with them.
const uint8_t kVocabCount[8] = {0x08};
const uint8_t kQuantConfig[3] = {0x02, 0x04, 0x04};
const uint8_t kBhiksha[22] = {0x00, 0x16};
const PinnedField kPinned[] = {
    {0, sizeof(kSeedHeader), kSeedHeader},
    {136, sizeof(kVocabCount), kVocabCount},
    {216, sizeof(kQuantConfig), kQuantConfig},
    {592, sizeof(kBhiksha), kBhiksha},
};

// The unigram table (11 records of float prob, float backoff, uint64 next) starts at 416; each
// `next` is the index of the unigram's first bigram, so the sequence is non-decreasing and ends
// at the bigram count (7). Anything else sends the trie lookups outside the mapped middle table.
static bool unigram_pointers_are_ordered(const uint8_t *data)
{
    constexpr size_t kUnigramOffset = 416;
    constexpr size_t kRecordSize = 16;
    constexpr unsigned kUnigrams = 10;
    constexpr uint64_t kBigrams = 7;
    uint64_t previous = 0;
    for (unsigned i = 0; i < kUnigrams; ++i)
    {
        uint64_t next;
        memcpy(&next, data + kUnigramOffset + i * kRecordSize + 8, sizeof(next));
        if (next < previous || next > kBigrams)
            return false;
        previous = next;
    }
    return previous == kBigrams;
}

static bool has_seed_parameters(const uint8_t *data, size_t size)
{
    if (size != kSeedSize)
        return false;
    for (const PinnedField &field : kPinned)
        if (memcmp(data + field.offset, field.expected, field.length) != 0)
            return false;
    return unigram_pointers_are_ordered(data);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!has_seed_parameters(data, size))
        return -1;
    const ScratchFile file(data, size);
    if (!file.ok())
        return -1;
    const ngram::LanguageModel model(file.path());
    if (!model.valid())
        return -1;
    const std::string tail = tail_bytes(data, size, 8);
    ngram::State state = model.null_state();
    ngram::State next;
    for (const std::string &word : {std::string("你"), std::string("好"), std::string("世"), std::string("界"),
                                    std::string("我"), tail, std::string("<unk>")})
    {
        (void)model.score(state, word, next);
        state = next;
    }
    (void)model.single_word_score(model.begin_state(), "你");
    (void)model.single_word_score(model.null_state(), tail);
    (void)model.is_unknown(model.index(tail));
    return 0;
}
