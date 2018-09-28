/**
  * @file csdb_unit_tests_shared_data.h
  * @author Evgeny V. Zalivochkin
  */

#pragma once
#ifndef _CREDITS_CSDB_UNIT_TESTS_SHARED_DATA_H_INCLUDED_
#define _CREDITS_CSDB_UNIT_TESTS_SHARED_DATA_H_INCLUDED_

#include "csdb/internal/shared_data.h"
#include <atomic>

class TestSharedData
{
  SHARED_DATA_CLASS_DECLARE(TestSharedData)

public:
  explicit TestSharedData(int value);
  bool is_copied() const;
  bool is_same(const TestSharedData &other) const;

  int value() const;
  void setValue(int new_value);

  static int counter();
};

#endif // _CREDITS_CSDB_UNIT_TESTS_SHARED_DATA_H_INCLUDED_
