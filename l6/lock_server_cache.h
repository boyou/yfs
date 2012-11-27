#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#line 6 "../lock_server_cache.h"
#include <map>
#line 8 "../lock_server_cache.h"
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"
#line 14 "../lock_server_cache.h"

#line 18 "../lock_server_cache.h"

#line 20 "../lock_server_cache.h"

struct reqid_t {
  reqid_t (std::string i, lock_protocol::xid_t x) {
    id = i;
    xid = x;
  }
  std::string id;
  lock_protocol::xid_t xid;
};

inline bool operator==(reqid_t a, reqid_t b) {
  return a.id == b.id && a.xid == b.xid;
}

inline bool operator!=(reqid_t a, reqid_t b) {
  return a.id != b.id || a.xid != b.xid;
}

class rlock_client {
 private:
  rpcc *cl;
 public:
  rlock_client(sockaddr_in dst);
  rlock_protocol::status revoke(lock_protocol::lockid_t lid, 
				lock_protocol::xid_t);
  rlock_protocol::status retry(lock_protocol::lockid_t lid,
			       lock_protocol::xid_t);
};

#line 50 "../lock_server_cache.h"

#line 54 "../lock_server_cache.h"
class lock_server_cache {
#line 56 "../lock_server_cache.h"
 private:
#line 58 "../lock_server_cache.h"
  int nacquire;
  enum lstate {
    FREE,
    OWNED
  };
  struct lock {
    lock(lock_protocol::lockid_t _lid) { 
      lid = _lid; 
      state = FREE;
    }
    std::list<reqid_t> requests;
    std::map<std::string,lock_protocol::xid_t> nextacquire;
    lock_protocol::lockid_t lid;
    lstate state;
  };
  std::map<lock_protocol::lockid_t, lock*> locks;
  pthread_mutex_t server_mutex;
#line 80 "../lock_server_cache.h"
  std::map<std::string, rlock_client*> rlc;
  std::list<lock_protocol::lockid_t> revokelist;
  std::list<lock_protocol::lockid_t> retrylist;
  pthread_cond_t revoke_cond;
  pthread_cond_t retry_cond;
  void addrevoke(lock_protocol::lockid_t lid);
  bool freshreq(lock_protocol::lockid_t lid, reqid_t t);
#line 88 "../lock_server_cache.h"
 public:
#line 92 "../lock_server_cache.h"
  lock_server_cache();
#line 94 "../lock_server_cache.h"
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  void revoker();
  void retryer();
#line 103 "../lock_server_cache.h"
  int acquire(lock_protocol::lockid_t, std::string id, 
	      lock_protocol::xid_t, int &);
  int release(lock_protocol::lockid_t, std::string id, lock_protocol::xid_t,
	      int &);
  int subscribe(std::string id, int &r);
#line 109 "../lock_server_cache.h"
};

#endif
