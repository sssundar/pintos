#include <unistd.h>
#include <sys/wait.h>


// int pipe(int filedes[2]);

int main (void)
{
  int n;
  int fd[2];
  pid_t pid;
  char line[MAXLINE];
  FILE *fp;

  if (pipe(fd) < 0)
    err_sys("pipe error");
  if ((pid = fork()) < 0) {
    err_sys("fork error");
  } else if (pid  > 0) { /* parent */
    // close read end
    close(fd[0]);

    while (fgets(line, MAXLINE, fp) != NULL) {
      n = strlen(line);
      if (write(fd[1], line, n) != n)
        err_sys("write error to pipe");
    }
    if (ferror(fp))
      err_sys("fgets error");
    close(fd[1]);

    if (waitpid(pid, NULL, 0) < 0)
      err_sys("waitpid error");
    exit();
    
  } else { /* child */
    // close write end
    close(fd[1]);

    if (fd[O] != STDIN_FILENO) {
      if (dup2(fd[O], STDIN_FILENO) != STDIN_FILENO)
        err_sys("dup2 error to stdin");

      /*get arguments for execl() */
      if ((pager = getenv("PAGER")) == NULL)
        pager = DEF_PAGER;
      
      if ((argvO = strrchr(pager, '/')) !=NULL)
        argvO++; /* step past rightmost slash */
      else
        argvO = pager; /* no slash in pager */
      if (execl(pager, argvO, (char *)0) < 0)
        err_sys("execl error for %s", pager); 
  }
  exit(0);
  
}
