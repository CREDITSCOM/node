#ifndef FILEUTILS_HPP
#define FILEUTILS_HPP

#include <boost/filesystem.hpp>

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
};
}

#endif  //  FILEUTILS_HPP
