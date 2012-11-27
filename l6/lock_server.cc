// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
  assert(pthread_mutex_init(&server_mutex, NULL) == 0);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}


// gives up server_mutex, and gets it again
void 
lock_server::waitforlock(lock *l)
{
  printf("waitforlock: wait for %016llx\n", l->lid);
  while (l->state == LOCKED) {
    pthread_cond_wait(&(l->cond), &server_mutex);
  }
  l->state = LOCKED;
}

int 
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;

  pthread_mutex_lock(&server_mutex);
  r = 0;
  printf ("acquire: request for %016llx from %d\n", lid, clt);
  lock *l = locks[lid];
  if (l == 0) {
    l = new lock(lid);
    locks[lid] = l;
  }
  waitforlock(l);
  printf("waitforlock: %d acquired %016llx\n", clt, lid);
  pthread_mutex_unlock(&server_mutex);
  return ret;
}

int lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  pthread_mutex_lock(&server_mutex);
  lock *l = locks[lid];
  assert (l);

  printf("release: release %016llx from %d\n", lid, clt);
  r = 0;
  l->state = FREE;
  pthread_cond_broadcast(&(l->cond));
  printf("release: %d released %016llx\n", clt, lid);
  pthread_mutex_unlock(&server_mutex);
  return lock_protocol::OK;
}


