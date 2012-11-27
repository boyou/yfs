#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>

#line 10 "../yfs_client.h"
#include "lock_protocol.h"
#include "lock_client.h"
#line 13 "../yfs_client.h"

#line 21 "../yfs_client.h"
class yfs_client {
  extent_client *ec;
#line 24 "../yfs_client.h"
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, FBIG, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    unsigned long long inum;
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);
#line 50 "../yfs_client.h"
  inum ilookup(inum di, std::string name);
#line 53 "../yfs_client.h"
  lock_client *lc;
#line 55 "../yfs_client.h"
 public:
#line 59 "../yfs_client.h"

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);
#line 68 "../yfs_client.h"
  int readdir(inum, unsigned long long ind, 
	      dirent &);
  int lookup(inum, std::string name, inum &);
  int create(inum, std::string name, int dir, 
	     inum &);
#line 75 "../yfs_client.h"
  int setsize(inum, unsigned long long sz);
  int readfile(inum, unsigned long long off, unsigned int n,
              std::string &);
  int writefile(inum, unsigned long long off, std::string);
#line 81 "../yfs_client.h"
  int unlink(inum inum, std::string name);
#line 83 "../yfs_client.h"
};

#endif 
