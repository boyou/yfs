// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#line 9 "../lock_server.h"
#include <map>
#line 11 "../lock_server.h"
#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"

class lock_server {

 protected:
  int nacquire;

#line 21 "../lock_server.h"
  enum lstate {
    FREE,
    LOCKED
  };
  struct lock {
    lock(lock_protocol::lockid_t _lid) { 
      assert(pthread_cond_init(&cond, NULL) == 0); 
      lid = _lid; 
      state = FREE;
    }
    pthread_cond_t cond;
    lock_protocol::lockid_t lid;
    lstate state;
  };
  std::map<lock_protocol::lockid_t, lock*> locks;
  pthread_mutex_t server_mutex;
  void waitforlock(lock *l);

 public:
  lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int &);

#line 44 "../lock_server.h"
 public:
  lock_server();
  ~lock_server() {};
  lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &);
};

#endif 







