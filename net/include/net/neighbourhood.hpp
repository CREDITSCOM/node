#ifndef __NEIGHBOURHOOD_HPP__
#define __NEIGHBOURHOOD_HPP__
#include <boost/asio.hpp>

#include <lib/system/allocators.hpp>
#include <lib/system/keys.hpp>

using namespace boost::asio;

class Network;

struct NeighbourEndpoints {
  ip::udp::endpoint in;

  bool specialOut;
  ip::udp::endpoint out;
};

typedef uint64_t ConnectionId;

class Neighbourhood {
public:
  struct Element {
    NeighbourEndpoints endpoints;
    PublicKey key;

    std::atomic<uint32_t> packetsCount = { 0 };
    uint32_t lastPacketsCount = 0;
    ConnectionId connId;

    std::atomic<Element*> next;
  };

  class const_iterator {
  public:
    const_iterator& operator++() {
      ptr_ = ptr_->next.load(std::memory_order_relaxed);
      return *this;
    }

    bool operator!=(const const_iterator& rhs) const {
      return ptr_ != rhs.ptr_;
    }

    const Element* operator->() const { return ptr_; }
    const Element& operator*() const { return *ptr_; }

  private:
    Element* ptr_;
    friend class Neighbourhood;
  };

  const_iterator begin() const {
    const_iterator result;
    result.ptr_ = first_.load(std::memory_order_relaxed);
    return result;
  }

  const_iterator end() const {
    const_iterator result;
    result.ptr_ = nullptr;
    return result;
  }

  Element* allocate();
  void deallocate(Element*);

  void insert(Element*);
  void remove(Element*);

private:
  TypedAllocator<Element, 64> allocator_;
  std::atomic<Element*> first_ = { nullptr };
};

#endif // __NEIGHBOURHOOD_HPP__
