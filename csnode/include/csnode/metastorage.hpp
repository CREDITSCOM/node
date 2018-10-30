#ifndef METASTORAGE_HPP
#define METASTORAGE_HPP

#include <lib/system/common.hpp>
#include <boost/circular_buffer.hpp>

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

            bool operator ==(const MetaElement& element)
            {
                return round == element.round;
            }

            bool operator !=(const MetaElement& element)
            {
                return !((*this) == element);
            }
        };

        // storage interface

        ///
        /// @brief Resizes circular storage buffer.
        ///
        void resize(std::size_t size)
        {
            m_buffer.resize(size);
        }

    private:
        boost::circular_buffer<MetaElement> m_buffer;
    };
}

#endif  // METASTORAGE_HPP
