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
#include <lib/system/reflection.hpp>
#include <lib/system/console.hpp>

namespace cs {
class FileUtils {
public:
    static bool isPathExist(const std::string& path) {
        namespace fs = boost::filesystem;

        fs::path p(path);
        bool res = false;

        try {
            res = fs::is_directory(p);
        }
        catch (const std::exception& e) {
            cs::Console::writeLine(funcName(), ", ", e.what());
        }

        return res;
    }

    static bool createPath(const std::string& path) {
        namespace fs = boost::filesystem;

        fs::path p(path);
        bool res = false;

        try {
            res = fs::create_directory(p);
        }
        catch (const std::exception& e) {
            cs::Console::writeLine(funcName(), ", ", e.what());
        }

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

    static bool removePath(const std::string& pathName) {
        namespace fs = boost::filesystem;

        fs::path path(pathName);
        bool res = false;

        try {
            res = fs::remove_all(path);
        }
        catch (const std::exception& e) {
            cs::Console::writeLine(funcName(), ", ", e.what());
        }

        return res;
    }
};
}

#endif  //  FILEUTILS_HPP
