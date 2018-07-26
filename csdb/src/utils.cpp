#include "csdb/internal/utils.h"
#include <deque>

#ifdef _MSC_VER
# include <direct.h>
# include <Shlobj.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#endif

namespace csdb {
namespace internal {

namespace
{

struct Node
{
  enum Type
  {
    File,
    Dir
  };

  Type type;
  std::string name;
};
using NodeList = std::deque<Node>;

NodeList children(const std::string &path)
{
  NodeList res;

#if defined(_MSC_VER)
  WIN32_FIND_DATAA d;
  HANDLE h = ::FindFirstFileA((path + "\\*.*").c_str(), &d);
  if(h != INVALID_HANDLE_VALUE)
  {
    do
    {
      const std::string name{d.cFileName};
      if((0 != (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) && ((name == ".") || (name == ".."))) {
        continue;
      }
      res.push_back({(0 != (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ? Node::Dir : Node::File, name});
    }
    while(::FindNextFileA(h, &d));

    ::FindClose(h);
  }
#else
  DIR *d = opendir(path.c_str());
  if(d)
  {
    struct dirent *entry;
    while((entry = readdir(d)) != NULL)
    {
      const std::string name{entry->d_name};
      if(entry->d_type == DT_DIR && (name == "." || name == ".."))
        continue;
      res.push_back({((entry->d_type == DT_DIR)? Node::Dir : Node::File), name});
    }
    closedir(d);
  }
#endif

  return res;
}

}

std::string path_add_separator(const std::string &path)
{
  if(path.empty())
    return path;

#ifdef _MSC_VER
  static const char sep = '\\';
#else
  static const char sep = '/';
#endif

  const char end = path[path.size() - 1];
  return ((end != '\\') && (end != '/')) ? (path + sep) : path;
}

std::string app_data_path()
{
#ifdef _MSC_VER
  char temp_char[MAX_PATH];
  HRESULT hresult = ::SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT | KF_FLAG_CREATE, temp_char);
  if(hresult == S_OK && temp_char[0])
    return path_add_separator(std::string(temp_char));
#else
  const std::string res = path_add_separator(std::string(getenv("HOME"))) + ".appdata/";
  if(path_make(res))
    return res;
#endif
  return std::string();
}

bool dir_exists(const std::string &path)
{
#ifdef _MSC_VER
  const DWORD attr = ::GetFileAttributesA(path.c_str());
  return (INVALID_FILE_ATTRIBUTES != attr) && (0 != (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
  struct stat buf;
  return (stat(path.c_str(), &buf) == 0) && ((buf.st_mode & S_IFMT) == S_IFDIR);
#endif
}

size_t file_size(const std::string &name)
{
#ifdef _MSC_VER
  WIN32_FILE_ATTRIBUTE_DATA fad;
  if ((!::GetFileAttributesExA(name.c_str(), GetFileExInfoStandard, &fad))
       || (INVALID_FILE_ATTRIBUTES == fad.dwFileAttributes)
       || (0 != (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))) {
    return static_cast<size_t>(-1);
  }
  return ((static_cast<size_t>(fad.nFileSizeHigh) << 32) | static_cast<size_t>(fad.nFileSizeLow));
#else
  struct stat buf;
  if ((0 != stat(name.c_str(), &buf)) || (S_IFDIR == (buf.st_mode & S_IFMT))) {
    return static_cast<size_t>(-1);
  }
  return static_cast<size_t>(buf.st_size);
#endif
}

bool dir_make(const std::string &path)
{
  if(dir_exists(path))
    return true;

#ifdef _MSC_VER
  return _mkdir(path.c_str()) == 0;
#else
  return mkdir(path.c_str(), 0755) == 0;
#endif
}

bool dir_remove(const std::string &path)
{
  if(!dir_exists(path))
    return true;

#ifdef _MSC_VER
  return _rmdir(path.c_str()) == 0;
#else
  return rmdir(path.c_str()) == 0;
#endif
}

bool path_make(const std::string &path)
{
  if(dir_make(path))
    return true;

  switch(errno)
  {
  case ENOENT:
    {
      std::string::size_type pos = path.find_last_of('/');
      if(pos == std::string::npos)
#if defined(_MSC_VER)
        pos = path.find_last_of('\\');
      if (pos == std::string::npos)
#endif
        return false;
      if(!path_make(path.substr(0, pos)))
        return false;
    }
    return dir_make(path);

  case EEXIST:
      return dir_exists(path);

  default:
      return false;
  }
}

bool path_remove(const std::string &path)
{
  if(dir_remove(path))
    return true;

  const NodeList nodes = children(path);
  for(const auto &it : nodes)
  {
    const std::string cur = path_add_separator(path) + it.name;
    switch(it.type)
    {
      case Node::Dir:
        if(!path_remove(cur))
          return false;
      break;

    case Node::File:
      if(!file_remove(cur))
        return false;
    }
  }

  return dir_remove(path);
}

size_t path_size(const std::string &path)
{
  if (!dir_exists(path)) {
    return file_size(path);
  }

  size_t total = 0;
  ::std::string path_sep = path_add_separator(path);
  for(const auto &it : children(path)) {
    size_t subtotal = path_size(path_sep + it.name);
    if (static_cast<size_t>(-1) != subtotal) {
      total += subtotal;
    }
  }

  return total;
}

bool file_remove(const std::string &path)
{
#ifdef _MSC_VER
  return _unlink(path.c_str()) == 0;
#else
  return unlink(path.c_str()) == 0;
#endif
}

} // namespace internal
} // namespace csdb
