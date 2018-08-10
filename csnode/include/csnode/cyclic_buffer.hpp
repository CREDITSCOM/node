#ifndef CYCLIC_BUFFER_H
#define CYCLIC_BUFFER_H

template<typename T, size_t N>
class CyclicBuffer
{
public:
    CyclicBuffer()
        : size_(0)
        , back_(0)
    {}

    void push_back(const T& val)
    {
        back_ = (back_ + 1) % N;
        data_[back_] = val;
        ++size_;
    }

    T& back() { return data_[back_]; }
    const T& back() const { return data_[back_]; }

    void pop_back()
    {
        back_ = (back_ - 1 + N) % N;
        --size_;
    }

    void push_front(const T& val)
    {
        size_t ind = (back_ - size_ + N) % N;
        data_[ind] = val;
        ++size_;
    }

    T& front() { return data_[getFrontInd(0)]; }
    const T& front() const { return data_[getFrontInd(0)]; }

    void pop_front()
    {
        --size_;
    }

    T& operator[](size_t ind) { return data_[getFrontInd(ind)]; }
    const T& operator[](size_t ind) const { return data_[getFrontInd(ind)]; }

    bool full() const { return size_ == N; }
    bool empty() const { return !size_; }
    size_t size() const { return size_; }
private:
    size_t getFrontInd(size_t offset) const
    {
        size_t frontInd = back_ + 1 - size_;
        return (frontInd + offset + N) % N;
    }
private:
    T data_[N];
    size_t size_;
    size_t back_;
};

#endif
