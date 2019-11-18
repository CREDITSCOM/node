#ifndef MMAPPEDFILE_H
#define MMAPPEDFILE_H

#include <exception>

#include <boost/iostreams/device/mapped_file.hpp>

#include <lib/system/logger.hpp>

namespace cs {
using FileSource = boost::iostreams::mapped_file_source;
using FileSink = boost::iostreams::mapped_file_sink;

template <class BoostMMapedFile>
class MMappedFileWrap {
public:
    MMappedFileWrap(const std::string& path,
            size_t maxSize = boost::iostreams::mapped_file::max_length,
            bool createNew = true) {
        try {
            if (!createNew) {
                file_.open(path, maxSize);
            }
            else {
                boost::iostreams::mapped_file_params params;
                params.path = path;
                params.new_file_size = maxSize;
                file_.open(params);
            }
        }
        catch (std::exception& e) {
            cserror() << e.what();
        }
        catch (...) {
            cserror() << __FILE__ << ", "
                      << __LINE__
                      << " exception ...";
        }
    }

    bool isOpen() {
        return file_.is_open();
    }

    ~MMappedFileWrap() {
        if (isOpen()) {
            file_.close();
        }
    }

    template<typename T>
    T* data() {
        return isOpen() ? (T*)file_.data() : nullptr;
    }

private:
    BoostMMapedFile file_;
};
} // namespace cs
#endif // MMAPPEDFILE_H
