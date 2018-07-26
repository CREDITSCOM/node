// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/testharness.h"

#include <string>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _MSC_VER
# include <windows.h>
#endif

namespace leveldb {
namespace test {

namespace {
struct Test {
  const char* base;
  const char* name;
  void (*func)();
};
std::vector<Test>* tests;
}

bool RegisterTest(const char* base, const char* name, void (*func)()) {
  if (tests == NULL) {
    tests = new std::vector<Test>;
  }
  Test t;
  t.base = base;
  t.name = name;
  t.func = func;
  tests->push_back(t);
  return true;
}

int RunAllTests() {
#ifdef _MSC_VER
  const char* matcher = nullptr;
  DWORD len = ::GetEnvironmentVariableA("LEVELDB_TESTS", NULL, 0);
  if (0 < len) {
    char* buf = (char *)_alloca(len + 1);
    ::GetEnvironmentVariableA("LEVELDB_TESTS", buf, len + 1);
    matcher = buf;
  }
#else
  const char* matcher = getenv("LEVELDB_TESTS");
#endif

  int num = 0;
  if (tests != NULL) {
    for (size_t i = 0; i < tests->size(); i++) {
      const Test& t = (*tests)[i];
      if (matcher != NULL) {
        std::string name = t.base;
        name.push_back('.');
        name.append(t.name);
        if (strstr(name.c_str(), matcher) == NULL) {
          continue;
        }
      }
      fprintf(stderr, "==== Test %s.%s\n", t.base, t.name);
      (*t.func)();
      ++num;
    }
  }
  fprintf(stderr, "==== PASSED %d tests\n", num);
  return 0;
}

std::string TmpDir() {
  std::string dir;
  Status s = Env::Default()->GetTestDirectory(&dir);
  ASSERT_TRUE(s.ok()) << s.ToString();
  return dir;
}

int RandomSeed() {
#ifdef _MSC_VER
  const char* env = nullptr;
  DWORD len = ::GetEnvironmentVariableA("TEST_RANDOM_SEED", NULL, 0);
  if (0 < len) {
    char* buf = (char *)_alloca(len + 1);
    ::GetEnvironmentVariableA("TEST_RANDOM_SEED", buf, len + 1);
    env = buf;
  }
#else
  const char* env = getenv("TEST_RANDOM_SEED");
#endif
  int result = (env != NULL ? atoi(env) : 301);
  if (result <= 0) {
    result = 301;
  }
  return result;
}

}  // namespace test
}  // namespace leveldb
