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

#line 50 "../lock_server_cache.h"

#line 54 "../lock_server_cache.h"
class lock_server_cache {
#line 56 "../lock_server_cache.h"
 private:
#line 88 "../lock_server_cache.h"
 public:
#line 92 "../lock_server_cache.h"
  lock_server_cache();
#line 94 "../lock_server_cache.h"
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  void revoker();
  void retryer();
#line 109 "../lock_server_cache.h"
};

#endif
