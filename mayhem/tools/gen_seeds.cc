// Regenerates the committed fuzz seeds (sentence/japanese/gram models) from mayhem/fixtures.h.
// Not part of the build; run by hand: clang++ -std=c++17 -I engine/ngram/octagram -I mayhem \
//   -I . mayhem/tools/gen_seeds.cc engine/ngram/octagram/octagram_gram.cpp -I mayhem/compat -o gen_seeds
// then ./gen_seeds <mayhem dir>. The kenlm seed is kenlm's build_binary output:
//   build_binary -s -a 22 -q 4 trie mayhem/language_model/arpa.txt mayhem/language_model/testsuite/small.lm
#include <filesystem>
#include <fstream>

#include "engine/ngram/octagram/octagram_gram.h"
#include "fixtures.h"

static void write(const std::filesystem::path &path, const std::string &bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

int main(int argc, char **argv)
{
    const std::filesystem::path root = argc > 1 ? argv[1] : "mayhem";
    write(root / "sentence_model/testsuite/f32.safetensors", fixtures::safetensors_model(false));
    write(root / "sentence_model/testsuite/i8.safetensors", fixtures::safetensors_model(true));
    write(root / "japanese_model/testsuite/small.dat", fixtures::japanese_model());
    const auto key = [](const char *context, const char *word, int value) {
        return std::make_pair(gram::GramDb::encode(context) + gram::GramDb::encode(word), value);
    };
    write(root / "gram/testsuite/small.gram",
          fixtures::gram_model({key("你", "好", 6000), key("你好", "世界", 7500), key("好", "世", 3000),
                                key("世", "界", 5200), key("我", "你", 4100), key("你好", "$", 2000),
                                key("界", "$", 1800), key("a", "b", 900)}));
}
