/**
 * @file csdb.h
 * @author Evgeny V. Zalivochkin
 */

#ifndef _CREDITS_CSDB_H_INCLUDED_
#define _CREDITS_CSDB_H_INCLUDED_

#include "csdb/storage.hpp"

namespace csdb {
/* Набор глобальныйх функций предназначен во-первых, для совместимости с предыдущей версией csdb,
 * а во-вторых, для работы с единственным хранилищем, без создания экземпляров класса \ref ::csdb::Storage.
 *
 * Набор практически копирует набор методов \ref ::csdb::Storage, реализуюя работу с единственным
 * внутренним экземпляром объекта класса. Подробные описания функций находятся в документации к
 * классу \ref ::csdb::Storage.
 */

/**
 * @brief Инициализация внутреннего объекта хранилища
 *
 * Параметры функции эквивалентны параметрам \ref ::csdb::Storage::open. Основное отличие заключается
 * в том, что в случае, если внутренний объект \ref ::csdb::Storage уже открыт, то функция не
 * пытается открыть новое хранилище, а сразу возвращает false.
 *
 * @sa ::csdb::Storage::open
 */
bool init(const Storage::OpenOptions& options, Storage::OpenCallback callback = nullptr);

/**
 * @overload
 */
bool init(const char* path_to_bases, const Storage::OpenCallback& callback = nullptr);

/**
 * @brief Проверка, инициализирован ли внутренний объект хранилища
 * @sa ::csdb::Storage::isOpen
 */
bool isInitialized();

/**
 * @brief Закрытие внутреннего объекта хранилища
 * @sa ::csdb::Storage::close
 */
void done();

/**
 * @brief Код последней ошибки
 * @sa ::csdb::Storage::last_error()
 */
Storage::Error lastError();

/**
 * @brief Описание последней ошибки
 * @sa ::csdb::Storage::last_error_message()
 */
::std::string lastErrorMessage();

/**
 * @brief Код последней ошибки работы с драйвером базы данных
 * @sa ::csdb::Storage::db_last_error()
 */
Database::Error dbLastError();

/**
 * @brief Описание последней ошибки работы с драйвером базы данных
 * @sa ::csdb::Storage::db_last_error_message()
 */
::std::string dbLastErrorMessage();

/**
 * @brief Хранилище по умолчанию
 * @return Внутренний екземпляр объекта хранилища.
 */
Storage defaultStorage();

}  // namespace csdb

#endif // _CREDITS_CSDB_H_INCLUDED_
