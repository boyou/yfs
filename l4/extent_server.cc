// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extent_server::extent_server() {}

std::string
extent_server::filename(extent_protocol::extentid_t id)
{
  char buf[32];
  sprintf(buf, "./ID/%016llx", id);
  return std::string(buf, strlen(buf));
}

int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
  std::string f = filename(id);
  int r;
  printf("put %016llx %s %zu\n", id, f.c_str(), buf.size());
  int fd = open(f.c_str(), O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR);
  if(fd < 0)
    return extent_protocol::NOENT;

  unsigned int n = buf.size();

  if (ftruncate(fd, 0) < 0)
    return extent_protocol::IOERR;
  else {
    if (n > extent_protocol::maxextent)
      n = extent_protocol::maxextent;
    r = write(fd, buf.c_str(), n);
    printf ("written %d bytes\n", n);
  }
  close(fd);
  if(r >= 0) return extent_protocol::OK;
  else return extent_protocol::IOERR;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  std::string f = filename(id);
  printf("get %016llx %s\n", id, f.c_str());
  struct stat sb;
  if(stat(f.c_str(), &sb) != 0)
    return extent_protocol::NOENT;

  int fd = open(f.c_str(), O_RDONLY);
  if(fd < 0)
    return extent_protocol::NOENT;
  int n = extent_protocol::maxextent;
  char *p = new char[n];
  n = read(fd, p, n);
  close(fd);
  if(n >= 0){
    buf = std::string(p, n);
    delete p;
    printf ("get returns %d bytes\n", n);
    return extent_protocol::OK;
  } else {
    delete p;
    return extent_protocol::IOERR;
  }
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  std::string f = filename(id);
  printf("getattr %016llx %s\n", id, f.c_str());
  struct stat sb;
  if(stat(f.c_str(), &sb) != 0)
    return extent_protocol::NOENT;
  a.size = sb.st_size;
  a.atime = sb.st_atime;
  a.mtime = sb.st_mtime;
  a.ctime = sb.st_ctime;
  printf("getattr %016llx %s -> %u\n", id, f.c_str(), a.mtime);
  return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  std::string f = filename(id);
  printf("remove %016llx %s\n", id, f.c_str());
  if (unlink(f.c_str()) < 0) 
    return extent_protocol::NOENT;
  else
    return extent_protocol::OK;
}

