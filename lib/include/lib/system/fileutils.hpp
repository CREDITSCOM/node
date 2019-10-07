#ifndef FILEUTILS_HPP
#define FILEUTILS_HPP

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <fstream>

#include <lib/system/common.hpp>

namespace cs {
class FileUtils {
public:
    static bool isPathExist(const std::string& path) {
        namespace fs = boost::filesystem;

        fs::path p(path);
        boost::system::error_code code;
        const auto res = fs::is_directory(p, code);

        return res;
    }

    static bool createPath(const std::string& path) {
        namespace fs = boost::filesystem;

        fs::path p(path);
        boost::system::error_code code;
        const auto res = fs::create_directory(p, code);

        return res;
    }

    static bool createPathIfNoExist(const std::string& path) {
        if (isPathExist(path)) {
            return true;
        }

        return createPath(path);
    }

    static cs::Bytes readAllFileData(const std::string& fileName) {
        std::ifstream file(fileName);
        return cs::Bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    static bool isFileExists(const std::string& fileName) {
        namespace fs = boost::filesystem;

        boost::system::error_code code;
        return fs::exists(fileName, code);
    }
};
}

#endif  //  FILEUTILS_HPP
