// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);

}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;


  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;


  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}

// internal version of lookup
yfs_client::inum
yfs_client::ilookup(inum di, std::string name)
{
  std::string buf;
  printf("ilookup %016llx %s\n", di, name.c_str());
  if (ec->get(di, buf) != extent_protocol::OK) {
    return 0;
  }
  std::istringstream ist(buf);
  unsigned long long finum;
  std::string zname;
  while ((ist >> zname) && (ist >> finum)) {
    printf("ilookup entry %s %016llx\n", zname.c_str(), finum);
    if (zname == name) return finum;
  }
  printf("ilookup entry %s doesn't exist\n", name.c_str());
  return 0;
}

int
yfs_client::lookup(inum di, std::string name, inum &xinum)
{
  int r = OK;


  printf("lookup %016llx %s\n", di, name.c_str());
  xinum = ilookup(di, name);
  if(xinum == 0)
    r = NOENT;

 release:
  return r;
}

int
yfs_client::create(inum di, std::string name, int dir, inum &xinum)
{
  std::string f;
  int r = OK;
  std::string entry;
  std::ostringstream ost;
  std::string extent;
  

  printf("create %016llx %s\n", di, name.c_str());

  xinum = ilookup(di, name);
  if(xinum) {
		r = EXIST;
    goto release;
  }

  if (dir) {
    xinum = random();
  } else {
    xinum = random() | 0x80000000;
  }

  f = filename(xinum);

  ost << name;
  ost << " ";
  ost << xinum;

  if (ec->get(di, extent) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  extent = extent + ost.str();

  if (extent.size() > extent_protocol::maxextent) {
    r = FBIG;
    goto release;
  }

  if (ec->put(di, extent) != extent_protocol::OK)  {
    r = IOERR;
    goto release;
  }

  if (ec->put(xinum, "") != extent_protocol::OK) 
    r = IOERR;

 release:

  return r;
}


int
yfs_client::readdir(inum inum, unsigned long long ind, dirent &e)
{
  int r = OK;
  std::string zname;
  unsigned long long zinum;
  unsigned long long zind = 0;
  std::string extent;
  std::istringstream ist;


  printf("readdir %016llx ind %lld\n", inum, ind);

  if (ec->get(inum, extent) != extent_protocol::OK) {
    r = NOENT;
    goto release;
  }

  ist.str(extent);
  while ((ist >> zname) && (ist >> zinum)) {
    printf("readdir entry %llu %s %016llx\n", zind, zname.c_str(), zinum);
    if (zind == ind) {
      e.name = zname.c_str();
      e.inum = zinum;
      goto release;
    }
    zind++;
  }
  printf("return inum 0\n");
  e.name = "";
  e.inum = 0;

 release:
  return r;
}


