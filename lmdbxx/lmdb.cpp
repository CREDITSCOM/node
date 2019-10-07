#include "lmdb.hpp"
#include <iostream>

#include <lib/system/fileutils.hpp>

namespace fs = boost::filesystem;

cs::Lmdb::Lmdb(const std::string& path, const unsigned int flags): path_(path), flags_(flags) {
    try {
        env_ = environment(flags);

        if (!cs::FileUtils::createPathIfNoExist(path_)) {
            std::cout << "Could not create path for Lmdb: " << path_ << std::endl;
        }
    }
    catch (const lmdb::error& error) {
        std::cout << "Lmdb construction error: " << error.what() << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << "Lmdb construction error: " << e.what() << std::endl;
    }
    catch (...) {
        std::cout << "Lmdb unknown construction error\n";
    }
}

cs::Lmdb::~Lmdb() noexcept {
    close();
}
