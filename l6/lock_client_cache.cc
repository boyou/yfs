// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>



static void *
releasethread(void *x)
{
  lock_client_cache *cc = (lock_client_cache *) x;
  cc->releaser();
  return 0;
}

int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // assert(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  /* register RPC handlers with rlsrpc */
  pthread_mutex_init(&lock_cache_mutex, NULL);
  xid = 0;
  rls = new lock_reverse_server(this);
  rlsrpc->reg(rlock_protocol::revoke, rls, &lock_reverse_server::revoke);
  rlsrpc->reg(rlock_protocol::retry, rls, &lock_reverse_server::retry);
  if (subscribe(id) != lock_protocol::OK) {
    printf("%s: registering with lock server failed\n", id.c_str());
  }
  assert(pthread_cond_init(&release_cond, NULL) == 0);  
  pthread_t th;
  int r = pthread_create(&th, NULL, &releasethread, (void *) this);
  assert (r == 0);
}


void
lock_client_cache::releaser()
{

  // This method should be a continuous loop, waiting to be notified of
  // freed locks that have been revoked by the server, so that it can
  // send a release RPC.


  bool again = false;
  pthread_mutex_lock(&lock_cache_mutex);
  while (1) {
    if (!again) pthread_cond_wait(&release_cond, &lock_cache_mutex);
    again = false;
    printf("releaser: run %s\n", id.c_str());
    for (std::list<lock_protocol::lockid_t>::iterator i = releaselist.begin();
	 i != releaselist.end(); i++) {
      lock_entry *l = lock_cache[*i];
      if (l == 0) {
	assert(0);
	continue;
      } else if (l->state == RELEASING) {
	printf("releaser: release %016llx %s(%llu)\n", l->lid, id.c_str(), 
	       l->xid);
	releaselist.erase(i);	 
	flush(l);
	again = true;  // flush gives up lock, so list may have been modified
	printf("releaser: start from beginning again %s\n", id.c_str());
	break;
      } else {
	printf("releaser: skip release %016llx %s(%llu) state %d\n", l->lid,
	       id.c_str(), l->xid, l->state);
      }
    }
    printf("releaser: done run %s\n", id.c_str());
  }
  pthread_mutex_unlock(&lock_cache_mutex);
}


void
lock_client_cache::flush(lock_entry *l)
{
  pthread_mutex_unlock(&lock_cache_mutex);
  dorelease(l->lid, id, l->xid);
  pthread_mutex_lock(&lock_cache_mutex);
  if (l->threads.size() > 0) {
    l->state = ACQUIRING;
    pthread_cond_broadcast(&l->cond);
  } else {
    l->state = UNUSED;
  }
}

void
lock_client_cache::addrelease(lock_protocol::lockid_t lid)
{
  for (std::list<lock_protocol::lockid_t>::iterator i = releaselist.begin();
       i != releaselist.end(); i++) {
    if (*i == lid) {
      printf("addrelease: %s: %016llx already on list\n", id.c_str(), lid);
      return;
    }
  }
  printf("addrelease: %s : %016llx to releaser\n", id.c_str(), lid);
  releaselist.push_back(lid);
  pthread_cond_signal(&release_cond);
}

bool
lock_client_cache::isrevoked(lock_entry *l)
{
  for (std::list<lock_protocol::lockid_t>::iterator i = releaselist.begin();
       i != releaselist.end(); i++) {
    printf("isrevoked: %016llx release %016llx %s(%llu)\n", *i,
	   l->lid, id.c_str(), l->xid);
    if (*i == l->lid)
      return true;
  }
  return false;
}


lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  pthread_mutex_lock(&lock_cache_mutex);
  tid_t tid = (tid_t) pthread_self();
  int ret = lock_protocol::OK;
  lock_entry *l = lock_cache[lid];

  if (l == 0) {
    l = new lock_entry(lid);
    lock_cache[lid] = l;
  }
  if (l->state == UNUSED)  l->state = ACQUIRING;
  l->threads.push_back(tid);
  printf("acquire: %lu : %s(%llu) lock %016llx front %lu %d\n", tid, id.c_str(), 
	 l->xid, lid,  l->threads.front(), l->state);
  while (l->threads.front() != tid || l->state != FREE) {
    if (l->threads.front() == tid && l->state == ACQUIRING) {
      int r = 0;
      l->retry = false;
      l->once = false;
      printf("acquire: %lu: %s(%llu): get lock %016llx state %d\n", tid, 
	     id.c_str(), nextacquire[lid], lid, l->state);
      assert (l->state == ACQUIRING);
      pthread_mutex_unlock(&lock_cache_mutex);
      ret = cl->call(lock_protocol::acquire, lid, id, nextacquire[lid], r);
      pthread_mutex_lock(&lock_cache_mutex);
      if (ret == lock_protocol::OK && l->state == ACQUIRING) {
	printf("acquire: %lu: %s(%llu) received lock %016llx single %d state %d\n", 
	       tid, id.c_str(), nextacquire[lid], lid, r, l->state);
	assert(l);
	assert (l->state == ACQUIRING);
	l->state = FREE;
	l->xid = nextacquire[lid];
	if (!l->once) l->once = r;
	l->retry = false;
	nextacquire[lid] = nextacquire[lid] + 1;
	break;
      } 
      if (l->state == FREE)
	break;
      if (l->retry) {
	printf("acquire: %lu : %s(%llu) : retry for %016llx state %d\n", 
	       tid, id.c_str(), nextacquire[lid], lid, l->state);
	l->retry = false;
	continue;
      }
    }
    printf("acquire: %lu : %s : wait for lock or retry %016llx state %d %llu\n",
	   tid, id.c_str(), lid, l->state, nextacquire[lid]);
    pthread_cond_wait(&l->cond, &lock_cache_mutex);
    printf("acquire: %lu: %s try again %016llx for %llu\n", tid, id.c_str(), 
	   lid, nextacquire[lid]);
  }
  assert(l->state == FREE);
  l->state = LOCKED;
  l->tid = tid;
  printf("acquire: %lu: %s(%llu): grab lock %016llx\n", l->tid, id.c_str(), 
	 l->xid, lid);
  pthread_mutex_unlock(&lock_cache_mutex);
  return ret;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
  pthread_mutex_lock(&lock_cache_mutex);
  tid_t tid = (tid_t) pthread_self();
  lock_entry *l = lock_cache[lid];
  assert (l);
  assert (l->threads.front() == tid);
  assert (l->state == LOCKED);
  l->threads.pop_front();
  if (l->once) {
    printf("release: %lu : %s(%llu) switch %016llx to releasing\n", tid, 
	   id.c_str(), l->xid, lid);
    addrelease(lid);
    l->state = RELEASING;
    pthread_cond_signal(&release_cond);
  } else {
    printf("release: %lu : %s(%llu) switch %016llx to free \n", tid, id.c_str(), 
	   l->xid, lid);
     l->state = FREE;
     if (l->threads.size() > 0) {
       pthread_cond_broadcast(&l->cond);
     }
  }
  pthread_mutex_unlock(&lock_cache_mutex);
  return lock_protocol::OK;

}


lock_protocol::status
lock_client_cache::subscribe(std::string me)
{
  int r;
  return cl->call(lock_protocol::subscribe, me, r);
}

int
lock_client_cache::dorevoke(lock_protocol::lockid_t lid, 
			    lock_protocol::xid_t xid)
{
  pthread_mutex_lock(&lock_cache_mutex);
  int ret = rlock_protocol::OK;
  lock_entry *l = lock_cache[lid];

  assert(l != 0);
  printf("dorevoke: revoke %016llx from %s(%llu) tid %lu xid %llu state %d\n", 
	 lid, id.c_str(), xid, l->tid, l->xid, l->state);
  if (l->xid <= xid) { 
    printf("dorevoke: fresh revoke\n");
    if (l->state == FREE)
	addrelease(lid);
    l->once = true;
    if (l->state == FREE) {
      l->state = RELEASING;
    }
  } else {
    printf("dorevoke: old revoke\n");
  }
  pthread_mutex_unlock(&lock_cache_mutex);
  return ret;
}

int
lock_client_cache::doretry(lock_protocol::lockid_t lid, 
			   lock_protocol::xid_t xid)
{
  pthread_mutex_lock(&lock_cache_mutex);
  lock_entry *l = lock_cache[lid];
  int ret = rlock_protocol::OK;
  if (l == 0) {
    printf("doretry: no entry for %016llx\n", lid);
    assert(0);
  } else {
    if (l->state == ACQUIRING && nextacquire[lid] == xid) {
      printf("doretry: retry lock %016llx id %s(%llu) state %d %llu\n",
	     lid, id.c_str(), xid, l->state, l->xid);
      l->retry = true;
      pthread_cond_broadcast(&l->cond);
    } else {
      printf("doretry: old retry %016llx %s(%llu) tid %lu state %d xid %llu\n", 
	     lid, id.c_str(), xid, l->tid, l->state, l->xid);
    }
  }
  pthread_mutex_unlock(&lock_cache_mutex);
  return ret;
}

void
lock_client_cache::dorelease(lock_protocol::lockid_t lid, std::string id, 
			      lock_protocol::xid_t xid)
{
  int r;
  int ret;

  printf("dorelease: send release %016llx %s(%llu)\n", lid, id.c_str(), xid);
  ret = cl->call(lock_protocol::release, lid, id, xid, r);
  assert (ret == lock_protocol::OK);
}

lock_reverse_server::lock_reverse_server(lock_client_cache *c)
  : lc (c)
{
}

rlock_protocol::status
lock_reverse_server::revoke(lock_protocol::lockid_t lid, lock_protocol::xid_t xid, int &r)
{
  int ret = lc->dorevoke(lid, xid);
  r = 0;
  return ret;
}

rlock_protocol::status
lock_reverse_server::retry(lock_protocol::lockid_t lid, 
			   lock_protocol::xid_t xid, int &r)
{
  int ret = lc->doretry(lid, xid);
  r = 0;
  return ret;
}
