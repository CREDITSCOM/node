/**
 * @file utils.h
 * @author Roman Bukin
 */

#ifndef _CREDITS_CSDB_UTILS_H_INCLUDED_
#define _CREDITS_CSDB_UTILS_H_INCLUDED_

#include "csdb/internal/types.hpp"

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
    uint8_t hi, lo;

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
 * @brief Добавляет оконечный разделитель к пути
 * @param Путь к папке
 * @return Путь к папке с добавленым оконечным разделителем
 *
 * Функция не проверят реального существования папки и просто добавляет
 * оконечный разделитель (в зависимости от платформы) в случае его отсутствия.
 *
 * Если оконечный разделитель уже присутствует, путь возвращается без изменений.
 */
std::string path_add_separator(const std::string &path);

/**
 * @brief Путь к папке для хранения данных приложения.
 * @return Путь к папке для хранения данных приложения.
 *
 * Функция возвращает специфичный для платформы путь для хранения данных приложения.
 * Если папки не существует, папака создаётся.
 *
 * Возвращаемый путь всегда завершается разделителем (слешем)
 */
std::string app_data_path();

/**
 * @brief Размер файла
 * @param[in] name Имя файла
 * @return Размер файла в байтах, или static_cast<size_t>(-1), если файл не существует или является папкой.
 */
size_t file_size(const std::string &name);

/**
 * @brief exists_file функция проверяет существование файла
 * @param path путь до проверяемого файла
 * @return true - файл существует
 */
inline bool file_exists(const std::string &path) {
  return (static_cast<uint64_t>(-1) != file_size(path));
}

/**
 * @brief remove_file функция удаляет файл
 * @param path путь до файла
 * @return true - файл удален
 */
bool file_remove(const std::string &path);

/**
 * @brief exists_dir функция проверяет существование директории
 * @param path путь до проверяемой директории
 * @return true - директория существует
 */
bool dir_exists(const std::string &path);

/**
 * @brief make_dir функция создает директорию
 * Для успешного создания директории необходимо,
 * что-бы родительская директория для создаваемой существовала.
 * Например, для успешного создания директории /home/user/.appdata/storage,
 * необходимо что-бы уже существовала директория /home/user/.appdata
 * @param path путь до создаваемой директории
 * @return true - директория создана
 */
bool dir_make(const std::string &path);

/**
 * @brief remove_dir удалить деркторию
 * Для успешного удалениия директории, она должна быть пуста.
 * @param path путь до удаляемой директории
 * @return true - дерктория удалена
 */
bool dir_remove(const std::string &path);

/**
 * @brief make_path функция создает полный путь до указанной директории
 * В процессе создания пути будут созданы все промежуточные директории
 * @param path путь до создаваемой директории
 * @return true - директория полностью создан
 */
bool path_make(const std::string &path);

/**
 * @brief remove_path функция удаляет директорию по указанному пути
 * со всем содержимым (будут удалены все файлы и директории)
 * @param path путь до удаляемой директории
 * @return true - директория полностью удалена
 */
bool path_remove(const std::string &path);

/**
 * @brief Объём папки
 * @param[in] path Путь к папке (оконочный разделитель не обязателен)
 * @return Суммарный размер всех файлов в папке, включая вложенные папки.
 *
 * Если указанный путь является файлом, возвращается размер файла. Если ни папка, ни файл
 * с таким имененм не существуют, возвращается static_cast<size_t>(-1).
 */
size_t path_size(const std::string &path);

}  // namespace internal
}  // namespace csdb

#endif // _CREDITS_CSDB_UTILS_H_INCLUDED_
