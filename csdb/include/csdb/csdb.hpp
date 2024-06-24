/**
 * @file csdb.h
 * @author Evgeny V. Zalivochkin
 */

#ifndef _CREDITS_CSDB_H_INCLUDED_
#define _CREDITS_CSDB_H_INCLUDED_

#include <csdb/storage.hpp>

namespace csdb {
/* The global functions set is necessary for the compatibility with previous csdb version,
 * and for working with the single storage without creating \ref ::csdb::Storage class object.
 *
 * This set almost copies the method's det of \ref ::csdb::Storage, realizing the work with single
 * internal class object. The detailed description of methods is in the \ref ::csdb::Storage
 * class documentation.
 */

/**
 * @brief Initializing of inner storage object
 *
 * The method parameters are equivalent to those of \ref ::csdb::Storage::open, main discrepancy is
 * that, in the case when inner \ref ::csdb::Storage object is already opened, then method doesn't 
 * try to open the new Storage, but returns false immediately.
 *
 * @sa ::csdb::Storage::open
 */
bool init(const Storage::OpenOptions& options, Storage::OpenCallback callback = nullptr);

/**
 * @overload
 */
bool init(const char* path_to_bases, const Storage::OpenCallback& callback = nullptr);

/**
 * @brief 
 Check if the internal storage object is initialized
 * @sa ::csdb::Storage::isOpen
 */
bool isInitialized();

/**
 * @brief Closing of internal storage object
 * @sa ::csdb::Storage::close
 */
void done();

/**
 * @brief Last error code
 * @sa ::csdb::Storage::last_error()
 */
Storage::Error lastError();

/**
 * @brief Last error description
 * @sa ::csdb::Storage::last_error_message()
 */
::std::string lastErrorMessage();

/**
 * @brief Last database drivers work error code
 * @sa ::csdb::Storage::db_last_error()
 */
Database::Error dbLastError();

/**
 * @brief Last database drivers work error description
 * @sa ::csdb::Storage::db_last_error_message()
 */
::std::string dbLastErrorMessage();

/**
 * @brief Default storage
 * @return Internal object of storage
 */
Storage defaultStorage();

}  // namespace csdb

#endif // _CREDITS_CSDB_H_INCLUDED_
