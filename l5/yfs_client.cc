// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include "lock_client.h"
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
  lc = new lock_client(lock_dst);

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

  if (lc->acquire(inum) < 0) {
    r = yfs_client::IOERR;
    goto release;
  }

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
  if (lc->release(inum) < 0) {
    r = IOERR;
  }

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;

  if (lc->acquire(inum) < 0) {
    r = IOERR;
    goto release;
  }

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
  if (lc->release(inum) < 0) {
    r = IOERR;
  }
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

  if (lc->acquire(di) < 0) {
    r = IOERR;
    goto release;
  }

  printf("lookup %016llx %s\n", di, name.c_str());
  xinum = ilookup(di, name);
  if(xinum == 0)
    r = NOENT;

 release:
  if (lc->release(di) < 0) {
    r = IOERR;
  }
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
  
  if (lc->acquire(di) < 0) {
    return IOERR;
  }

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
  printf("create: release lock on dir %016llx\n", di);
  if (lc->release(di) < 0) {
    r = IOERR;
  }

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

  if (lc->acquire(inum) < 0) {
    return IOERR;
  }

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
  if (lc->release(inum) < 0) {
    r = IOERR;
  }
  return r;
}

int
yfs_client::unlink(inum di, std::string name)
{
  int r = OK;
  std::string extent;
  std::ostringstream ost;
  std::string::size_type i;

  if (lc->acquire(di) < 0) {
    return IOERR;
  }

  printf("unlink %016llx %s\n", di, name.c_str());

  inum xinum = ilookup(di, name);
  if(!xinum) {
    //  r = NOENT;
    goto release_dir;
  }
  
  if (lc->acquire(xinum) < 0) {
    r = IOERR;
    goto release_dir;
  }

  if (ec->get(di, extent) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  ost << name;
  ost << " ";
  ost << xinum;

  printf("unlink %016llx\n", xinum);
  
  i = extent.find(ost.str());
  assert (i != std::string::npos);

  extent.erase(i, ost.str().size());

  r = ec->remove(xinum);
  if (r == extent_protocol::NOENT) {
    printf ("unlink: extent didn't exist\n");
    r = OK;
  }

  if (r != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  printf ("unlink: write directory back\n");

  if (ec->put(di, extent) != extent_protocol::OK)  {
    r = IOERR;
  }

  printf ("unlink: written directory back\n");

 release:
  if (lc->release(xinum) < 0) {
    r = IOERR;
  }
 release_dir:
  if (lc->release(di) < 0) {
    r = IOERR;
  }
  return r;
}


int
yfs_client::readfile(inum inum, unsigned long long off, unsigned int n, std::string &buf)
{
  std::string extent;
  int r = OK;
  char *p;

  if (lc->acquire(inum) < 0) {
    r = IOERR;
    goto release;
  }

  printf("readfile %016llx off=%lld n=%u\n", inum, off, n);

  if (ec->get(inum, extent) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  
  if(n > extent.size())
    n =  extent.size();
  if(off >= extent.size())
    n = 0;
  p = new char[n];
  memcpy(p, extent.c_str() + off, n);
  buf = std::string(p, n);
  delete p;

 release:
  if (lc->release(inum) < 0) {
    r = IOERR;
  }
  return r;
}

int
yfs_client::writefile(inum inum, unsigned long long off, std::string buf)
{
  std::string extent;
  int r = OK;

  if (lc->acquire(inum) < 0) {
    r = IOERR;
    goto release;
  }

  printf("writefile %016llx off=%lld n=%zu\n", inum, off, buf.size());

  r = ec->get(inum, extent);

  if (off == (unsigned long long) -1)
    off = extent.size();
  extent.replace(off, buf.size(), buf);

  if (ec->put(inum, extent) != extent_protocol::OK) 
    r = IOERR;

 release:
  if (lc->release(inum) < 0) {
    r = IOERR;
  }
  return r;
}

int
yfs_client::setsize(inum inum, unsigned long long sz)
{
  std::string extent;
  int r = OK;
  std::string buf;

 if (lc->acquire(inum) < 0) {
    r = IOERR;
    goto release;
  }

  printf("setsize %016llx sz=%lld\n", inum, sz);
  r = ec->get(inum, extent);
  buf = extent.substr(0, sz);  // XXX sz > extent.size()

  if (ec->put(inum, buf) != extent_protocol::OK) 
    r = IOERR;

 release:
  if (lc->release(inum) < 0) {
    r = IOERR;
  }
  return r;

}
