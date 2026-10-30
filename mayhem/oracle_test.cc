// Known-answer oracle: loads the committed seed models and drives the engine through fixed inputs,
// printing every result. mayhem/test.sh diffs this output against mayhem/oracle.golden, so a
// build that no-ops, exits early or returns different answers fails.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <metasequoia/session.h>

#include "engine/ngram/language_model.h"
#include "japanese/japanese_sentence_decoder.h"
#include "neural/sentence_model.h"
#include "ngram/octagram/octagram_gram.h"
#include "session_fixture.h"

namespace fs = std::filesystem;
using namespace metasequoia;

namespace
{
int failures = 0;

void require(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

std::vector<std::uint8_t> read_bytes(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void sentence(const fs::path &seed, const char *label)
{
    const auto model = neural::SentenceModel::load(read_bytes(seed));
    require(model != nullptr, label);
    if (!model)
        return;
    const auto scores = model->score_sentences("我", {"你好", "世界", "你好世界", "xyz"});
    for (std::size_t i = 0; i < scores.size(); ++i)
        std::printf("%s score[%zu]=%.4f\n", label, i, scores[i]);
    const auto next = model->next_log_probabilities("你好");
    std::size_t best = 0;
    double total = 0;
    for (std::size_t i = 0; i < next.size(); ++i)
    {
        total += std::exp(static_cast<double>(next[i]));
        if (next[i] > next[best])
            best = i;
    }
    std::printf("%s next: n=%zu argmax=%zu mass=%.4f\n", label, next.size(), best, total);
}

void japanese_dictionary(const fs::path &seed)
{
    const japanese::JapaneseSentenceDecoder decoder(seed.string());
    require(decoder.ready(), "japanese model loads");
    for (const char *reading : {"かな", "きょう"})
        for (const auto &lemma : decoder.ExactLemmas(reading))
            std::printf("exact %s -> %s cost=%d ids=%u/%u\n", reading, lemma.surface.c_str(), lemma.word_cost,
                        lemma.left_id, lemma.right_id);
    for (const auto &lemma : decoder.PrefixLemmas("わ"))
        std::printf("prefix わ -> %s\n", lemma.surface.c_str());
    for (const auto &candidate : decoder.Decode("わたしのにほんご", 3))
        std::printf("decode -> %s cost=%lld len=%zu\n", candidate.text.c_str(), static_cast<long long>(candidate.cost),
                    candidate.reading_length);
    std::printf("connection 1,2=%d 3,3=%d\n", decoder.ConnectionCost(1, 2), decoder.ConnectionCost(3, 3));
}

void language_model(const fs::path &seed)
{
    const ngram::LanguageModel model(seed);
    require(model.valid(), "kenlm model loads");
    if (!model.valid())
        return;
    ngram::State state = model.null_state();
    ngram::State next;
    for (const char *word : {"你", "好", "世", "界", "xyz"})
    {
        const float score = model.score(state, word, next);
        state = next;
        std::printf("lm %s unk=%d score=%.4f\n", word, model.is_unknown(model.index(word)) ? 1 : 0, score);
    }
}

void grammar(const fs::path &seed)
{
    gram::GramDb db;
    require(db.open(seed), "gram db opens");
    gram::GrammarConfig config;
    config.collocation_penalty = 0.0;
    config.rear_penalty = 5.0;
    gram::QueryStats stats;
    const double a = db.query("你", "好", false, config, &stats);
    std::printf("gram 你|好=%.4f windows=%d matches=%d best=%.4f\n", a, stats.context_windows, stats.matches,
                stats.best_value);
    std::printf("gram 我你好|世界=%.4f\n", db.query("我你好", "世界", false, config));
    std::printf("gram 世|界 rear=%.4f\n", db.query("世", "界", true, config));
    std::printf("gram 好|吗=%.4f\n", db.query("好", "吗", false, config));
}

void keystrokes(const fs::path &root)
{
    const fs::path resources = root / "resources";
    session_fixture::build_resources(resources);
    SessionOptions options;
    options.paths = prepare_runtime_paths(resources, root / "user", root / "cache", "v1");
    options.learning = false;
    Session session(options);
    for (char ch : std::string("nihao"))
        session.character(ch);
    auto snap = session.snapshot();
    std::printf("session preedit=%s candidates=", snap.preedit.c_str());
    for (std::size_t i = 0; i < snap.candidates.size() && i < 6; ++i)
        std::printf("%s%s", i ? "," : "", snap.candidates[i].word.c_str());
    std::printf("\n");
    const auto done = session.select(0);
    std::printf("session commit=%s\n", done.commit ? done.commit->c_str() : "(none)");
    session.character('n');
    session.character('i');
    session.command(Command::MoveLeft);
    std::printf("session caret=%zu text=%s\n", session.snapshot().caret_position,
                session.snapshot().editing_text.c_str());
    session.command(Command::Cancel);
    std::printf("session punct=%s\n", session.punctuation(',').commit.value_or("(none)").c_str());

    options.scheme = SchemeType::JapaneseRomaji;
    Session japanese(options);
    for (char ch : std::string("kyou"))
        japanese.character(ch);
    snap = japanese.snapshot();
    std::printf("japanese preedit=%s candidates=", snap.preedit.c_str());
    for (std::size_t i = 0; i < snap.candidates.size() && i < 4; ++i)
        std::printf("%s%s", i ? "," : "", snap.candidates[i].word.c_str());
    std::printf("\n");
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: oracle_test <mayhem-dir> <scratch-dir>\n");
        return 2;
    }
    const fs::path mayhem = argv[1];
    sentence(mayhem / "sentence_model/testsuite/f32.safetensors", "f32");
    sentence(mayhem / "sentence_model/testsuite/i8.safetensors", "i8");
    japanese_dictionary(mayhem / "japanese_model/testsuite/small.dat");
    language_model(mayhem / "language_model/testsuite/small.lm");
    grammar(mayhem / "gram/testsuite/small.gram");
    keystrokes(argv[2]);
    return failures == 0 ? 0 : 1;
}
