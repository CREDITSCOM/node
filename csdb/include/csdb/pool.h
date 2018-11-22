/**
 * @file pool.h
 * @author Evgeny V. Zalivochkin
 */

#ifndef _CREDITS_CSDB_POOL_H_INCLUDED_
#define _CREDITS_CSDB_POOL_H_INCLUDED_

#include <array>
#include <cinttypes>
#include <climits>
#include <string>
#include <vector>

#include "csdb/address.h"
#include "csdb/internal/shared_data.h"
#include "csdb/internal/types.h"
#include "csdb/storage.h"
#include "csdb/transaction.h"
#include "csdb/user_field.h"

#include <lib/system/common.hpp>

namespace csdb {

class Transaction;
class TransactionID;

namespace priv {
class obstream;
class ibstream;
}  // namespace priv

class PoolHash {
  SHARED_DATA_CLASS_DECLARE(PoolHash)

public:
  bool is_empty() const noexcept;
  size_t size() const noexcept;
  std::string to_string() const noexcept;

  /**
   * @brief Получение хэша из строкового представления
   * @param[in] str Строковое представление хэша
   * @return Хэш, полученный из строкового представления.
   *
   * В случае, если строковое представление неверное, возвращается пустой хэш.
   */
  static PoolHash from_string(const ::std::string& str);

  ::csdb::internal::byte_array to_binary() const noexcept;
  static PoolHash from_binary(const ::csdb::internal::byte_array& data);

  bool operator==(const PoolHash& other) const noexcept;
  inline bool operator!=(const PoolHash& other) const noexcept;

  /**
   * @brief operator <
   *
   * Оператор предназначен для возможности сортировок контейнеров класса или
   * использования класса в качестве ключа.
   */
  bool operator<(const PoolHash& other) const noexcept;

  static PoolHash calc_from_data(const internal::byte_array& data);

private:
  void put(::csdb::priv::obstream&) const;
  bool get(::csdb::priv::ibstream&);
  friend class ::csdb::priv::obstream;
  friend class ::csdb::priv::ibstream;
  friend class Storage;
};

class Pool {
  SHARED_DATA_CLASS_DECLARE(Pool)
public:
  using sequence_t = uint64_t;
  using Transactions = std::vector<csdb::Transaction>;
  class NewWalletInfo {
  public:
    using WalletId = csdb::internal::WalletId;

    enum AddressType {
      AddressIsSource,
      AddressIsTarget
    };

    struct AddressId {
      size_t trxInd_ : sizeof(size_t) * CHAR_BIT - 1;
      size_t addressType_ : 1;

      bool operator==(const AddressId& rh) const {
        return trxInd_ == rh.trxInd_ && addressType_ == rh.addressType_;
      }
      bool operator!=(const AddressId& rh) const {
        return !operator==(rh);
      }
    };

  public:
    NewWalletInfo()
    : addressId_()
    , walletId_() {
    }
    NewWalletInfo(AddressId addressId, csdb::internal::WalletId walletId)
    : addressId_(addressId)
    , walletId_(walletId) {
    }
    void put(::csdb::priv::obstream&) const;
    bool get(::csdb::priv::ibstream&);

    bool operator==(const NewWalletInfo& rh) const {
      return addressId_ == rh.addressId_ && walletId_ == rh.walletId_;
    }
    bool operator!=(const NewWalletInfo& rh) const {
      return !operator==(rh);
    }

  public:
    AddressId addressId_;
    WalletId walletId_;
  };
  using NewWallets = std::vector<NewWalletInfo>;

public:
  Pool(PoolHash previous_hash, sequence_t sequence, Storage storage = Storage());

  static Pool from_binary(const ::csdb::internal::byte_array& data);
  static Pool meta_from_binary(const ::csdb::internal::byte_array& data, size_t& cnt);
  static Pool load(PoolHash hash, Storage storage = Storage());

  static Pool from_byte_stream(const char* data, size_t size);
  char* to_byte_stream(uint32_t&);
  ::csdb::internal::byte_array to_byte_stream_for_sig();

  Pool meta_from_byte_stream(const char*, size_t);
  static Pool from_lz4_byte_stream(size_t);

  bool is_valid() const noexcept;
  bool is_read_only() const noexcept;
  PoolHash previous_hash() const noexcept;
  sequence_t sequence() const noexcept;
  Storage storage() const noexcept;
  size_t transactions_count() const noexcept;
  std::vector<uint8_t> writer_public_key() const noexcept;
  std::string signature() const noexcept;
  const ::std::vector<::std::vector<uint8_t>>& confidants() const noexcept;
  const ::std::vector<std::pair<int, ::std::string>>& signatures() const noexcept;

  void set_previous_hash(PoolHash previous_hash) noexcept;
  void set_sequence(sequence_t sequence) noexcept;
  void set_storage(Storage storage) noexcept;
  void set_writer_public_key(std::vector<uint8_t> writer_public_key) noexcept;
  void set_signature(const std::string& signature) noexcept;
  void set_confidants(std::vector<::std::vector<uint8_t>>& confidants) noexcept;
  void add_signature(int index, ::std::string& signature) noexcept;

  Transactions& transactions();
  const Transactions& transactions() const;

  NewWallets* newWallets() noexcept;
  const NewWallets& newWallets() const noexcept;
  bool getWalletAddress(const NewWalletInfo& info, csdb::Address& wallAddress) const;

  /**
   * @brief Добавляет транзакцию в пул.
   * @param[in] transaction Транзакция для добавления
   * @return true, если транзакция была успешно добавлена. false, если транзакция не прошла
   * проверку.
   *
   * Добаление возможно только во вновь создаваемый пул (т.е. если \ref is_read_only возвращает false).
   *
   * Перед добавлением транзакция проходит проверку на валидность по базе данных, указанной для
   * пула, и по ранее добавленным транзакциям. Если база данных не задана, или она была закрыта,
   * проверка считается неуспешной.
   */
  bool add_transaction(Transaction transaction
#ifdef CSDB_UNIT_TEST
                       ,
                       bool skip_check
#endif
  );

  /**
   * @brief Закончить формирование пула.
   * @return true, если для пула успешно сформировано бинарное представление.
   *
   * Для вновь создаваемого пула (т.е. если \ref is_read_only возвращает false) метод формирует
   * его бинарное представлени, вычисляет хэш и переводит пул в состояние read-only. После этого
   * для пула становятся доступными функции \ref hash, \ref save и \ref to_binary.
   *
   * Для read-only пулов функция не делает ничего и просто возвращает true.
   */
  bool compose();

  /**
   * @brief Хеш пула
   * @return Хеш пула, если пул находится в режиме read-only, и пустой хеш в противном
   *         случае.
   */
  PoolHash hash() const noexcept;
  void recount() noexcept;

  /**
   * @brief Бинарное представление пула
   * @return Бинарное представление пула, если пул находится в режиме read-only, и пустой
   *         массив в противном случае.
   */
  ::csdb::internal::byte_array to_binary() const noexcept;

  /**
   * @brief Сохранение пула в хранилище.
   * @param[in] storage Хранилище, в котором нужно сохранить пул.
   * @return  true, если сохранение прошло успешно.
   *
   * Функция работает только для сформированных пулов (т.е. находящихся в режиме read-only).
   *
   * Если переданное хранилище не доступно (не открыто), то функция пытается сохранить пул в том
   * хранилище, которое было передано ему при создании. Если и это хранилище недоступно, функци
   * пытается использовать хранилище по умолчанию. Если ни одно из перечисленных хранилищ не доступно,
   * возвращается false.
   *
   * Функция не проверяет, есть ли уже пул с таким же хэшем в хранилище, т.к. вероятность совпадения
   * хешей у двух разных пулов практически нулевая.
   *
   * Если сохранение прошло успешно, то хранилище, в которое произошло сохранение, становится хранилищем
   * для данного экземпляра объекта (поэтому метод не константный).
   */
  bool save(Storage storage = Storage());

  /**
   * @brief Добавляет дополнительное произвольное поле к пулу
   * @param[in] id    Идентификатор дополнительного поля
   * @param[in] field Значение дополнительного поля
   * @return true, если поле добавлено, false в противном случае
   *
   * Поле добавляется только для пулов, не находящихся в режим Read-Only
   * (\ref is_read_only возвращает false).
   *
   * Если поле с таким идентификатором было добавлено ранее, они замещается новым.
   */
  bool add_user_field(user_field_id_t id, UserField field) noexcept;

  /**
   * @brief Возвращает дополнительное поле.
   * @param[in] id  Идентификатор дополнительного поля
   * @return  Значение дополнительного поля. Если поля с таким идентификатором нет в списке
   *          дополнительных полей, возвращается невалидный объект
   *          (\ref UserField::is_valid == false).
   */
  UserField user_field(user_field_id_t id) const noexcept;

  /**
   * @brief Список идентификаторов дополнительных полей
   * @return  Список идентификаторов дополнительных полей
   */
  ::std::set<user_field_id_t> user_field_ids() const noexcept;

  /// \deprecated Функция будет исключена в последующих версиях.
  Transaction transaction(size_t index ) const;

  /**
   * @brief Получить транзакцию по идентификатору
   * @param[in] id Идентификатор транзакции
   * @return Возвращает объект транзакции. Если транзакции не существует в данном пуле, возвращается
   *         невалидный объект (\ref ::csdb::Transaction::is_valid() == false).
   */
  Transaction transaction(TransactionID id) const;

  /**
  * @brief Получить последнюю транзакцию по адресу источника
  * @param[in] source Адрес источника
  * @return Возвращает объект транзакции. Если транзакции не существует в данном пуле, возвращается
  *         невалидный объект (\ref ::csdb::Transaction::is_valid() == false).
  */
  Transaction get_last_by_source(Address source) const noexcept;

  /**
  * @brief Получить последнюю транзакцию по адресу назначения
  * @param[in] source Адрес назначения
  * @return Возвращает объект транзакции. Если транзакции не существует в данном пуле, возвращается
  *         невалидный объект (\ref ::csdb::Transaction::is_valid() == false).
  */
  Transaction get_last_by_target(Address target) const noexcept;

  void sign(const cs::PrivateKey& private_key);
  bool verify_signature();
  bool verify_signature(const std::string& signature);

  friend class Storage;
};

inline bool PoolHash::operator !=(const PoolHash &other) const noexcept
{
  return !operator ==(other);
}
}  // namespace csdb

#endif // _CREDITS_CSDB_POOL_H_INCLUDED_
