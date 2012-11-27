// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

static void *
revokethread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->revoker();
  return 0;
}

static void *
retrythread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->retryer();
  return 0;
}

lock_server_cache::lock_server_cache()
{
  pthread_t th;
  assert(pthread_mutex_init(&server_mutex, NULL) == 0);
  assert(pthread_cond_init(&revoke_cond, NULL) == 0);  
  assert(pthread_cond_init(&retry_cond, NULL) == 0);  
  int r = pthread_create(&th, NULL, &revokethread, (void *) this);
  assert (r == 0);
  r = pthread_create(&th, NULL, &retrythread, (void *) this);
  assert (r == 0);
}

void
lock_server_cache::revoker()
{

  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock

  bool again = false;
  pthread_mutex_lock(&server_mutex);
  while (1) {
    if (!again) pthread_cond_wait(&revoke_cond, &server_mutex);
    again = false;
    printf("revoker start\n");
    for (std::list<lock_protocol::lockid_t>::iterator i = revokelist.begin();
        i != revokelist.end(); i++) {
      lock *l = locks[*i];
      assert(l);
      reqid_t f = l->requests.front();
      revokelist.pop_front();
      if (l->requests.size() > 1) {
        printf ("revoker: send revoke for %016llx to %s(%llu)\n", l->lid,
            f.id.c_str(), f.xid);
        pthread_mutex_unlock(&server_mutex);
  // XXX slightly risky to look in cdst after releasing lock
        rlock_protocol::status ret = rlc[f.id]->revoke(l->lid, f.xid);
        assert(ret == rlock_protocol::OK);
        pthread_mutex_lock(&server_mutex);
        again = true;
        printf("revoker start again\n");
        break;
      }    
    }
    printf("revoker done\n");
  }
  pthread_mutex_unlock(&server_mutex);
}


bool
lock_server_cache::freshreq(lock_protocol::lockid_t lid, reqid_t t)
{
  lock *l = locks[lid];
  assert(l);
  for (std::list<reqid_t>::iterator i = l->requests.begin(); 
       i != l->requests.end(); i++) {
    if (*i == t) return false;
  }
  return true;
}

void
lock_server_cache::addrevoke(lock_protocol::lockid_t lid)
{
  for (std::list<lock_protocol::lockid_t>::iterator i = revokelist.begin();
       i != revokelist.end(); i++) {
    if (*i == lid) return;
  }
  revokelist.push_back(lid);
  pthread_cond_signal(&revoke_cond);
}


void
lock_server_cache::retryer()
{

  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.

  bool again = false;
  pthread_mutex_lock(&server_mutex);
  while (1) {
    if (!again)  pthread_cond_wait(&retry_cond, &server_mutex);
    again = false;
    printf("retryer runs\n");
    for (std::list<lock_protocol::lockid_t>::iterator i = retrylist.begin();
        i != retrylist.end(); i++) {
      lock *l = locks[*i];
      assert(l);
      reqid_t f = l->requests.front();
      retrylist.pop_front();
      if (l->requests.size() > 0) {
        printf ("retryer: send retry %016llx to %s(%llu) state %d\n", 
            l->lid, f.id.c_str(), f.xid, l->state);
        pthread_mutex_unlock(&server_mutex);
  // XXX looking at rlc without mutex; it is ok, but ...
        lock_protocol::status ret = rlc[f.id]->retry(l->lid, f.xid);
        assert (ret == lock_protocol::OK);
        pthread_mutex_lock(&server_mutex);
        again = true;
        printf("run retryer again\n");
        break;
      }
    }
    printf("retryer done\n");
  }
  pthread_mutex_unlock(&server_mutex);
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
             lock_protocol::xid_t xid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  reqid_t t(id, xid);
  lock *l;

  pthread_mutex_lock(&server_mutex);
  r = 0;
  printf ("acquire_cache: %s(%llu) : request for %016llx\n", id.c_str(), xid,
	  lid);
  l = locks[lid];
  if (l == 0) {
    l = new lock(lid);
    locks[lid] = l;
  }

  if (freshreq(lid, t)) l->requests.push_back(t);

  reqid_t f = l->requests.front(); 
  printf("acquire_cache: %s(%llu) : request %016llx front %s(%llu) state %d\n",
	 id.c_str(), xid, lid, f.id.c_str(), f.xid, l->state);

  if (l->state == FREE && t == f) {
    l->state = OWNED;
    r = l->requests.size() > 1;
    printf ("acquire_cache: %s(%llu) : give %016llx (once? %d)\n", id.c_str(),
	    xid, lid, r);
  } else {
    if (l->state == FREE) {  // waiting for front to acquire?
      printf ("acquire_cache: request for %016llx front  %s(%llu)\n", 
	      lid, f.id.c_str(), f.xid);
    }
    addrevoke(lid);
    ret = lock_protocol::RETRY;
  }
  pthread_mutex_unlock(&server_mutex);
  return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         lock_protocol::xid_t xid, int &r)
{
  lock *l;
  reqid_t t(id, xid);
  reqid_t f("", 0);
  lock_protocol::status ret = lock_protocol::OK;

  pthread_mutex_lock(&server_mutex);
  l = locks[lid];
  assert(l);

  printf("release: %s(%llu) release %016llx front %s(%llu)\n", id.c_str(), xid, 
   lid, l->requests.front().id.c_str(), l->requests.front().xid);
  if (l->requests.size() == 0 || l->requests.front() != t) {
    printf("release: possibly a dup from %s(%llu) because of view change\n", 
     id.c_str(), xid);
  } else {
    r = 0;
    assert(l->state == OWNED);
    l->state = FREE;
    l->requests.pop_front();
    printf("release: %s(%llu) released %016llx %zu\n", id.c_str(), xid, 
     lid, l->requests.size());
  }
  // even in the case of an old release, send retry, because it might
  // have gotten lost during a view change.
  if (l->requests.size() > 0) {
    retrylist.push_back(lid);
    pthread_cond_signal(&retry_cond);
  }
  pthread_mutex_unlock(&server_mutex);
  return ret;
}

int
lock_server_cache::subscribe(std::string id, int &r)
{
  struct sockaddr_in *dst = new sockaddr_in;

  pthread_mutex_lock(&server_mutex);

  printf ("client %s subscribes\n", id.c_str());
  make_sockaddr(id.c_str(), dst);
  rlc[id] = new rlock_client(*dst);
  pthread_mutex_unlock(&server_mutex);
  return lock_protocol::OK;
}





lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  printf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

rlock_client::rlock_client(sockaddr_in dst)
{
  cl = new rpcc(dst);
  cl->bind(rpcc::to(1000));
}

int 
rlock_client::revoke(lock_protocol::lockid_t lid, lock_protocol::xid_t xid)
{
  int r;
  int ret = cl->call(rlock_protocol::revoke, lid, xid, r);
  printf ("revoke: returns %d\n", ret);
  return ret;
}

int 
rlock_client::retry(lock_protocol::lockid_t lid, lock_protocol::xid_t xid)
{
  int r;
  printf ("retry: send retry for lock %016llx\n", lid);
  int ret = cl->call(rlock_protocol::retry, lid, xid, r);
  printf ("retry: returns %d\n", ret);
  return ret;
}

