// A small but real resource bundle (dictionaries, English words, help codes, Japanese model) for
// driving the public metasequoia::Session. Shared by the session fuzzer and the golden oracle.
#pragma once

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "contracts/assets/assets.h"
#include "core/data_path.h"
#include "english/english_dictionary.h"

#include "fixtures.h"

namespace session_fixture
{

inline void execute(const std::filesystem::path &path, const std::string &sql)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(metasequoia::path_to_utf8(path).c_str(), &db) != SQLITE_OK)
        throw std::runtime_error("fixture open failed");
    const int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    const std::string error = sqlite3_errmsg(db);
    sqlite3_close(db);
    if (result != SQLITE_OK)
        throw std::runtime_error(error);
}

inline void build_resources(const std::filesystem::path &dir)
{
    using namespace metasequoia;
    std::filesystem::create_directories(dir / "helpcodes");
    // Tables are split by syllable count: tbl_{N}_{initial of the first syllable}.
    execute(dir / assets::main_dictionary,
            "CREATE TABLE tbl_1_n(key TEXT, jp TEXT, value TEXT, weight INTEGER);"
            "INSERT INTO tbl_1_n VALUES('ni','n','你',200),('ni','n','泥',120),('ni','n','拟',90);"
            "CREATE TABLE tbl_1_h(key TEXT, jp TEXT, value TEXT, weight INTEGER);"
            "INSERT INTO tbl_1_h VALUES('hao','h','好',200),('hao','h','号',110);"
            "CREATE TABLE tbl_1_m(key TEXT, jp TEXT, value TEXT, weight INTEGER);"
            "INSERT INTO tbl_1_m VALUES('ma','m','吗',200),('ma','m','妈',180);"
            "CREATE TABLE tbl_1_s(key TEXT, jp TEXT, value TEXT, weight INTEGER);"
            "INSERT INTO tbl_1_s VALUES('shi','s','是',210),('shi','s','时',150);"
            "CREATE TABLE tbl_2_n(key TEXT, jp TEXT, value TEXT, weight INTEGER);"
            "INSERT INTO tbl_2_n VALUES('ni''hao','nh','你好',200),('ni''hao','nh','拟好',60);"
            "CREATE TABLE tbl_2_m(key TEXT, jp TEXT, value TEXT, weight INTEGER);"
            "INSERT INTO tbl_2_m VALUES('ma''ma','mm','妈妈',200);"
            "CREATE TABLE tbl_3_n(key TEXT, jp TEXT, value TEXT, weight INTEGER);"
            "INSERT INTO tbl_3_n VALUES('ni''hao''ma','nhm','你好吗',200);"
            "CREATE TABLE quick_parases(key TEXT, value TEXT, weight INTEGER);"
            "INSERT INTO quick_parases VALUES('x','你好短语',10),('xx','再见短语',9);");
    if (!EnglishDictionary::ensure_schema(path_to_utf8(dir / assets::english_dictionary)))
        throw std::runtime_error("english fixture failed");
    execute(dir / assets::english_dictionary,
            "INSERT INTO english_words(word,display,weight) VALUES('hello','hello',10),('help','help',8),"
            "('world','world',7);");
    std::ofstream(dir / assets::translations) << "hello\t你好翻译\n";
    std::ofstream(dir / assets::helpcode_lantian) << "你=aa\n拟=cc\n好=bb\n";
    std::ofstream(dir / assets::helpcode_xiaohe) << "你=cc\n拟=aa\n好=dd\n";
    std::ofstream(dir / assets::japanese_model, std::ios::binary) << fixtures::japanese_model();
}

} // namespace session_fixture
