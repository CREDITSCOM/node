#ifndef METASTORAGE_HPP
#define METASTORAGE_HPP

#include <boost/circular_buffer.hpp>
#include <lib/system/common.hpp>
#include <optional>

/// nested namespace
namespace cs::values {
constexpr std::size_t defaultMetaStorageMaxSize = 5;
}

namespace cs {
///
/// @brief Class for storage meta information with
/// common interface and dependency of round number.
///
template <typename T>
class MetaStorage {
public:
  ///
  /// @brief Represents meta element with storage
  /// of round and generated (instantiated) meta class.
  ///
  struct MetaElement {
    cs::RoundNumber round = 0;
    T meta;

    MetaElement() = default;
    MetaElement(cs::RoundNumber number, T&& value) noexcept
    : round(number)
    , meta(std::move(value)) {
    }

    MetaElement(cs::RoundNumber number, const T& value) noexcept
    : round(number)
    , meta(value) {
    }

    bool operator==(const MetaElement& element) const {
      return round == element.round;
    }

    bool operator!=(const MetaElement& element) const {
      return !((*this) == element);
    }
  };

  /// element type
  using Element = MetaStorage<T>::MetaElement;

  // default initialization
  inline MetaStorage() noexcept
  : buffer_(cs::values::defaultMetaStorageMaxSize) {
  }

  // storage interface

  ///
  /// @brief Resizes circular storage buffer.
  ///
  void resize(std::size_t size) {
    buffer_.resize(size);
  }

  ///
  /// @brief Returns current circular buffer size
  ///
  std::size_t size() const {
    return buffer_.size();
  }

  ///
  /// @brief Appends meta element to buffer.
  /// @param element Created from outside rvalue.
  /// @return Returns true if append is success, otherwise returns false.
  ///
  bool append(MetaElement&& value) {
    const auto iterator = std::find(buffer_.begin(), buffer_.end(), value);
    const auto result = (iterator == buffer_.end());

    if (result) {
      buffer_.push_back(std::move(value));
    }

    return result;
  }

  ///
  /// @brief Appends meta element to buffer.
  /// @return Returns true if append is success, otherwise returns false.
  ///
  bool append(const MetaElement& value) {
    MetaElement element = {
      value.round,
      value.meta
    };

    return append(std::move(element));
  }

  ///
  /// @brief Appends value with round key.
  /// @param round Current round to add as a key.
  /// @param value Movable value created from outside.
  /// @return Returns true if append is success, otherwise returns false.
  ///
  bool append(RoundNumber round, T&& value) {
    MetaElement element = {
      round, std::move(value)
    };

    return append(std::move(element));
  }

  ///
  /// @brief Returns contains meta storage this round or not.
  ///
  bool contains(RoundNumber round) {
    const auto iterator = std::find_if(buffer_.begin(), buffer_.end(), [=](const MetaElement& value) {
      return value.round == round;
    });

    return iterator != buffer_.end();
  }

  ///
  /// @brief Returns copy of meta element if this round exists at storage, otherwise returns nothing.
  /// @param round Round number to get element from storage.
  /// @return Returns optional parameter with T type.
  ///
  std::optional<T> value(RoundNumber round) const {
    const auto iterator = std::find_if(buffer_.begin(), buffer_.end(), [=](const MetaElement& value) {
      return value.round == round;
    });

    if (iterator != buffer_.end()) {
      return std::make_optional<T>(iterator->meta);
    }

    return std::nullopt;
  }

  ///
  /// @brief Returns and removes meta element if it's exists at round parameter.
  /// @param round Round number to get element from storage.
  /// @return Returns meta element of storage if found, otherwise returns nothing.
  ///.
  std::optional<T> extract(RoundNumber round) {
    const auto iterator = std::find_if(buffer_.begin(), buffer_.end(), [=](const MetaElement& value) {
      return value.round == round;
    });

    if (iterator == buffer_.end()) {
      return std::nullopt;
    }

    MetaElement result = std::move(*iterator);
    buffer_.erase(iterator);

    return std::make_optional<T>(std::move(result.meta));
  }

  ///
  /// @brief Returns pointer to existing element, otherwise returns nullptr.
  /// @param round Round of searching element.
  /// @return Reference to element.
  /// @warning Before using this methods use contains(round) to check element existing, or check pointer on nullptr.
  ///
  T* get(RoundNumber round) {
    const auto iterator = std::find_if(buffer_.begin(), buffer_.end(), [=](const MetaElement& value) {
      return round == value.round;
    });

    if (iterator == buffer_.end()) {
      return nullptr;
    }

    return &(iterator->meta);
  }

  ///
  /// @brief Returns reference to last round meta storage.
  /// @warning Use only if elements exists.
  ///
  const T& last() const {
    auto iterator = end();
    --iterator;

    return iterator->meta;
  }

  ///
  /// @brief Returns max round meta element.
  /// @warning Use only if elements exists.
  ///
  const T& max() const {
    const MetaElement* element = nullptr;
    cs::RoundNumber maxRound = 0;

    std::for_each(buffer_.begin(), buffer_.end(), [&](const MetaElement& value) {
      if (maxRound <= value.round) {
        maxRound = value.round;
        element = &value;
      }
    });

    return element->meta;
  }

  ///
  /// @brief Returns const begin interator of circular buffer.
  ///
  auto begin() const {
    return buffer_.begin();
  }

  ///
  /// @brief Returns const end interator of circular buffer.
  ///
  auto end() const {
    return buffer_.end();
  }

private:
  boost::circular_buffer<MetaElement> buffer_;
};
}  // namespace cs

#endif  // METASTORAGE_HPP
