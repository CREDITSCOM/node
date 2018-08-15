/**
  * @file transaction.h
  * @author Evgeny V. Zalivochkin
  */

#pragma once
#ifndef _CREDITS_CSDB_TRANSACTION_H_INCLUDED_
#define _CREDITS_CSDB_TRANSACTION_H_INCLUDED_

#include <set>

#include "csdb/user_field.h"

#include "csdb/internal/shared_data.h"
#include "csdb/internal/types.h"


namespace csdb {

namespace priv {
class obstream;
class ibstream;
} // namespace priv

class Address;
class Amount;
class Currency;
class PoolHash;
class Pool;

/**
 * @brief Уникальный идетификатор транзакции в базе
 *
 * Класс позволяет однозначно идентифицировать транзакцию в базе. Транзакция получает этот
 * идентификатор только после того, как она была помещена в пул, и пул был записан в базу.
 *
 * Конкретретное содержимое этого класса не специфицируется. Для получения идентификатора
 * используется метод \ref Transaction::id, а также предоставляются методы преобразования
 * в строку (\ref TransactionID::to_string) и получения из строки (\ref TransactionID::from_string).
 *
 * Конкретный формат строки также не специфицируется, однако класс гарантирует, что любая
 * строка, полученная с помощью метода \ref TransactionID::to_string, будет преобразована
 * к валидному идентификатору с помощью метода \ref TransactionID::from_string.
 *
 * Единственное специфицируемая часть содержимого класса - это возможность получить
 * \ref PoolHash для пула, в который помещена транзакция.
 */
class TransactionID
{
  SHARED_DATA_CLASS_DECLARE(TransactionID)
public:
  /// \deprecated Тип будет удалён в следующих версиях.
  using sequence_t = size_t;

  /// \deprecated Конструктор будет удалён в следующих версиях.
  TransactionID(PoolHash poolHash, sequence_t index);

  bool is_valid() const noexcept;
  PoolHash pool_hash() const noexcept;

  /// \deprecated Метод будет удалён в следующих версиях.
  sequence_t index() const noexcept;

  std::string to_string() const noexcept;

  /**
   * @brief Получение идентификатора транзакции из строкового представления
   * @param[in] str Строковое представление идентификатора транзакции
   * @return Идентификатор транзакции, полученный из строкового представления
   *
   * В случае, если строковое представление не может быть декодировано как
   * идетификатор транзакции, возвращаемый идентификатор невалидны
   * (\ref is_valid() возвращает false)
   */
  static TransactionID from_string(const ::std::string& str);

  bool operator ==(const TransactionID &other) const noexcept;
  bool operator !=(const TransactionID &other) const noexcept;
  bool operator < (const TransactionID &other) const noexcept;

private:
  void put(::csdb::priv::obstream&) const;
  bool get(::csdb::priv::ibstream&);
  friend class ::csdb::priv::obstream;
  friend class ::csdb::priv::ibstream;
  friend class Transaction;
  friend class Pool;
};

class Transaction
{
  SHARED_DATA_CLASS_DECLARE(Transaction)

public:
  Transaction(int64_t innerID, Address source, Address target, Currency currency, Amount amount, Amount comission, std::string signature);
  Transaction(int64_t innerID, Address source, Address target, Currency currency, Amount amount, Amount comission, std::string signature, Amount balance);

  bool is_valid() const noexcept;
  bool is_read_only() const noexcept;

  TransactionID id() const noexcept;
  int64_t innerID() const noexcept;
  Address source() const noexcept;
  Address target() const noexcept;
  Currency currency() const noexcept;
  Amount amount() const noexcept;
  Amount comission() const noexcept;
  std::string signature() const noexcept;
  Amount balance() const noexcept;

  void set_innerID(int64_t innerID);
  void set_source(Address source);
  void set_target(Address target);
  void set_currency(Currency currency);
  void set_amount(Amount amount);
  void set_comission(Amount comission);
  void set_signature(std::string signature);
  void set_balance(Amount balance);

  ::csdb::internal::byte_array to_binary();
  static Transaction from_binary(const ::csdb::internal::byte_array data);

  static Transaction from_byte_stream(const char* data, size_t m_size);
  std::vector<uint8_t> to_byte_stream() const;
  std::vector<uint8_t> to_byte_stream_for_sig() const;

  /**
   * @brief Добавляет дополнительное произвольное поле к транзакции
   * @param[in] id    Идентификатор дополнительного поля
   * @param[in] field Значение дополнительного поля
   * @return true, если поле добавлено, false в противном случае
   *
   * Поле добавляется только для транзакций, не находящихся в режим Read-Only
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

private:
  void put(::csdb::priv::obstream&) const;
  bool get(::csdb::priv::ibstream&);
  friend class ::csdb::priv::obstream;
  friend class ::csdb::priv::ibstream;
  friend class Pool;
};

} // namespace csdb

#endif // _CREDITS_CSDB_TRANSACTION_H_INCLUDED_
