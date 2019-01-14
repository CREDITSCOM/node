/**
 * @file storage.h
 * @author Roman Bukin, Evgeny Zalivochkin
 */

#ifndef _CREDITS_CSDB_STORAGE_H_INCLUDED_
#define _CREDITS_CSDB_STORAGE_H_INCLUDED_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "csdb/database.hpp"
#include "csdb/transaction.hpp"

#include <lib/system/common.hpp>
#include <lib/system/signals.hpp>

namespace csdb {

class Pool;
class PoolHash;
class Address;
class Wallet;
class Transaction;
class TransactionID;

/** @brief The read block signal, caller may assign test_failed to true if block is logically corrupted */
using ReadBlockSignal = cs::Signal<void(const csdb::Pool block, bool* test_failed)>;

/**
 * @brief Объект хранилища.
 *
 * Объект хранилища является классом, через который осуществляется дальнейшая работа с данными
 * в хранилище (чтение, запись и т.п.). Объект по сути является "разделяемым указателем" (shared_ptr),
 * поэтому может свододно копироваться, однако фактически копии ссылаются на один и то же экземпляр,
 * и изменения, произведённые в одном из экземпляров, отразятся во всех копиях.
 *
 * Для работы с физическим хранилищем используется интерфейсный класс \ref ::csdb::Database.
 *
 * В данный момент имеется единственная реализация интерфейса \ref ::csdb::Database -
 * \ref ::csdb::DatabaseLevelDB. Она же используется при открытии объекта \ref ::csdb::Storage.
 *
 * При создании объект \ref ::csdb::Storage имеет статус "не открытого", вызовы любых методов получени
 * или записи данных порождают ошибку ::csdb::Storage::NotOpen. Для работы с объектом \ref ::csdb::Storage
 * необходимо вызывать функцию \ref ::csdb::Storage::open или создать объект с помощью функции
 * ::csdb::Storage::get.
 */
class Storage final {
private:
  class priv;
  bool write_queue_search(const PoolHash &hash, Pool &res_pool) const;
  bool write_queue_pop(Pool &res_pool);

public:
  using WeakPtr = ::std::weak_ptr<priv>;

  enum Error
  {
    NoError = 0,
    NotOpen = 1,
    DatabaseError = 2,
    ChainError = 3,
    DataIntegrityError = 4,
    UserCancelled = 5,
    InvalidParameter = 6,
    UnknownError = 255,
  };

public:
  Error last_error() const;
  std::string last_error_message() const;
  Database::Error db_last_error() const;
  std::string db_last_error_message() const;

public:
  Storage();
  ~Storage();

public:
  inline Storage(const Storage &) noexcept = default;
  inline Storage(Storage &&) noexcept = default;
  inline Storage &operator=(const Storage &) noexcept = default;
  inline Storage &operator=(Storage &&) noexcept = default;

  explicit Storage(WeakPtr ptr) noexcept;
  WeakPtr weak_ptr() const noexcept;

public:
  struct OpenOptions {
    /// Экземпляр драйвера базы данных
    ::std::shared_ptr<Database> db;
  };

  struct OpenProgress {
    uint64_t poolsProcessed;
  };

  /**
   * @brief Callback для операции открытия
   * @return true, если операцию необходимо прервать.
   */
  typedef ::std::function<bool(const OpenProgress &)> OpenCallback;

  ReadBlockSignal* read_block_event() const;

  /**
   * @brief Открывает хранилище по набору параметров.
   * @param opt       Набор параметров для открытия. Член стурктуры \ref OpenOptions::db должен
   *                  указывать на уже открытую базу.
   * @param callback  Функция обратного вызова для процедуры открытия
   * @return          true, если открытие и анализ прошли успешно. В противном случае false.
   *
   * В случае неудачи информацию об ошибке можно получить с помошью методов \ref last_error,
   * \ref last_error_message, \ref db_last_error() и \ref db_last_error_message()
   */
  bool open(const OpenOptions &opt, OpenCallback callback = nullptr);

  /**
   * @brief Открывает хранилище по пути к хранилищу
   * @param path_to_base  Путь к базе данных (слеш в конце необязателен)
   * @param callback      Функция обратного вызова для процедуры открытия
   * @return              true, если открытие и анализ прошли успешно. В противном случае false.
   * @overload
   *
   * Метод пытается открыть существующее (или создать новое) хранилище с драйвером базы
   * данных, определённым для текущей платформы. Если указанный путь не существует, метод пытается
   * создать указанный путь. Если передан пустой путь, то используется путь по умолчанию для
   * текущей платформы.
   *
   * В случае неудачи информацию об ошибке можно получить с помошью методов \ref last_error,
   * \ref last_error_message, \ref db_last_error() и \ref db_last_error_message()
   */
  bool open(const ::std::string &path_to_base = ::std::string{}, OpenCallback callback = nullptr);

  /**
   * @brief Создание хранилища по набору параметров.
   *
   * Создаёт объект хранилища и пытается его открыть. См. \ref open(const OpenOptions &opt, OpenCallback callback);
   */
  static inline Storage get(const OpenOptions &opt, OpenCallback callback = nullptr);

  /**
   * @brief Создание хранилища по пути к хранилищу.
   *
   * Создаёт объект хранилища и пытается его открыть.
   * См. \ref open(const ::std::string& path_to_base, OpenCallback callback);
   */
  static inline Storage get(const ::std::string &path_to_base = ::std::string{}, OpenCallback callback = nullptr);

  /**
   * @brief Проверяет, открыто ли хранилище.
   * @return true, если хранилище открыто и с ним можно работать. Иначе false.
   */
  bool isOpen() const;

  /**
   * @brief Закрывает хранилище
   *
   * После вызова этого метода обращение к любым методам получения или записи данных приводят
   * к ошибке \ref NotOpen.
   */
  void close();

  /**
   * @brief Хэш последнего блока
   * @return Хэш последнего блока
   *
   * Хеш последнего блока возвращается корректно только в том случае, если блоки,
   * помещённые в хранилище (в том числе и после открытия), образуют корректную
   * связанную цепочку.
   *
   * Если хранилище пустое, или не содержит законченной цепочки, возвращается пустой хэш.
   */
  PoolHash last_hash() const noexcept;
  void set_last_hash(const PoolHash&) noexcept;
  void set_size(const size_t) noexcept;

  /**
   * @brief Записавает пул в хранилище
   * @param[in] pool Пул для записи в хранилище.
   * @return true, если пул успешно записан.
   *
   * \sa ::csdb::Pool::save
   */
  bool pool_save(Pool pool);

  /**
   * @brief Загружает пул из хранилища
   * @param[in] hash Хэш пула, который надо загрузить.
   * @return Загруженный пул. Если пул не найден или данные из хранилища не могут быть
   *         интерпретированы, как пул, возвращается невалидный пул.
   *         (\ref ::csdb::Pool::is_valid() == false).
   *
   * \sa ::csdb::Pool::load
   */
  Pool pool_load(const PoolHash &hash) const;
  Pool pool_load(const cs::Sequence sequence) const;
  Pool pool_load_meta(const PoolHash &hash, size_t &cnt) const;

  Pool pool_remove_last();

  /**
   * @brief Получение транзакции по идентификатору.
   * @param[in] id Идентификатор транзакции
   * @return Объект транзакции. Если тразакции с таким идентификаторм отсутствует в хранилище,
   *         возвращается невалидный объект (\ref ::csdb::Transaction::is_valid() == false).
   */
  Transaction transaction(const TransactionID &id) const;

  /**
   * @brief Получить последнюю транзакцию по адресу источника
   * @param[in] source Адрес источника
   * @return Объект транзакции. Если транзакции не существует в данном пуле, возвращается
   *         невалидный объект (\ref ::csdb::Transaction::is_valid() == false).
   */
  Transaction get_last_by_source(Address source) const noexcept;

  /**
  * @brief Получить последнюю транзакцию по адресу назначения
  * @param[in] source Адрес назначения
  * @return Объект транзакции. Если транзакции не существует в данном пуле, возвращается
  *         невалидный объект (\ref ::csdb::Transaction::is_valid() == false).
  */
  Transaction get_last_by_target(Address target) const noexcept;

  // And now for something completely different
  PoolHash get_previous_transaction_block(const Address&, const PoolHash&);
  void set_previous_transaction_block(const Address&, const PoolHash& currTransBlock, const PoolHash& prevTransBlock);

  /**
   * @brief size возвращает количество пулов в хранилище
   * @return количество блоков в хранилище
   *
   * \deprecated Функция будет удалена в последующих версиях.
   */
  size_t size() const noexcept;

  /**
   * @brief wallet получить кошелек для указанного адреса
   * Кошелек содержит все данные для расчета баланса и проведению транзакций для
   * указанного адреса
   * @param addr адрес кошелька
   * @return кошелек
   */
  Wallet wallet(const Address &addr) const;

  /**
   * @brief transactions получить список транзакций для указанного адреса
   * @param addr адрес кошелька
   * @param limit маскимальное число транзакций в списке
   * @param offset идентификатор транзакции после которой начинать формирование списка
   * @return список транзакций
   *
   * \deprecated Функция будет удалена в последующих версиях.
   */
  std::vector<Transaction> transactions(const Address &addr, size_t limit = 100, const TransactionID &offset = TransactionID()) const;

  /**
  * @brief get_from_blockchain возвращает true, если транзакция с addr и innerId есть в blockchain
  * @param addr       адрес кошелька (input)
  * @param InnerId    id транзакции (input)
  * @Transaction trx  полученная транзакция (output)
  * @return содержит ли blockchain транзакцию
  *
  * \параметр addr должен точно совпадать с полем source у транзакции в блокчейне (если addr - id, source должен быть также id)
  * \используется для входного параметра addr в виде id кошелька
  */
  bool get_from_blockchain(const Address &addr /*input*/, const int64_t &InnerId /*input*/, Transaction &trx /*output*/) const;

private:
  static internal::byte_array get_trans_index_key(const Address&, const PoolHash&);
  Pool pool_load_internal(const PoolHash &hash, const bool metaOnly, size_t& trxCnt) const;

  ::std::shared_ptr<priv> d;
};

inline Storage Storage::get(const OpenOptions &opt, OpenCallback callback)
{
  Storage s;
  s.open(opt, callback);
  return s;
}

inline Storage Storage::get(const std::string& path_to_base, OpenCallback callback)
{
  Storage s;
  s.open(path_to_base, callback);
  return s;
}

}  // namespace csdb

#endif // _CREDITS_CSDB_STORAGE_H_INCLUDED_
