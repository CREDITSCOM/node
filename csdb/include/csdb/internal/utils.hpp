/**
 * @file utils.h
 * @author Roman Bukin
 */

#ifndef _CREDITS_CSDB_UTILS_H_INCLUDED_
#define _CREDITS_CSDB_UTILS_H_INCLUDED_

#include <csdb/internal/types.hpp>

namespace csdb {
namespace internal {

template <typename Iterator>
cs::Bytes from_hex(Iterator beg, Iterator end) {
    auto digit_from_hex = [](char val, uint8_t &result) {
        val = static_cast<char>(toupper(val));

        if ((val >= '0') && (val <= '9'))
            result = static_cast<uint8_t>(val - '0');
        else if ((val >= 'A') && (val <= 'F'))
            result = static_cast<uint8_t>(val - 'A' + 10);
        else
            return false;

        return true;
    };

    cs::Bytes res;
    res.reserve(std::distance(beg, end) / 2);
    for (Iterator it = beg;;) {
        uint8_t hi = 0, lo = 0;

        if (it == end || !digit_from_hex(*it++, hi))
            break;

        if (it == end || !digit_from_hex(*it++, lo))
            break;

        res.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return res;
}

template <typename Iterator>
std::string to_hex(Iterator beg, Iterator end) {
    auto digit_to_hex = [](uint8_t val) { return static_cast<char>((val < 10) ? (val + '0') : (val - 10 + 'A')); };

    std::string res;
    res.reserve(std::distance(beg, end) * 2);
    for (Iterator it = beg; it != end; it++) {
        res.push_back(digit_to_hex((*it >> 4) & 0x0F));
        res.push_back(digit_to_hex(*it & 0x0F));
    }
    return res;
}

inline cs::Bytes from_hex(const std::string &val) {
    return from_hex(val.begin(), val.end());
}

inline std::string to_hex(const cs::Bytes &val) {
    return to_hex(val.begin(), val.end());
}

/**
 * @brief Adds final splitter to path
 * @param Path to folder
 * @return Path to folder with adding the final splitter
 *
 * Method doesn't check the existance of folder, but just adds
 * the final splitter (depending of platform) in case of its absence.
 *
 * If the final splitter presents the path is returned without the changes.
 */
std::string path_add_separator(const std::string &path);

/**
 * @brief Path to the directory with application data.
 * @return  Path to the directory with application data.
 *
 * Method returns the specific for the platform path to application data.
 * If the directory doesn't exist it'll be created.
 *
 * The returned path is always finished with splitter (slash)
 */
std::string app_data_path();

/**
 * @brief File size
 * @param[in] name - File name
 * @return File size in bytes, or static_cast<size_t>(-1), if the file doesn't exist or is a Folder.
 */
size_t file_size(const std::string &name);

/**
 * @brief exists_file method checks the existance of the file
 * @param path - path to checked file
 * @return true - if file exists
 */
inline bool file_exists(const std::string &path) {
    return (static_cast<uint64_t>(-1) != file_size(path));
}

/**
 * @brief remove_file method removes file
 * @param path - path to file
 * @return true - if metod removes file
 */
bool file_remove(const std::string &path);

/**
 * @brief exists_dir method
 * @param path - path to checked directory
 * @return true - if directory exists
 */
bool dir_exists(const std::string &path);

/**
 * @brief make_dir method creates directory
 * To sucessfully create directory it's necessary,
 * that the parent directory exists.
 * I.e., to create directory /home/user/.appdata/storage,
 * it's necessary the directory /home/user/.appdata exists.
 * @param path - path to created directory
 * @return true - if directory created
 */
bool dir_make(const std::string &path);

/**
 * @brief remove_dir - removes directory
 * To remove directory sucessfully it should be empty.
 * @param path - path to removed directory
 * @return true - if directory removed sucessfully
 */
bool dir_remove(const std::string &path);

/**
 * @brief make_path method creates full path to given directory
 * During the path creation all necessary directories will be created
 * @param path - path to created directory
 * @return true - if the directory is fully created
 */
bool path_make(const std::string &path);

/**
 * @brief remove_path - method removes the directory at the given path
 * aith all content (all directories and files will be removed)
 * @param path path to given directory
 * @return true - is the directory is fully removed
 */
bool path_remove(const std::string &path);

/**
 * @brief directory size
 * @param[in] path - path to the directory (final splitter is not necessary)
 * @return Summary size of all files in the directory including nested folders.
 *
 * If the given path is the file, method gies the file size. If there is no folder nor file
 * with diven name(path) exists, method returns static_cast<size_t>(-1).
 */
size_t path_size(const std::string &path);

}  // namespace internal
}  // namespace csdb

#endif // _CREDITS_CSDB_UTILS_H_INCLUDED_
