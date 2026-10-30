// Fuzzes the memory-mapped MSJPDT1 Japanese dictionary loader and its lookup/decoder paths.
#include <cstdint>
#include <string>

#include "japanese/japanese_sentence_decoder.h"

#include "harness_util.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 56)
        return -1;
    const ScratchFile file(data, size);
    if (!file.ok())
        return -1;
    const japanese::JapaneseSentenceDecoder decoder(file.path());
    if (!decoder.ready())
        return -1;
    const std::string tail = tail_bytes(data, size, 16);
    for (const std::string &reading : {std::string("かな"), std::string("きょう"), std::string("わたしのにほんご"),
                                       std::string("こんにちは"), std::string("k"), tail})
    {
        (void)decoder.Decode(reading, 8);
        (void)decoder.ExactLemmas(reading, 8);
        (void)decoder.PrefixLemmas(reading, 8);
        (void)decoder.LongerPrefixLemmas(reading, 8);
    }
    (void)decoder.PrefixLemmasContinuing("き", {"ょ", "う", "k"}, 8);
    (void)decoder.ConnectionCost(0, 0);
    (void)decoder.ConnectionCost(1, 2);
    (void)decoder.ConnectionCost(65535, 65535);
    return 0;
}
