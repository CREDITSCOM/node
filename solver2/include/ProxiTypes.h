#pragma once
#include <cstdint>

class Vector
{
public:

    Vector() = default;
};

class Matrix
{
public:

    Matrix() = default;
};

class RoundTable
{
public:

    RoundTable() = default;
    RoundTable(const RoundTable& rhs) = default;
    RoundTable& operator=(const RoundTable& src) = default;

    explicit RoundTable(uint32_t num)
        : m_num(num)
    {
    }

    uint32_t RoundNumber() const
    {
        return m_num;
    }

    bool isValid() const
    {
        return m_num > 0;
    }

private:

    uint32_t m_num { 0 };
};

class Block
{
public:

    Block() = default;
    Block(const Block& rhs) = default;
    Block& operator=(const Block& src) = default;

    explicit Block(uint32_t num)
        : m_num(num)
    {
    }

    uint32_t SequenceNumber() const
    {
        return m_num;
    }

    bool isValid() const
    {
        return m_num > 0;
    }

private:

    uint32_t m_num { 0 };
};
