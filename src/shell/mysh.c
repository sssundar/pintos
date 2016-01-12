#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#define MAXPATH 1000

int main(void)
{
  // Max size is 1 KiB
  char str[1024];
  char tokens[1024];
  
  // This gets the current username
  char *login;
  login = getlogin();
  // This is the current working directory
  char cwd[MAXPATH];
  getcwd(cwd, sizeof(cwd));

  while(1){
    printf("%s:%s> ", login, cwd);
    fgets(str, 1024, stdin);
    
    // If too many characters are passed into the command line
    /**
    if (str[1023] == 0 && str[1022] != '\n')
      printf("ERROR: %d\n", E2BIG);
    **/

    // Have Hamik's function parse what is in str.
    //    char *tokens[] = hamik_function;
    
    //execve(tokens[0], tokens, tokens + 1);
  }
  //  printf ("Curiosity$ %s", str);
}
