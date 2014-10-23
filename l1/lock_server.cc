// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
  pthread_mutex_init(&mutex, NULL);
}

lock_server::~lock_server()
{
  for (std::map <lock_protocol::lockid_t, pthread_cond_t>::iterator it = lock_cond.begin();
          it != lock_cond.end(); ++it)
  {
    pthread_cond_destroy(&(it->second));
  }
  pthread_mutex_destroy(&mutex);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}


lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
  fprintf(stderr, "client %d is asking for lock %d\n", clt, lid);
  pthread_mutex_lock(&mutex);
  if (lock_cnt.find(lid) == lock_cnt.end())
  {
      lock_cond[lid] = PTHREAD_COND_INITIALIZER;
  }
  lock_cnt[lid] += 1;
  while (lock_held[lid])
  {
      pthread_cond_wait(&lock_cond[lid], &mutex);
  }
  fprintf(stderr, "lock %d granted to client %d\n", lid, clt);
  lock_held[lid] = true;
  lock_cnt[lid] -= 1;
  ++nacquire;
  pthread_mutex_unlock(&mutex);
  return lock_protocol::OK;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  fprintf(stderr, "client %d is releasing lock %d\n", clt, lid);
  pthread_mutex_lock(&mutex);
  fprintf(stderr, "lock %d released by client %d\n", lid, clt);
  lock_held[lid] = false;
  if (lock_cnt[lid] > 0)
  {
      pthread_cond_signal(&lock_cond[lid]);
  }
  pthread_mutex_unlock(&mutex);
  return lock_protocol::OK;
}

lock_protocol::status
lock_server::subscribe(int clt, lock_protocol::lockid_t lid, int &r)
{
    return lock_protocol::OK;
}
