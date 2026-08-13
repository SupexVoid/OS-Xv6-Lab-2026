#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

static void
run_line(char *line, int length, int argc, char *argv[])
{
  char *exec_argv[MAXARG];
  int count = 0;

  for(int i = 1; i < argc; i++){
    if(count >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      exit(1);
    }
    exec_argv[count++] = argv[i];
  }

  line[length] = 0;
  char *p = line;
  while(*p){
    while(*p == ' ' || *p == '\t')
      *p++ = 0;
    if(*p == 0)
      break;
    if(count >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      exit(1);
    }
    exec_argv[count++] = p;
    while(*p && *p != ' ' && *p != '\t')
      p++;
  }
  exec_argv[count] = 0;

  if(count == argc - 1)
    return;

  int pid = fork();
  if(pid < 0){
    fprintf(2, "xargs: fork failed\n");
    exit(1);
  }
  if(pid == 0){
    exec(exec_argv[0], exec_argv);
    fprintf(2, "xargs: exec %s failed\n", exec_argv[0]);
    exit(1);
  }
  wait(0);
}

int
main(int argc, char *argv[])
{
  char line[512];
  int length = 0;
  char c;
  int n;

  if(argc < 2){
    fprintf(2, "usage: xargs command [arguments ...]\n");
    exit(1);
  }

  while((n = read(0, &c, 1)) > 0){
    if(c == '\n' || c == '\r'){
      run_line(line, length, argc, argv);
      length = 0;
    } else {
      if(length >= sizeof(line) - 1){
        fprintf(2, "xargs: input line too long\n");
        exit(1);
      }
      line[length++] = c;
    }
  }
  if(n < 0){
    fprintf(2, "xargs: read failed\n");
    exit(1);
  }
  if(length > 0)
    run_line(line, length, argc, argv);

  exit(0);
}
