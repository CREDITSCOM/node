#include "../include/csnode/dynamicbuffer.h"
#include <algorithm>

cs::DynamicBuffer::DynamicBuffer(size_t size):
    m_size(size)
{
    m_array = new char[size];
}

cs::DynamicBuffer::DynamicBuffer(const char* data, std::size_t size):
    m_size(size)
{
    m_array = new char[size];

    std::copy(data, data + size, m_array);
}

cs::DynamicBuffer::DynamicBuffer(const unsigned char* data, std::size_t size):
    DynamicBuffer(reinterpret_cast<const char*>(data), size)
{
}

cs::DynamicBuffer::DynamicBuffer(const cs::DynamicBuffer& buffer):
    m_size(buffer.m_size)
{
    m_array = new char[m_size];

    std::copy(buffer.m_array, buffer.m_array + buffer.m_size, m_array);
}

cs::DynamicBuffer::DynamicBuffer(cs::DynamicBuffer&& buffer):
    m_array(buffer.m_array),
    m_size(buffer.m_size)
{
    buffer.m_array = nullptr;
    buffer.m_size = 0;
}

cs::DynamicBuffer& cs::DynamicBuffer::operator=(const cs::DynamicBuffer& buffer)
{
    if (this != &buffer)
    {
        if (m_array) {
            delete[] m_array;
        }

        m_size = buffer.m_size;
        m_array = new char[m_size];

        std::copy(buffer.m_array, buffer.m_array + buffer.m_size, m_array);
    }

    return *this;
}

cs::DynamicBuffer& cs::DynamicBuffer::operator=(cs::DynamicBuffer&& buffer)
{
    if (this != &buffer)
    {
        if (m_array) {
            delete[] m_array;
        }

        m_size = buffer.m_size;
        m_array = buffer.m_array;

        buffer.m_size = 0;
        buffer.m_array = nullptr;
    }

    return *this;
}

cs::DynamicBuffer::~DynamicBuffer()
{
    if (m_array)
    {
        delete[] m_array;

        m_array = nullptr;
        m_size = 0;
    }
}

char& cs::DynamicBuffer::operator[](std::size_t index)
{
    return const_cast<char&>(static_cast<const DynamicBuffer*>(this)->operator[](index));
}

const char& cs::DynamicBuffer::operator[](std::size_t index) const
{
    return *(m_array + index);
}

char* cs::DynamicBuffer::get() const
{
    return m_array;
}

char* cs::DynamicBuffer::operator*() const
{
    return this->get();
}

size_t cs::DynamicBuffer::size() const
{
    return m_size;
}

char* cs::DynamicBuffer::begin()
{
    return m_array;
}

char* cs::DynamicBuffer::end()
{
    return  m_array + m_size;
}

const char* cs::DynamicBuffer::begin() const
{
    return  m_array;
}

const char* cs::DynamicBuffer::end() const
{
    return m_array + m_size;
}

bool cs::operator==(const cs::DynamicBuffer& lhs, const cs::DynamicBuffer& rhs)
{
    if (lhs.size() != rhs.size()) {
        return  false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if (lhs[i] != rhs[i]) {
            return  false;
        }
    }

    return true;
}

bool cs::operator!=(const cs::DynamicBuffer& lhs, const cs::DynamicBuffer& rhs)
{
    return !(lhs == rhs);
}

void cs::swap(cs::DynamicBuffer& lhs, cs::DynamicBuffer& rhs)
{
    if (&lhs != &rhs)
    {
        std::swap(lhs.m_size, rhs.m_size);
        std::swap(lhs.m_array, rhs.m_array);
    }
}
