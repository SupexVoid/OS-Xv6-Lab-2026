#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
sieve(int input_fd)
{
  int prime;
  int value;
  int next_pipe[2];

  if(read(input_fd, &prime, sizeof(prime)) != sizeof(prime)){
    close(input_fd);
    exit(0);
  }
  printf("prime %d\n", prime);

  if(pipe(next_pipe) < 0){
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(input_fd);
    close(next_pipe[1]);
    sieve(next_pipe[0]);
  }

  close(next_pipe[0]);
  while(read(input_fd, &value, sizeof(value)) == sizeof(value)){
    if(value % prime != 0)
      write(next_pipe[1], &value, sizeof(value));
  }
  close(input_fd);
  close(next_pipe[1]);
  wait(0);
  exit(0);
}

int
main(int argc, char *argv[])
{
  int first_pipe[2];

  if(pipe(first_pipe) < 0){
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    close(first_pipe[1]);
    sieve(first_pipe[0]);
  }

  close(first_pipe[0]);
  for(int value = 2; value <= 35; value++)
    write(first_pipe[1], &value, sizeof(value));
  close(first_pipe[1]);
  wait(0);
  exit(0);
}
