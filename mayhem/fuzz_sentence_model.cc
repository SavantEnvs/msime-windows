// Fuzzes the safetensors sentence-model loader and the scorer that runs on whatever it accepted.
//
// The loader trusts the header: lengths, offsets and tensor shapes are cast straight to size_t and
// never checked against the model config, so a mutated header re-finds the same few out-of-bounds
// reads at every call site. This harness rejects (never repairs) inputs that fail a structural
// check, so the fuzzer spends its time in the loader's real parsing and in the scorer.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "neural/json.h"
#include "neural/sentence_model.h"

#include "harness_util.h"

namespace
{

using neural::json::Value;

// A non-negative integer that is small enough to be a count or an offset.
bool as_index(const Value *value, std::size_t &out)
{
    if (value == nullptr || !value->is_number())
        return false;
    const double d = value->number;
    if (!(d >= 0.0 && d <= 1e9) || d != std::floor(d))
        return false;
    out = static_cast<std::size_t>(d);
    return true;
}

bool shape_of(const Value *value, std::vector<std::size_t> &out)
{
    out.clear();
    if (value == nullptr || !value->is_array())
        return false;
    for (const Value &dim : value->array)
    {
        std::size_t d = 0;
        if (!as_index(&dim, d))
            return false;
        out.push_back(d);
    }
    return true;
}

struct Tensor
{
    std::string dtype;
    std::vector<std::size_t> shape;
    std::size_t off0 = 0;
    std::size_t off1 = 0;
};

bool tensor_of(const Value &desc, Tensor &out)
{
    const Value *dtype = desc.find("dtype");
    const Value *offsets = desc.find("data_offsets");
    if (dtype == nullptr || !dtype->is_string() || offsets == nullptr || !offsets->is_array() ||
        offsets->array.size() != 2)
        return false;
    out.dtype = dtype->string;
    return shape_of(desc.find("shape"), out.shape) && as_index(&offsets->array[0], out.off0) &&
           as_index(&offsets->array[1], out.off1);
}

std::size_t elements(const std::vector<std::size_t> &shape)
{
    std::size_t total = 1;
    for (std::size_t d : shape)
        total *= d; // every dim is <= 1e9 and the config bounds keep the expected shapes tiny
    return total;
}

// True iff the header describes a model the engine can load without reading outside the body:
// every tensor range is inside the body, and every tensor the loader reads has the dtype, shape and
// byte length the config implies.
bool well_formed(const std::vector<std::uint8_t> &bytes)
{
    std::uint64_t header_len = 0;
    std::memcpy(&header_len, bytes.data(), 8);
    if (header_len == 0 || header_len > bytes.size() - 8)
        return false;
    const std::size_t data_len = bytes.size() - 8 - static_cast<std::size_t>(header_len);

    Value header;
    try
    {
        header = neural::json::parse(std::string(reinterpret_cast<const char *>(bytes.data()) + 8,
                                                 static_cast<std::size_t>(header_len)));
    }
    catch (const std::exception &)
    {
        return false;
    }
    if (!header.is_object())
        return false;
    const Value *metadata = header.find("__metadata__");
    if (metadata == nullptr || !metadata->is_object())
        return false;
    const Value *config_text = metadata->find("config");
    if (config_text == nullptr || !config_text->is_string())
        return false;

    std::size_t vocab = 0, layers = 0, heads = 0, embd = 0, context = 0;
    try
    {
        const Value config = neural::json::parse(config_text->string);
        if (!config.is_object() || !as_index(config.find("vocab"), vocab) ||
            !as_index(config.find("n_layer"), layers) || !as_index(config.find("n_head"), heads) ||
            !as_index(config.find("n_embd"), embd) || !as_index(config.find("context"), context))
            return false;
    }
    catch (const std::exception &)
    {
        return false;
    }
    if (vocab < 1 || vocab > 64 || layers < 1 || layers > 2 || heads < 1 || embd < 1 || embd > 32 ||
        embd % heads != 0 || context < 1 || context > 32)
        return false;

    // Every tensor descriptor the loader would walk must be sane and inside the body.
    for (const auto &field : header.object)
    {
        if (field.first == "__metadata__" || !field.second.is_object())
            continue;
        Tensor tensor;
        if (!tensor_of(field.second, tensor) || tensor.off0 > tensor.off1 || tensor.off1 > data_len)
            return false;
    }

    auto expect = [&](const std::string &name, std::vector<std::size_t> shape, bool matrix) {
        const Value *desc = header.find(name);
        Tensor tensor;
        if (desc == nullptr || !desc->is_object() || !tensor_of(*desc, tensor) || tensor.shape != shape)
            return false;
        const std::size_t count = elements(shape);
        const std::size_t span = tensor.off1 - tensor.off0;
        if (tensor.dtype == "F32")
            return span == count * 4;
        if (tensor.dtype == "F16")
            return span == count * 2;
        if (tensor.dtype != "I8" || !matrix)
            return false;
        // The int8 payload carries one float scale per row in a companion tensor.
        const Value *scale_desc = header.find(name + ".scale");
        Tensor scale;
        return span == count && scale_desc != nullptr && scale_desc->is_object() && tensor_of(*scale_desc, scale) &&
               scale.shape == std::vector<std::size_t>{shape.front()} && scale.off1 - scale.off0 == shape.front() * 4;
    };

    if (!expect("tok.weight", {vocab, embd}, true) || !expect("pos.weight", {context, embd}, false) ||
        !expect("ln_f.weight", {embd}, false) || !expect("ln_f.bias", {embd}, false))
        return false;
    for (std::size_t layer = 0; layer < layers; ++layer)
    {
        const std::string p = "blocks." + std::to_string(layer) + ".";
        if (!expect(p + "ln1.weight", {embd}, false) || !expect(p + "ln1.bias", {embd}, false) ||
            !expect(p + "qkv.weight", {3 * embd, embd}, true) || !expect(p + "qkv.bias", {3 * embd}, false) ||
            !expect(p + "proj.weight", {embd, embd}, true) || !expect(p + "proj.bias", {embd}, false) ||
            !expect(p + "ln2.weight", {embd}, false) || !expect(p + "ln2.bias", {embd}, false) ||
            !expect(p + "fc.weight", {4 * embd, embd}, true) || !expect(p + "fc.bias", {4 * embd}, false) ||
            !expect(p + "out.weight", {embd, 4 * embd}, true) || !expect(p + "out.bias", {embd}, false))
            return false;
    }
    return true;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 16)
        return -1;
    const std::vector<std::uint8_t> bytes(data, data + size);
    if (!well_formed(bytes))
        return -1;
    const auto model = neural::SentenceModel::load(bytes);
    if (!model)
        return -1;
    const std::string tail = tail_bytes(data, size, 12);
    (void)model->next_log_probabilities("你好");
    (void)model->next_log_probabilities(tail);
    (void)model->score_sentences("我", {"你好", "世界", "你好世界", tail});
    (void)model->score_sentences(tail, {"好", ""});
    return 0;
}
