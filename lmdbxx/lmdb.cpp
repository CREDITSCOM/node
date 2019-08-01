#include "lmdb.hpp"
#include <iostream>

#ifdef __cpp_lib_filesystem
#include <filesystem>
namespace fs = std::filesystem;
#elif
#include <experimental/filesystem>
namespace fs = std::experimenta::filesystem
#endif

cs::Lmdb::Lmdb(const std::string& path, const unsigned int flags) try : env_(lmdb::env::create(flags)), path_(path) {
    fs::path dbPath(path_);
    std::error_code code;
    const auto res = fs::is_directory(dbPath, code);

    if (!res) {
        std::cout << "Lmdb path doesn not exist, creating path " << dbPath.string() << std::endl;
        fs::create_directory(dbPath);
    }
}
catch (const lmdb::error& error) {
    std::cout << "Lmdb construction error: " << error.what() << std::endl;
}
catch(...) {
    std::cout << "Lmdb unknown construction error\n";
}

cs::Lmdb::~Lmdb() noexcept {
    close();
}
