#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>

static int nthread = 1;

struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
  bstate.round = 0;
}

static void 
barrier()
{
  int this_round;

  assert(pthread_mutex_lock(&bstate.barrier_mutex) == 0);
  this_round = bstate.round;
  bstate.nthread++;

  if(bstate.nthread == nthread){
    bstate.nthread = 0;
    bstate.round++;
    assert(pthread_cond_broadcast(&bstate.barrier_cond) == 0);
  } else {
    // The loop handles both spurious wakeups and a fast thread entering the
    // next generation before every waiter from this one has resumed.
    while(this_round == bstate.round)
      assert(pthread_cond_wait(&bstate.barrier_cond,
                               &bstate.barrier_mutex) == 0);
  }

  assert(pthread_mutex_unlock(&bstate.barrier_mutex) == 0);
}

static void *
thread(void *xa)
{
  int i;

  (void)xa;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    assert (i == t);
    barrier();
    usleep(rand() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  int i;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srand(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread,
                          (void *)(intptr_t)i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  assert(pthread_mutex_destroy(&bstate.barrier_mutex) == 0);
  assert(pthread_cond_destroy(&bstate.barrier_cond) == 0);
  free(tha);
  printf("OK; passed\n");
}
