// Drives the public Session state machine (all four input schemes) with a byte-coded script of
// keystrokes, candidate actions and live setting changes, over a tiny in-tree resource bundle.
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <unistd.h>

#include <metasequoia/session.h>

#include "session_fixture.h"

namespace fs = std::filesystem;
using namespace metasequoia;

namespace
{
fs::path g_root;
fs::path g_resources;

SchemeType scheme_of(unsigned v)
{
    switch (v & 3)
    {
    case 0:
        return SchemeType::Quanpin;
    case 1:
        return SchemeType::Shuangpin;
    case 2:
        return SchemeType::Wubi;
    default:
        return SchemeType::JapaneseRomaji;
    }
}

void run_op(Session &session, const uint8_t *data, size_t size, size_t &i, bool &shift)
{
    const uint8_t b = data[i++];
    const unsigned low = b & 0x0F;
    if (b >= 0x20 && b < 0x7F)
    {
        session.character(static_cast<char>(b), shift);
        shift = false;
    }
    else if (b < 0x09)
    {
        session.command(static_cast<Command>(b));
    }
    else if (b >= 0x80 && b < 0x90)
    {
        session.candidate_key(static_cast<char>('0' + low % 10));
    }
    else if (b >= 0x90 && b < 0xA0)
    {
        const char ch = i < size && data[i] >= 0x21 && data[i] < 0x7F ? static_cast<char>(data[i++]) : ',';
        session.punctuation(ch);
    }
    else if (b >= 0xA0 && b < 0xB0)
    {
        session.select(low);
    }
    else if (b >= 0xB0 && b < 0xC0)
    {
        session.select_edge(low >> 1, (low & 1) ? CandidateEdge::LastHan : CandidateEdge::FirstHan);
    }
    else if (b >= 0xC0 && b < 0xD0)
    {
        const std::size_t index = low >> 2;
        switch (low & 3)
        {
        case 0:
            session.pin(index);
            break;
        case 1:
            session.remove(index);
            break;
        case 2:
            session.fix_position(index, 1 + static_cast<int>(low % 5));
            break;
        default:
            session.clear_position(index);
            break;
        }
    }
    else if (b >= 0xD0 && b < 0xE0)
    {
        if (low & 1)
            session.finish(low >> 1);
        else
            session.finish();
    }
    else if (b >= 0xE0 && b < 0xE4)
    {
        session.switch_scheme(scheme_of(b));
    }
    else if (b == 0xE4 || b == 0xE5)
    {
        session.set_dedicated_english(b == 0xE4);
    }
    else if (b == 0xE6 || b == 0xE7)
    {
        session.set_wubi_mixed_pinyin(b == 0xE6);
    }
    else if (b == 0xE8 || b == 0xE9)
    {
        session.set_helpcode_enabled(b == 0xE8);
    }
    else if (b == 0xEA || b == 0xEB)
    {
        session.set_chinese_punctuation_enabled(b == 0xEA);
    }
    else if (b == 0xEC || b == 0xED)
    {
        session.set_helpcode_schema(b == 0xEC ? "lantian" : "xiaohe");
    }
    else if (b == 0xEE)
    {
        if (const auto query = session.online_query())
            session.apply_online_candidate(*query, "你好", CandidateSource::CloudSuggestion);
    }
    else if (b == 0xEF)
    {
        shift = true;
    }
    (void)session.snapshot();
}
} // namespace

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    const char *tmp = std::getenv("TMPDIR");
    const fs::path base = fs::absolute(tmp != nullptr && *tmp != '\0' ? fs::path(tmp) : fs::path("."));
    g_root = base / ("msime-fuzz-" + std::to_string(static_cast<unsigned long>(::getpid())));
    g_resources = g_root / "resources";
    fs::create_directories(g_root);
    session_fixture::build_resources(g_resources);
    std::atexit([] {
        std::error_code ec;
        fs::remove_all(g_root, ec);
    });
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 3)
        return -1;
    const fs::path user = g_root / "user";
    const fs::path cache = g_root / "cache";
    SessionOptions options;
    options.paths = prepare_runtime_paths(g_resources, user, cache, "v1");
    options.scheme = scheme_of(data[0]);
    const uint8_t flags = data[1];
    options.helpcode = (flags & 1) != 0;
    options.chinese_punctuation = (flags & 2) != 0;
    options.learning = (flags & 4) != 0;
    options.autocorrect_types = (flags >> 3) & 0x0F;
    options.english.mixed_candidates = (flags & 0x80) != 0;
    options.expressive.emoji_candidates = (flags & 0x40) != 0;
    options.fuzzy_pinyin.rules = (static_cast<std::uint32_t>(flags) * 0x01010101u) & 0x3FFu;
    options.wubi.z_wildcard = (flags & 0x20) != 0;
    options.shuangpin_profile = GetShuangpinProfile(data[0] & 4 ? "microsoft" : "xiaohe");
    {
        Session session(options);
        size_t i = 2;
        bool shift = false;
        for (unsigned ops = 0; i < size && ops < 192; ++ops)
            run_op(session, data, size, i, shift);
    }
    std::error_code ec;
    fs::remove_all(user, ec);
    fs::remove_all(cache, ec);
    return 0;
}
