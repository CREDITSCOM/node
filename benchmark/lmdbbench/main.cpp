#include <lmdb.hpp>
#include <framework.hpp>

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

static void runBench(cs::Lmdb* db) {
    std::string key = "Key";
    std::string value = "Value";

    for (size_t i = 0; i < 10000; ++i) {
        db->insert(key + std::to_string(i), value);
    }
}

static void testLmdb(unsigned int flags) {
    const char* path = "testdbpath";

    cs::Lmdb db(path);
    db.open(flags);

    cs::Framework::execute(std::bind(&runBench, &db), std::chrono::seconds(100), "Db run failed");

    db.close();
    fs::remove_all(fs::path(path));
}

static void testLmdbDefaultFlags() {
    testLmdb(lmdb::env::default_flags);
}

static void testLmdbWithFlags() {
    testLmdb(MDB_NOSYNC | MDB_WRITEMAP | MDB_MAPASYNC);
}

int main() {
    testLmdbDefaultFlags();
    testLmdbWithFlags();

    return 0;
}
