#include <lmdb.hpp>
#include <framework.hpp>

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

static void runBench(cs::Lmdb* db) {
    std::string key = "Key";
    std::string value = "Value";

    for (size_t i = 0; i < 1000; ++i) {
        db->insert(key + std::to_string(i), value);
    }
}

static void testLmdbDefaultFlags() {
    const char* path = "testdbpath";

    cs::Lmdb db(path);
    db.open();

    cs::Framework::execute(std::bind(&runBench, &db), std::chrono::seconds(100), "Db run failed");

    db.close();
    fs::remove_all(fs::path(path));
}

int main() {
    testLmdbDefaultFlags();
    return 0;
}
