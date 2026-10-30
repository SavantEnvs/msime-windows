// Deterministic, tiny stand-ins for the engine's model files. Shared by the seed generator
// (tools/gen_seeds.cc), the golden oracle (oracle_test.cc) and the session fuzzer.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "darts.h"

namespace fixtures
{

inline void put_le(std::string &out, std::uint64_t value, int width)
{
    for (int i = 0; i < width; ++i)
        out.push_back(static_cast<char>((value >> (i * 8)) & 255));
}

// Sentence model (safetensors): vocab 8, one layer, n_embd 4, context 8. quantized=true stores the
// big matrices as I8 with per-row scales so the dequantizing loader path is covered too.
inline std::string safetensors_model(bool quantized)
{
    struct Tensor
    {
        std::string name;
        std::string dtype;
        std::vector<std::size_t> shape;
        std::string bytes;
    };
    std::uint32_t state = 12345;
    const auto next_float = [&state]() {
        state = state * 1664525u + 1013904223u;
        return (static_cast<float>((state >> 8) & 0xFFFF) / 65535.0f - 0.5f) * 0.6f;
    };
    std::vector<Tensor> tensors;
    const auto floats = [&](const std::string &name, std::vector<std::size_t> shape, float fixed, bool random) {
        std::size_t count = 1;
        for (auto d : shape)
            count *= d;
        Tensor t{name, "F32", std::move(shape), {}};
        for (std::size_t i = 0; i < count; ++i)
        {
            const float v = random ? next_float() : fixed;
            t.bytes.append(reinterpret_cast<const char *>(&v), 4);
        }
        tensors.push_back(std::move(t));
    };
    const auto matrix = [&](const std::string &name, std::size_t rows, std::size_t cols) {
        if (!quantized)
        {
            floats(name, {rows, cols}, 0, true);
            return;
        }
        Tensor t{name, "I8", {rows, cols}, {}};
        Tensor scale{name + ".scale", "F32", {rows}, {}};
        for (std::size_t r = 0; r < rows; ++r)
        {
            for (std::size_t c = 0; c < cols; ++c)
                t.bytes.push_back(static_cast<char>(static_cast<int>(next_float() * 200.0f)));
            const float s = 0.005f;
            scale.bytes.append(reinterpret_cast<const char *>(&s), 4);
        }
        tensors.push_back(std::move(t));
        tensors.push_back(std::move(scale));
    };
    matrix("tok.weight", 8, 4);
    floats("pos.weight", {8, 4}, 0, true);
    floats("blocks.0.ln1.weight", {4}, 1.0f, false);
    floats("blocks.0.ln1.bias", {4}, 0.0f, false);
    matrix("blocks.0.qkv.weight", 12, 4);
    floats("blocks.0.qkv.bias", {12}, 0.0f, false);
    matrix("blocks.0.proj.weight", 4, 4);
    floats("blocks.0.proj.bias", {4}, 0.0f, false);
    floats("blocks.0.ln2.weight", {4}, 1.0f, false);
    floats("blocks.0.ln2.bias", {4}, 0.0f, false);
    matrix("blocks.0.fc.weight", 16, 4);
    floats("blocks.0.fc.bias", {16}, 0.0f, false);
    matrix("blocks.0.out.weight", 4, 16);
    floats("blocks.0.out.bias", {4}, 0.0f, false);
    floats("ln_f.weight", {4}, 1.0f, false);
    floats("ln_f.bias", {4}, 0.0f, false);

    std::string header = "{\"__metadata__\":{\"config\":\"{\\\"vocab\\\":8,\\\"n_layer\\\":1,\\\"n_head\\\":1,"
                         "\\\"n_embd\\\":4,\\\"context\\\":8}\","
                         "\"vocab\":\"[\\\"<pad>\\\",\\\"<unk>\\\",\\\"<bos>\\\",\\\"你\\\",\\\"好\\\",\\\"世\\\","
                         "\\\"界\\\",\\\"我\\\"]\"}";
    std::string data;
    for (const Tensor &t : tensors)
    {
        header += ",\"" + t.name + "\":{\"dtype\":\"" + t.dtype + "\",\"shape\":[";
        for (std::size_t i = 0; i < t.shape.size(); ++i)
            header += (i ? "," : "") + std::to_string(t.shape[i]);
        header += "],\"data_offsets\":[" + std::to_string(data.size()) + "," +
                  std::to_string(data.size() + t.bytes.size()) + "]}";
        data += t.bytes;
    }
    header += "}";
    std::string out;
    put_le(out, header.size(), 8);
    return out + header + data;
}

// Japanese model (MSJPDT1): a handful of sorted tokens over a 4x4 connection matrix.
inline std::string japanese_model()
{
    struct Entry
    {
        std::string reading, surface;
        std::uint16_t left, right;
        std::int32_t cost;
    };
    std::vector<Entry> entries = {
        {"かな", "仮名", 1, 1, 3000},      {"かな", "神奈", 2, 2, 5000},     {"きょう", "今日", 1, 2, 2500},
        {"きょう", "京", 2, 1, 6000},       {"こんにちは", "今日は", 3, 3, 4000}, {"にほんご", "日本語", 1, 3, 3200},
        {"は", "は", 3, 0, 1500},           {"わたし", "私", 1, 1, 2000},     {"わたし", "渡し", 2, 2, 5500},
        {"の", "の", 3, 3, 1200},
    };
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.reading < b.reading; });
    std::string strings;
    std::string tokens;
    for (const Entry &e : entries)
    {
        put_le(tokens, strings.size(), 4);
        put_le(tokens, e.reading.size(), 2);
        strings += e.reading;
        put_le(tokens, strings.size(), 4);
        put_le(tokens, e.surface.size(), 2);
        strings += e.surface;
        put_le(tokens, e.left, 2);
        put_le(tokens, e.right, 2);
        put_le(tokens, static_cast<std::uint32_t>(e.cost), 4);
    }
    std::string matrix;
    for (int right = 0; right < 4; ++right)
        for (int left = 0; left < 4; ++left)
            put_le(matrix, static_cast<std::uint16_t>((right * 7 + left * 3) % 11 * 40 - 100), 2);
    const std::uint64_t token_offset = 56;
    const std::uint64_t connection_offset = token_offset + tokens.size();
    const std::uint64_t string_offset = connection_offset + matrix.size();
    std::string out("MSJPDT1", 8);
    put_le(out, 1, 4);
    put_le(out, entries.size(), 4);
    put_le(out, 4, 4);
    put_le(out, 0, 4);
    put_le(out, token_offset, 8);
    put_le(out, connection_offset, 8);
    put_le(out, string_offset, 8);
    put_le(out, strings.size(), 8);
    return out + tokens + matrix + strings;
}

// Rime::Grammar double-array (octagram): context+word keys encoded like GramDb::encode.
inline std::string gram_model(const std::vector<std::pair<std::string, int>> &encoded_keys)
{
    std::vector<std::pair<std::string, int>> sorted = encoded_keys;
    std::sort(sorted.begin(), sorted.end());
    std::vector<const char *> keys;
    std::vector<int> values;
    for (const auto &item : sorted)
    {
        keys.push_back(item.first.c_str());
        values.push_back(item.second);
    }
    Darts::DoubleArray trie;
    trie.build(keys.size(), keys.data(), nullptr, values.data());
    std::string out(32, '\0');
    std::memcpy(&out[0], "Rime::Grammar/1.0", 17);
    put_le(out, 0, 4);
    put_le(out, trie.size(), 4);
    put_le(out, 4, 4);
    out.append(static_cast<const char *>(trie.array()), trie.total_size());
    return out;
}

} // namespace fixtures
