#include "../include/csnode/dynamicbuffer.h"
#include <algorithm>

cs::DynamicBuffer::DynamicBuffer(size_t size):
    mSize(size)
{
    mArray = new char[size];
}

cs::DynamicBuffer::DynamicBuffer(const char* data, std::size_t size):
    mSize(size)
{
    mArray = new char[size];

    std::copy(data, data + size, mArray);
}

cs::DynamicBuffer::DynamicBuffer(const unsigned char* data, std::size_t size):
    DynamicBuffer(reinterpret_cast<const char*>(data), size)
{
}

cs::DynamicBuffer::DynamicBuffer(const cs::DynamicBuffer& buffer):
    mSize(buffer.mSize)
{
    mArray = new char[mSize];

    std::copy(buffer.mArray, buffer.mArray + buffer.mSize, mArray);
}

cs::DynamicBuffer::DynamicBuffer(cs::DynamicBuffer&& buffer):
    mArray(buffer.mArray),
    mSize(buffer.mSize)
{
    buffer.mArray = nullptr;
    buffer.mSize = 0;
}

cs::DynamicBuffer& cs::DynamicBuffer::operator=(const cs::DynamicBuffer& buffer)
{
    if (this != &buffer)
    {
        if (mArray)
            delete[] mArray;

        mSize = buffer.mSize;
        mArray = new char[mSize];

        std::copy(buffer.mArray, buffer.mArray + buffer.mSize, mArray);
    }

    return *this;
}

cs::DynamicBuffer& cs::DynamicBuffer::operator=(cs::DynamicBuffer&& buffer)
{
    if (this != &buffer)
    {
        if (mArray)
            delete[] mArray;

        mSize = buffer.mSize;
        mArray = buffer.mArray;

        buffer.mSize = 0;
        buffer.mArray = nullptr;
    }

    return *this;
}

cs::DynamicBuffer::~DynamicBuffer()
{
    if (mArray)
    {
        delete[] mArray;

        mArray = nullptr;
        mSize = 0;
    }
}

char& cs::DynamicBuffer::operator[](std::size_t index)
{
    return const_cast<char&>(static_cast<const DynamicBuffer*>(this)->operator[](index));
}

const char& cs::DynamicBuffer::operator[](std::size_t index) const
{
    return *(mArray + index);
}

char* cs::DynamicBuffer::get() const
{
    return mArray;
}

char* cs::DynamicBuffer::operator*() const
{
    return this->get();
}

size_t cs::DynamicBuffer::size() const
{
    return mSize;
}

char* cs::DynamicBuffer::begin()
{
    return mArray;
}

char* cs::DynamicBuffer::end()
{
    return  mArray + mSize;
}

const char* cs::DynamicBuffer::begin() const
{
    return  mArray;
}

const char* cs::DynamicBuffer::end() const
{
    return mArray + mSize;
}

bool cs::operator==(const cs::DynamicBuffer& lhs, const cs::DynamicBuffer& rhs)
{
    if (lhs.size() != rhs.size())
        return  false;

    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if (lhs[i] != rhs[i])
            return  false;
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
        std::swap(lhs.mSize, rhs.mSize);
        std::swap(lhs.mArray, rhs.mArray);
    }
}
