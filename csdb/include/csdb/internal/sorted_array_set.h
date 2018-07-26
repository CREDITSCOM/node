#ifndef SORTED_ARRAY_SET_H
#define SORTED_ARRAY_SET_H

#include <cinttypes>
#include <cstring>
#include <algorithm>
#include <cassert>

namespace csdb {
namespace internal {

class sorted_array_set
{
public:

  using Byte = uint8_t;
  using SizeType = size_t;

  sorted_array_set() = default;

  sorted_array_set(const Byte* elements, SizeType elementsCount, SizeType elementSize) :
    elements{elements}, elementsCount{elementsCount}, elementSize{elementSize}
  {
    assert( elements != nullptr );
    assert( elementsCount > 0 );
    assert( elementSize > 0 );
  }

  // Copy semantics might be unclear to the user, thus disallow copy and assign for now
  sorted_array_set(const sorted_array_set&) = delete;
  sorted_array_set& operator=(const sorted_array_set&) = delete;

  sorted_array_set(sorted_array_set&&) = default;
  sorted_array_set& operator=(sorted_array_set&&) = default;

  template <class T>
  sorted_array_set(const T* elements, SizeType elementsCount, SizeType elementSize = sizeof(T)) :
    sorted_array_set(reinterpret_cast<const Byte*>(elements), elementsCount, elementSize)
  {
  }

  template <class T>
  bool contains(const T* element) const
  {
    assert(element != nullptr);

    const Byte* ptr = reinterpret_cast<const Byte*>(element);
    const size_t index = getIndex(ptr);
    return index != size();
  }

  template <class T>
  size_t getIndex(const T* element) const
  {
    assert(element != nullptr);

    const Byte* ptr = reinterpret_cast<const Byte*>(element);
    const size_t index = find(ptr);
    return index;
  }

  const Byte* operator[](size_t index) const
  {
    return &elements[ index * elementSize ];
  }

  size_t size() const { return elementsCount; }

  template <size_t E, class T>
  static void sort(T* ptr, SizeType elementsCount)
  {
    static constexpr size_t elementSize = E;

    struct Element
    {
      Byte data[elementSize];
    };

    Element* elements = reinterpret_cast<Element*>(ptr);
    Comparator comparator(elementSize);

    std::sort(elements, elements + elementsCount, [&](const Element& l, const Element& r)
      {
        const Byte& a = reinterpret_cast<const Byte&>(l);
        const Byte& b = reinterpret_cast<const Byte&>(r);
        return comparator(a, b);
      });
  }

  template <size_t E, class T>
  static bool isSorted(const T* ptr, SizeType elementsCount)
  {
    static constexpr size_t elementSize = E;

    struct Element
    {
      Byte data[elementSize];
    };

    const Element* elements = reinterpret_cast<const Element*>(ptr);
    Comparator comparator(elementSize);

    auto sorted = std::is_sorted(elements, elements + elementsCount, [&](const Element& l, const Element& r)
      {
        const Byte& a = reinterpret_cast<const Byte&>(l);
        const Byte& b = reinterpret_cast<const Byte&>(r);
        return comparator(a, b);
      });

    return sorted;
  }

private:

  struct Comparator
  {
    bool operator()(const Byte& l, const Byte& r) const
    {
      auto res = std::lexicographical_compare(&l, &l + elementSize, &r, &r + elementSize);
      return res;
    }

    Comparator(SizeType elementSize) : elementSize{ elementSize } {}

    SizeType elementSize;
  };

  template <class I, class T, class P>
  I lower_bound(I first, I last, const T& val, const P& pred) const
  {
    using namespace std;

    I it;
    typename iterator_traits<I>::difference_type count, step;
    count = distance(first, last) / elementSize;

    while (count > 0)
    {
      it = first;
      step = count / 2;
      advance(it, step * elementSize);

      if ( pred(*it, val) )
      {
        it += elementSize;
        first = it;
        count -= step + 1;
      }
      else
      {
        count = step;
      }
    }

    return first;
  }

  size_t find(const Byte* element) const
  {
    const SizeType length = elementsCount * elementSize;

    auto it = lower_bound(elements, elements + length, *element, Comparator(elementSize));

    if ( it != (elements + length) )
    {
      if ( memcmp(it, element, elementSize) == 0 )
        return std::distance(elements, it) / elementSize;
    }

    return size();
  }

  const Byte* elements = nullptr;
  SizeType elementsCount = 0;
  SizeType elementSize = 0;
};

//
// Templated version
// (Easier to use, but no performance gain)
//

template <size_t N>
class sorted_array_set_t
{
public:

  using Byte = uint8_t;
  using SizeType = size_t;

  static constexpr SizeType elementSize = N;

  sorted_array_set_t() = default;

  sorted_array_set_t(const Byte* elements, SizeType elementsCount) :
    elements{ reinterpret_cast<const element_adaptor*>(elements) }, elementsCount{ elementsCount }
  {
    assert(elements != nullptr);
    assert(elementsCount > 0);
  }

  // Copy semantics might be unclear to the user, thus disallow copy and assign for now
  sorted_array_set_t(const sorted_array_set_t&) = delete;
  sorted_array_set_t& operator=(const sorted_array_set_t&) = delete;

  sorted_array_set_t(sorted_array_set_t&&) = default;
  sorted_array_set_t& operator=(sorted_array_set_t&&) = default;

  template <class T>
  sorted_array_set_t(const T* elements, SizeType elementsCount) :
    sorted_array_set_t(reinterpret_cast<const Byte*>(elements), elementsCount)
  {
  }

  template <class T>
  bool contains(const T* element) const
  {
    assert(element != nullptr);

    const element_adaptor* ptr = reinterpret_cast<const element_adaptor*>(element);
    const size_t index = getIndex(ptr);
    return index != size();
  }

  template <class T>
  size_t getIndex(const T* element) const
  {
    assert(element != nullptr);

    const element_adaptor* ptr = reinterpret_cast<const element_adaptor*>(element);
    const size_t index = find(ptr);
    return index;
  }

  const Byte* operator[](size_t index) const
  {
    const Byte* element = reinterpret_cast<const Byte*>(&elements[index]);
    return element;
  }

  size_t size() const { return elementsCount; }

  template <class T>
  static void sort(T* ptr, SizeType elementsCount)
  {
    element_adaptor* elements = reinterpret_cast<element_adaptor*>(ptr);

    std::sort(elements, elements + elementsCount, Comparator());
  }

  template <class T>
  static bool isSorted(T* ptr, SizeType elementsCount)
  {
    element_adaptor* elements = reinterpret_cast<element_adaptor*>(ptr);

    auto sorted = std::is_sorted(elements, elements + elementsCount, Comparator());
    return sorted;
  }

private:

  struct element_adaptor
  {
    Byte data[elementSize];
  };

  struct Comparator
  {
    bool operator()(const element_adaptor& l, const element_adaptor& r) const
    {
      const Byte& a = reinterpret_cast<const Byte&>(l.data);
      const Byte& b = reinterpret_cast<const Byte&>(r.data);

      auto res = std::lexicographical_compare(&a, &a + elementSize, &b, &b + elementSize);
      return res;
    }
  };

  size_t find(const element_adaptor* element) const
  {
    auto end = elements + elementsCount;
    auto it = std::lower_bound(elements, end, *element, Comparator());

    if (it != end)
    {
      if (memcmp(it, element, elementSize) == 0)
        return std::distance(elements, it);
    }

    return size();
  }

  const element_adaptor* elements = nullptr;
  SizeType elementsCount = 0;
};

} // namespace internal
} // namespace csdb

#endif // SORTED_ARRAY_SET_H
