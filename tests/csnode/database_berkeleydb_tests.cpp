#include "gtest/gtest.h"

#include <cerrno>
#include <exception>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <csdb/database_berkeleydb.hpp>

// A failed open leaves some handles null: destroying the object afterwards must not crash.
TEST(DatabaseBerkeleyDB, DestroyAfterAFailedOpen) {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "cs_berkeleydb_failed_open";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "blockchain.db", std::ios::binary);
        f << std::string(128 * 1024, 'x');   // not a BerkeleyDB file
    }
    std::ofstream(dir / "sequence.db").close();

    bool opened = true;
    {
        auto db = std::make_unique<csdb::DatabaseBerkeleyDB>();
        try {
            opened = db->open(dir.string());
        }
        catch (const std::exception&) {
            opened = false;
        }
        EXPECT_FALSE(static_cast<csdb::Database&>(*db).is_open());
    }   // the destructor runs here
    EXPECT_FALSE(opened);
    fs::remove_all(dir);
}

// an environment that needs recovery stops the node (the error is rethrown); every other error, a full
// disk included, is a failed call the node retries
TEST(DatabaseBerkeleyDB, OnlyARecoveryErrorIsFatal) {
    EXPECT_TRUE(csdb::needsRecovery(DbRunRecoveryException("panic")));
    EXPECT_TRUE(csdb::needsRecovery(DbException("panic", DB_RUNRECOVERY)));
    EXPECT_FALSE(csdb::needsRecovery(DbException("disk full", ENOSPC)));
    EXPECT_FALSE(csdb::needsRecovery(DbException("not found", DB_NOTFOUND)));
    EXPECT_FALSE(csdb::needsRecovery(std::runtime_error("other")));
}
