#ifndef METASTORAGE_HPP
#define METASTORAGE_HPP

#include <optional>
#include <lib/system/common.hpp>
#include <boost/circular_buffer.hpp>

/// nested namespace
namespace cs::values
{
    constexpr std::size_t defaultMetaStorageSize = 5;
}

namespace cs
{
    ///
    /// @brief Class for storage meta information with
    /// common interface and dependency of round number.
    ///
    template<typename T>
    class MetaStorage
    {
    public:
        ///
        /// @brief Represents meta element with storage
        /// of round and generated (instantiated) meta class.
        ///
        struct MetaElement
        {
            RoundNumber round;
            T meta;

            MetaElement() = default;
            MetaElement(RoundNumber number, T&& value) noexcept : round(number), meta(std::move(value)) {}

            bool operator ==(const MetaElement& element) const
            {
                return round == element.round;
            }

            bool operator !=(const MetaElement& element) const
            {
                return !((*this) == element);
            }
        };

        /// element type
        using Element = MetaStorage<T>::MetaElement;

        // default initialization
        inline MetaStorage() noexcept
        {
            m_buffer.resize(cs::values::defaultMetaStorageSize);
        }

        // storage interface

        ///
        /// @brief Resizes circular storage buffer.
        ///
        void resize(std::size_t size)
        {
            m_buffer.resize(size);
        }

        ///
        /// @brief Returns current circular buffer size
        ///
        std::size_t size() const
        {
            return m_buffer.size();
        }

        ///
        /// @brief Appends meta element to buffer.
        /// @param element Created from outside rvalue.
        /// @return Returns true if append is success, otherwise returns false.
        ///
        bool append(MetaElement&& value)
        {
            const auto iterator = std::find(m_buffer.begin(), m_buffer.end(), value);
            const auto result = (iterator == m_buffer.end());

            if (result) {
                m_buffer.push_back(std::move(value));
            }

            return result;
        }

        ///
        /// @brief Appends meta element to buffer.
        /// @param element Created from outside no needed lvalue, cuz it would be moved.
        /// @return Returns true if append is success, otherwise returns false.
        /// @warning value would be moved.
        ///
        bool append(MetaElement& value)
        {
            return append(std::move(value));
        }

        ///
        /// @brief Appends value with round key.
        /// @param round Current round to add as a key.
        /// @param value Movable value created from outside.
        /// @return Returns true if append is success, otherwise returns false.
        ///
        bool append(RoundNumber round, T&& value)
        {
            MetaElement element = {
                round,
                std::move(value)
            };

            return append(std::move(element));
        }

        ///
        /// @brief Returns contains meta storage this round or not.
        ///
        bool contains(RoundNumber round)
        {
            const auto iterator = std::find_if(m_buffer.begin(), m_buffer.end(), [=](const MetaElement& value) {
                return value.round == round;
            });

            return iterator != m_buffer.end();
        }

        ///
        /// @brief Returns copy of meta element if this round exists at storage, otherwise returns nothing.
        /// @param round Round number to get element from storage.
        /// @return Returns optional parameter with T type.
        ///
        std::optional<T> value(RoundNumber round) const
        {
            const auto iterator = std::find_if(m_buffer.begin(), m_buffer.end(), [=](const MetaElement& value) {
                return value.round == round;
            });

            if (iterator != m_buffer.end()) {
                return std::make_optional<T>(iterator->meta);
            }

            return std::nullopt;
        }

        ///
        /// @brief Returns and removes meta element if it's exists at round parameter.
        /// @param round Round number to get element from storage.
        /// @return Returns meta element of storage if found, otherwise default constructed meta element.
        ///.
        std::optional<T> extract(RoundNumber round)
        {
            const auto iterator = std::find_if(m_buffer.begin(), m_buffer.end(), [=](const MetaElement& value) {
                return value.round == round;
            });

            if (iterator == m_buffer.end()) {
                return std::nullopt;
            }

            MetaElement result = std::move(*iterator);
            m_buffer.erase(iterator);

            return std::make_optional<T>(std::move(result.meta));
        }

        ///
        /// @brief Returns reference to existing element, otherwise returns nullptr.
        /// @param round Round of searching element.
        /// @return Reference to element.
        /// @warning Before using this methods use contains(round) to check element existing, or check pointer on nullptr
        ///
        T* get(RoundNumber round)
        {
            const auto iterator = std::find_if(m_buffer.begin(), m_buffer.end(), [=](const MetaElement& value) {
                return round == value.round;
            });

            if (iterator == m_buffer.end()) {
                return nullptr;
            }

            return &(iterator->meta);
        }

    private:
        boost::circular_buffer<MetaElement> m_buffer;
    };
}

#endif  // METASTORAGE_HPP
