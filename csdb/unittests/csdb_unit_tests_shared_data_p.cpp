#include "csdb_unit_tests_shared_data.h"

#include "csdb/internal/shared_data_ptr_implementation.h"

class TestSharedData::priv : public csdb::internal::shared_data
{
public:
  priv()
  {
    ++counter_;
  }

  priv(int value) : value_(value)
  {
    ++counter_;
  }

  priv(const priv& other) :
    csdb::internal::shared_data(other),
    copy_constuctor_called_(true)
  {
    ++counter_;
  }

  ~priv()
  {
    --counter_;
  }

public:
  bool copy_constuctor_called_ = false;
  int value_ = 0;
  static std::atomic<int> counter_;

  friend class SharedDataTest;
};

std::atomic<int> TestSharedData::priv::counter_(0);

SHARED_DATA_CLASS_IMPLEMENTATION(TestSharedData)

TestSharedData::TestSharedData(int value) : d(new TestSharedData::priv(value))
{
}

bool TestSharedData::is_copied() const
{
  return d->copy_constuctor_called_;
}

bool TestSharedData::is_same(const TestSharedData &other) const
{
  return (&(*d)) == (&(*other.d));
}

int TestSharedData::value() const
{
  return d->value_;
}

void TestSharedData::setValue(int new_value)
{
  d->value_ = new_value;
}

int TestSharedData::counter()
{
  return TestSharedData::priv::counter_;
}
