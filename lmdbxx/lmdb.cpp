#include "lmdb.hpp"
#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

namespace fs = boost::filesystem;

cs::Lmdb::Lmdb(const std::string& path, const unsigned int flags) try : env_(environment(flags)), path_(path), flags_(flags) {
    fs::path dbPath(path_);
    boost::system::error_code code;
    const auto res = fs::is_directory(dbPath, code);

    if (!res) {
        std::cout << "Lmdb path does not exist, creating path " << dbPath.string() << std::endl;
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
