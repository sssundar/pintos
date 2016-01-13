#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "parser.c"
#include "parser.h"

#define MAXPATH 260

int main(void)
{
  // Max size is 1 KiB
  char str[1024];
  
  // This gets the current username
  char *login;
  login = getlogin();
  // This is the current working directory
  char cwd[MAXPATH];
  getcwd(cwd, sizeof(cwd));

  char *error_fgets;
  int error_chdir;
  
  char *command_name;
  command *comms;
  command *command_walker;
  int command_length;
  int num_commands; 
  int i;
  
  while(1){
    free_commands(comms);
    
    printf("%s:%s> ", login, cwd);
    error_fgets = fgets(str, 1024, stdin);
    if (error_fgets == NULL) {
      exit();
    }
    command_length = strlen((const char *) str);
    str[command_length-1] = '\0';
    comms = get_commands(str);
    
    // count number of commands, num_commands
    command_length = 0;
    command_walker = comms;
    while (command_walker->argv != NULL) {
      command_length += 1;
      command_walker += 1;
    }
    for (i = 0; i < command_length; i++) {
      if (command_length == 1) {
        command_name = comms[i]->argv[0];

        if (command_name == "cd") {
          if (comms[i]->argc > 1) {
            error_chdir = chdir(comms[i]->argv[1]);
          } else { 
            error_chdir = chdir(NULL); 
          }
          if (error_chdir == 0) {
            // success, run getcwd again
            getcwd(cwd, sizeof(cwd));
          } else {
            perror("cursh: ");
            break;
          }
        } else if (command_name == "exit") {
          exit(EXIT_SUCCESS);
        } else {
          continue;
        }
        
      }
    }
  }
}
