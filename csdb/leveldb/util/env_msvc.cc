// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <deque>
#include <limits>
#include <set>
#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "port/port.h"
#include "util/logging.h"
#include "util/mutexlock.h"
#include "util/posix_logger.h"
#include "util/env_posix_test_helper.h"

namespace leveldb {

namespace {

static int open_read_only_file_limit = -1;
static int mmap_limit = -1;

static const size_t kBufSize = 65536;

static Status WindowsError(const std::string& context, int last_error) {
  static char buf[1024];
  char* message;
  if (0 != ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, last_error,
                           MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPSTR)&message, 0, NULL)) {
      strncpy_s(buf, message, _TRUNCATE);
      ::LocalFree(message);
  } else {
    strncpy_s(buf, "Unknown error.", _TRUNCATE);
  }
  if (ERROR_FILE_NOT_FOUND == last_error) {
    return Status::NotFound(context, buf);
  } else {
    return Status::IOError(context, buf);
  }
}

// Helper class to limit resource usage to avoid exhaustion.
// Currently used to limit read-only file descriptors and mmap file usage
// so that we do not end up running out of file descriptors, virtual memory,
// or running into kernel performance problems for very large databases.
class Limiter {
 public:
  // Limit maximum number of resources to |n|.
  Limiter(intptr_t n) {
    SetAllowed(n);
  }

  // If another resource is available, acquire it and return true.
  // Else return false.
  bool Acquire() {
    if (GetAllowed() <= 0) {
      return false;
    }
    MutexLock l(&mu_);
    intptr_t x = GetAllowed();
    if (x <= 0) {
      return false;
    } else {
      SetAllowed(x - 1);
      return true;
    }
  }

  // Release a resource acquired by a previous call to Acquire() that returned
  // true.
  void Release() {
    MutexLock l(&mu_);
    SetAllowed(GetAllowed() + 1);
  }

 private:
  port::Mutex mu_;
  port::AtomicPointer allowed_;

  intptr_t GetAllowed() const {
    return reinterpret_cast<intptr_t>(allowed_.Acquire_Load());
  }

  // REQUIRES: mu_ must be held
  void SetAllowed(intptr_t v) {
    allowed_.Release_Store(reinterpret_cast<void*>(v));
  }

  Limiter(const Limiter&);
  void operator=(const Limiter&);
};

class WindowsSequentialFile: public SequentialFile {
 private:
  std::string filename_;
  HANDLE fd_;

 public:
  WindowsSequentialFile(const std::string& fname, HANDLE fd)
      : filename_(fname), fd_(fd) {}
  virtual ~WindowsSequentialFile() { ::CloseHandle(fd_); }

  virtual Status Read(size_t n, Slice* result, char* scratch) {
    DWORD r;
    if (::ReadFile(fd_, scratch, static_cast<DWORD>(n), &r, NULL)) {
      *result = Slice(scratch, r);
      return Status::OK();
    } else {
      return WindowsError(filename_, ::GetLastError());
    }
  }

  virtual Status Skip(uint64_t n) {
    LONG lo = static_cast<LONG>(n);
    LONG hi = static_cast<LONG>(n >> 32);
    if (INVALID_SET_FILE_POINTER == ::SetFilePointer(fd_, lo, &hi, FILE_CURRENT)) {
      return WindowsError(filename_, ::GetLastError());
    }
    return Status::OK();
  }
};

class WindowsRandomAccessFile: public RandomAccessFile {
 private:
  std::string filename_;
  bool temporary_fd_;  // If true, fd_ is -1 and we open on every read.
  HANDLE fd_;
  Limiter* limiter_;

 public:
  WindowsRandomAccessFile(const std::string& fname, HANDLE fd, Limiter* limiter)
      : filename_(fname), fd_(fd), limiter_(limiter) {
    temporary_fd_ = !limiter->Acquire();
    if (temporary_fd_) {
      // Open file on every access.
      ::CloseHandle(fd_);
      fd_ = INVALID_HANDLE_VALUE;
    }
  }

  virtual ~WindowsRandomAccessFile() {
    if (!temporary_fd_) {
      ::CloseHandle(fd_);
      limiter_->Release();
    }
  }

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    HANDLE fd = fd_;
    LARGE_INTEGER pos, savePos;
    if (temporary_fd_) {
      fd = ::CreateFileA(filename_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                         0, NULL);
      if (INVALID_HANDLE_VALUE == fd) {
        return WindowsError(filename_, ::GetLastError());
      }
    } else {
      pos.QuadPart = 0;
      ::SetFilePointerEx(fd, pos, &savePos, FILE_CURRENT);
    }
    pos.QuadPart = offset;
    ::SetFilePointerEx(fd, pos, NULL, FILE_BEGIN);

    Status s;
    size_t total_read = 0;
    char *p = scratch;
    while (n) {
      size_t to_read = std::min(n, static_cast<size_t>(std::numeric_limits<DWORD>::max()));
      DWORD read = 0;
      if (!::ReadFile(fd, p, static_cast<DWORD>(to_read), &read, NULL)) {
        s = WindowsError(filename_, ::GetLastError());
        total_read = 0;
        break;
      }
      total_read += read;
      if (static_cast<size_t>(read) != to_read) {
        break;
      }
      n -= read;
      p += read;
    }
    *result = Slice(scratch, total_read);

    if (temporary_fd_) {
      ::CloseHandle(fd);
    } else {
      ::SetFilePointerEx(fd, savePos, NULL, FILE_BEGIN);
    }

    return s;
  }
};

// mmap() based random-access
class WindowsMmapReadableFile: public RandomAccessFile {
 private:
  std::string filename_;
  HANDLE file_;
  HANDLE map_;
  void* mmapped_region_;
  size_t length_;
  Limiter* limiter_;

 public:
  // base[0,length-1] contains the mmapped contents of the file.
  WindowsMmapReadableFile(const std::string& fname, HANDLE file, HANDLE  map, void* base, size_t length,
                          Limiter* limiter)
      : filename_(fname), file_(file), map_(map), mmapped_region_(base), length_(length),
        limiter_(limiter) {
  }

  virtual ~WindowsMmapReadableFile() {
    ::UnmapViewOfFile(mmapped_region_);
    ::CloseHandle(map_);
    ::CloseHandle(file_);
    limiter_->Release();
  }

  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      char* scratch) const {
    Status s;
    if (offset + n > length_) {
      *result = Slice();
      s = WindowsError(filename_, ERROR_ACCESS_DENIED);
    } else {
      *result = Slice(reinterpret_cast<char*>(mmapped_region_) + offset, n);
    }
    return s;
  }
};

class WindowsWritableFile : public WritableFile {
 private:
  // buf_[0, pos_-1] contains data to be written to fd_.
  std::string filename_;
  HANDLE fd_;
  char buf_[kBufSize];
  size_t pos_;

 public:
  WindowsWritableFile(const std::string& fname, HANDLE fd)
      : filename_(fname), fd_(fd), pos_(0) { }

  ~WindowsWritableFile() {
    if (INVALID_HANDLE_VALUE != fd_) {
      // Ignoring any potential errors
      Close();
    }
  }

  virtual Status Append(const Slice& data) {
    size_t n = data.size();
    const char* p = data.data();

    // Fit as much as possible into buffer.
    size_t copy = std::min(n, kBufSize - pos_);
    memcpy(buf_ + pos_, p, copy);
    p += copy;
    n -= copy;
    pos_ += copy;
    if (n == 0) {
      return Status::OK();
    }

    // Can't fit in buffer, so need to do at least one write.
    Status s = FlushBuffered();
    if (!s.ok()) {
      return s;
    }

    // Small writes go to buffer, large writes are written directly.
    if (n < kBufSize) {
      memcpy(buf_, p, n);
      pos_ = n;
      return Status::OK();
    }
    return WriteRaw(p, n);
  }

  virtual Status Close() {
    Status result = FlushBuffered();
    ::SetEndOfFile(fd_);
    if ((!::CloseHandle(fd_)) && result.ok()) {
      result = WindowsError(filename_, ::GetLastError());
    }
    fd_ = INVALID_HANDLE_VALUE;
    return result;
  }

  virtual Status Flush() {
    return FlushBuffered();
  }

  Status SyncDirIfManifest() {
    const char* f = filename_.c_str();
    const char* sep = strrchr(f, '/');
    Slice basename;
    std::string dir;
    if (sep == NULL) {
      dir = ".";
      basename = f;
    } else {
      dir = std::string(f, sep - f);
      basename = sep + 1;
    }
    Status s;
    if (basename.starts_with("MANIFEST")) {
      ::FlushFileBuffers(fd_);
      HANDLE fd = ::CreateFileA(dir.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
      if (INVALID_HANDLE_VALUE == fd) {
        s = WindowsError(dir, ::GetLastError());
      } else {
        if (!::FlushFileBuffers(fd)) {
          s = WindowsError(dir, ::GetLastError());
        }
        ::CloseHandle(fd);
      }
    }
    return s;
  }

  virtual Status Sync() {
    // Ensure new files referred to by the manifest are in the filesystem.
    Status s = SyncDirIfManifest();
    if (!s.ok()) {
      return s;
    }
    s = FlushBuffered();
    if (s.ok()) {
      if (!::FlushFileBuffers(fd_)) {
        s = WindowsError(filename_, ::GetLastError());
      }
    }
    return s;
  }

 private:
  Status FlushBuffered() {
    Status s = WriteRaw(buf_, pos_);
    pos_ = 0;
    return s;
  }

  Status WriteRaw(const char* p, size_t n) {
    while (n > 0) {
      DWORD to_write = static_cast<DWORD>(std::min(n, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
      DWORD written;
      if (!::WriteFile(fd_, p, to_write, &written, NULL)) {
        return WindowsError(filename_, ::GetLastError());
      }
      p += written;
      n -= written;
    }
    return Status::OK();
  }
};

static bool LockOrUnlock(HANDLE fd, bool lock) {
  LARGE_INTEGER fs;
  if (!::GetFileSizeEx(fd, &fs)) {
    return false;
  }


  if (lock) {
    if (!::LockFile(fd, 0, 0, fs.LowPart, fs.HighPart )) {
      return false;
    }
  } else {
    if (!::UnlockFile(fd, 0, 0, fs.LowPart, fs.HighPart )) {
      return false;
    }
  }
  return true;
}

class WindowsFileLock : public FileLock {
 public:
  HANDLE fd_;
  std::string name_;
};

// Set of locked files.  We keep a separate set instead of just
// relying on fcntrl(F_SETLK) since fcntl(F_SETLK) does not provide
// any protection against multiple uses from the same process.
class WindowsLockTable {
 private:
  port::Mutex mu_;
  std::set<std::string> locked_files_;
 public:
  bool Insert(const std::string& fname) {
    MutexLock l(&mu_);
    return locked_files_.insert(fname).second;
  }
  void Remove(const std::string& fname) {
    MutexLock l(&mu_);
    locked_files_.erase(fname);
  }
};

class WindowsEnv : public Env {
 public:
  WindowsEnv();
#pragma warning(disable: 4722)
  virtual ~WindowsEnv() {
    char msg[] = "Destroying Env::Default()\n";
    fwrite(msg, 1, sizeof(msg), stderr);
    abort();
  }
#pragma warning(default: 4722)

  virtual Status NewSequentialFile(const std::string& fname,
                                   SequentialFile** result) {
    HANDLE fd = ::CreateFileA(fname.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                              0, NULL);
    if (INVALID_HANDLE_VALUE == fd) {
      *result = NULL;
      return WindowsError(fname, ::GetLastError());
    } else {
      *result = new WindowsSequentialFile(fname, fd);
      return Status::OK();
    }
  }

  virtual Status NewRandomAccessFile(const std::string& fname,
                                     RandomAccessFile** result) {
    *result = NULL;
    Status s;
    HANDLE fd = ::CreateFileA(fname.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                              0, NULL);
    if (INVALID_HANDLE_VALUE == fd) {
      s = WindowsError(fname, ::GetLastError());
    } else if (mmap_limit_.Acquire()) {
      LARGE_INTEGER size;
      if (!::GetFileSizeEx(fd, &size)) {
        s = WindowsError(fname, ::GetLastError());
      } else {
        HANDLE map = ::CreateFileMappingA(fd, NULL, PAGE_READONLY, size.HighPart, size.LowPart, NULL);
        if (NULL == map) {
          s = WindowsError(fname, ::GetLastError());
        } else {
          void* base = ::MapViewOfFileEx(map, FILE_MAP_READ, 0, 0, static_cast<SIZE_T>(size.QuadPart), NULL);
          if (NULL == base) {
            s = WindowsError(fname, ::GetLastError());
            ::CloseHandle(map);
          } else {
            *result = new WindowsMmapReadableFile(fname, fd, map, base, static_cast<size_t>(size.QuadPart),
                                                  &mmap_limit_);
          }
        }
      }
      if (!s.ok()) {
        ::CloseHandle(fd);
        mmap_limit_.Release();
      }
    } else {
      *result = new WindowsRandomAccessFile(fname, fd, &fd_limit_);
    }
    return s;
  }

  virtual Status NewWritableFile(const std::string& fname,
                                 WritableFile** result) {
    HANDLE fd = ::CreateFileA(fname.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL, NULL);
    if (INVALID_HANDLE_VALUE == fd) {
      *result = NULL;
      return WindowsError(fname, ::GetLastError());
    } else {
      *result = new WindowsWritableFile(fname, fd);
      return Status::OK();
    }
  }

  virtual Status NewAppendableFile(const std::string& fname,
                                   WritableFile** result) {
    HANDLE fd = ::CreateFileA(fname.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL, NULL);
    if (INVALID_HANDLE_VALUE == fd) {
      *result = NULL;
      return WindowsError(fname, ::GetLastError());
    } else {
      LARGE_INTEGER pos;
      pos.QuadPart = 0;
      if( !::SetFilePointerEx(fd, pos, NULL, FILE_END)) {
        return WindowsError(fname, ::GetLastError());
      }
      *result = new WindowsWritableFile(fname, fd);
      return Status::OK();
    }
  }

  virtual bool FileExists(const std::string& fname) {
    return _access(fname.c_str(), 0) == 0;
  }

  virtual Status GetChildren(const std::string& dir,
                             std::vector<std::string>* result) {
    result->clear();

    WIN32_FIND_DATAA d;
    HANDLE h = ::FindFirstFileA((dir + "\\*.*").c_str(), &d);
    if (INVALID_HANDLE_VALUE == h)
    {
      int err = ::GetLastError();
      if (ERROR_FILE_NOT_FOUND != ::GetLastError()) {
        return WindowsError(dir, err);
      } else {
        return Status::OK();
      }
    }

    do {
      result->push_back(d.cFileName);
    } while(::FindNextFileA(h, &d));

    ::FindClose(h);

    return Status::OK();
  }

  virtual Status DeleteFile(const std::string& fname) {
    Status result;
    if (_unlink(fname.c_str()) != 0) {
      result = WindowsError(fname, errno);
    }
    return result;
  }

  virtual Status CreateDir(const std::string& name) {
    Status result;
    if (_mkdir(name.c_str()) != 0) {
      result = WindowsError(name, errno);
    }
    return result;
  }

  virtual Status DeleteDir(const std::string& name) {
    Status result;
    if (_rmdir(name.c_str()) != 0) {
      result = WindowsError(name, errno);
    }
    return result;
  }

  virtual Status GetFileSize(const std::string& fname, uint64_t* size) {
    Status s;
    struct stat sbuf;
    if (stat(fname.c_str(), &sbuf) != 0) {
      *size = 0;
      s = WindowsError(fname, errno);
    } else {
      *size = sbuf.st_size;
    }
    return s;
  }

  virtual Status RenameFile(const std::string& src, const std::string& target) {
    Status result;
    _unlink(target.c_str());
    if (rename(src.c_str(), target.c_str()) != 0) {
      result = WindowsError(src, errno);
    }
    return result;
  }

  virtual Status LockFile(const std::string& fname, FileLock** lock) {
    *lock = NULL;
    Status result;
    HANDLE fd = ::CreateFileA(fname.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL, NULL);
    if (INVALID_HANDLE_VALUE == fd) {
      result = WindowsError(fname, ::GetLastError());
    } else if (!locks_.Insert(fname)) {
      result = Status::IOError("lock " + fname, "already held by process");
      ::CloseHandle(fd);
    } else if (!LockOrUnlock(fd, true)) {
      result = WindowsError("lock " + fname, ::GetLastError());
      ::CloseHandle(fd);
      locks_.Remove(fname);
    } else {
      WindowsFileLock* my_lock = new WindowsFileLock;
      my_lock->fd_ = fd;
      my_lock->name_ = fname;
      *lock = my_lock;
    }
    return result;
  }

  virtual Status UnlockFile(FileLock* lock) {
    WindowsFileLock* my_lock = reinterpret_cast<WindowsFileLock*>(lock);
    Status result;
    if (!LockOrUnlock(my_lock->fd_, false)) {
      result = WindowsError("unlock", ::GetLastError());
    }
    locks_.Remove(my_lock->name_);
    ::CloseHandle(my_lock->fd_);
    delete my_lock;
    return result;
  }

  virtual void Schedule(void (*function)(void*), void* arg);

  virtual void StartThread(void (*function)(void* arg), void* arg);

  virtual Status GetTestDirectory(std::string* result) {
    DWORD len = ::GetEnvironmentVariableA("TEST_TMPDIR", NULL, 0);
    if (0 < len) {
      char* env = (char*)_alloca(len + 1);
      ::GetEnvironmentVariableA("TEST_TMPDIR", env, len + 1);
      *result = env;
    } else {
      char buf[MAX_PATH + 31];
      len = ::GetTempPathA(sizeof(buf) - 1, buf);
      if ((0 < len) && (('\\' == buf[len-1]) || ('//' == buf[len-1]))) {
        buf[--len] = '\0';
      }
      sprintf_s(buf + len, sizeof(buf) - len, "/leveldbtest-%u",
                static_cast<unsigned int>(::GetCurrentThreadId()));
      *result = buf;
    }
    // Directory may already exist
    CreateDir(*result);
    return Status::OK();
  }

  static uint64_t gettid() {
    return static_cast<uint64_t>(::GetCurrentThreadId());
  }

  virtual Status NewLogger(const std::string& fname, Logger** result) {
    FILE* f;
    errno_t err = fopen_s(&f, fname.c_str(), "w");
    if (0 != err) {
      *result = NULL;
      return WindowsError(fname, err);
    } else {
      *result = new PosixLogger(f, &WindowsEnv::gettid);
      return Status::OK();
    }
  }

  virtual uint64_t NowMicros() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
  }

  virtual void SleepForMicroseconds(int micros) {
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
  }

 private:

  // BGThread() is the body of the background thread
  void BGThread();
  static unsigned __stdcall BGThreadWrapper(void* arg) {
    reinterpret_cast<WindowsEnv*>(arg)->BGThread();
    return NULL;
  }

  port::Mutex mu_;
  port::CondVar bgsignal_;
  bool started_bgthread_;

  // Entry per Schedule() call
  struct BGItem { void* arg; void (*function)(void*); };
  typedef std::deque<BGItem> BGQueue;
  BGQueue queue_;

  WindowsLockTable locks_;
  Limiter mmap_limit_;
  Limiter fd_limit_;
};

// Return the maximum number of concurrent mmaps.
static int MaxMmaps() {
  if (mmap_limit >= 0) {
    return mmap_limit;
  }
  // Up to 1000 mmaps for 64-bit binaries; none for smaller pointer sizes.
  mmap_limit = sizeof(void*) >= 8 ? 1000 : 0;
  return mmap_limit;
}

// Return the maximum number of read-only files to keep open.
static intptr_t MaxOpenFiles() {
  if (open_read_only_file_limit >= 0) {
    return open_read_only_file_limit;
  }
  open_read_only_file_limit = _getmaxstdio();
  return open_read_only_file_limit;
}

WindowsEnv::WindowsEnv()
    : bgsignal_(&mu_),
      started_bgthread_(false),
      mmap_limit_(MaxMmaps()),
      fd_limit_(MaxOpenFiles()) {
}

void WindowsEnv::Schedule(void (*function)(void*), void* arg) {
  mu_.Lock();

  // Start background thread if necessary
  if (!started_bgthread_) {
    started_bgthread_ = true;
    ::CloseHandle(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, WindowsEnv::BGThreadWrapper, this, 0, NULL)));
  }

  // If the queue is currently empty, the background thread may currently be
  // waiting.
  if (queue_.empty()) {
    bgsignal_.Signal();
  }

  // Add to priority queue
  queue_.push_back(BGItem());
  queue_.back().function = function;
  queue_.back().arg = arg;

  mu_.Unlock();
}

void WindowsEnv::BGThread() {
  while (true) {
    // Wait until there is an item that is ready to run
    mu_.Lock();
    while (queue_.empty()) {
      bgsignal_.Wait();
    }

    void (*function)(void*) = queue_.front().function;
    void* arg = queue_.front().arg;
    queue_.pop_front();

    mu_.Unlock();
    (*function)(arg);
  }
}

namespace {
struct StartThreadState {
  void (*user_function)(void*);
  void* arg;
};
}
static unsigned __stdcall StartThreadWrapper(void* arg) {
  StartThreadState* state = reinterpret_cast<StartThreadState*>(arg);
  state->user_function(state->arg);
  delete state;
  return NULL;
}

void WindowsEnv::StartThread(void (*function)(void* arg), void* arg) {
  ::CloseHandle(reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, StartThreadWrapper,
                                                        new StartThreadState{function, arg}, 0, NULL)));
}

}  // namespace

static std::once_flag once;
static Env* default_env;
static void InitDefaultEnv() { default_env = new WindowsEnv; }

void EnvPosixTestHelper::SetReadOnlyFDLimit(int limit) {
  assert(default_env == NULL);
  open_read_only_file_limit = limit;
}

void EnvPosixTestHelper::SetReadOnlyMMapLimit(int limit) {
  assert(default_env == NULL);
  mmap_limit = limit;
}

Env* Env::Default() {
  std::call_once(once, InitDefaultEnv);
  return default_env;
}

}  // namespace leveldb
