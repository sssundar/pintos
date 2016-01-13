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
  char *error_getenv;
  int error_chdir;
  
  char *command_name;
  command *comms = NULL;
  command *command_walker;
  int command_length;
  int num_commands; 
  int i;
  
  char *myhomedirectory = malloc(sizeof(char) * MAXPATH);
  if (myhomedirectory == NULL) {
    printf("cursh: Could not allocate holder for home directory. Exiting.\n");
    exit(EXIT_FAILURE);
  }

  while(1){
    free_commands(comms);
    
    printf("%s:%s> ", login, cwd);
    error_fgets = fgets(str, 1024, stdin);
    if (error_fgets == NULL) {
      // Either because of EOF or STDIN read error. Can't tell, and 
      // correct behavior is to terminate so:
      exit(EXIT_SUCCESS);
    }
    command_length = strlen((const char *) str);
    
    if (command_length == 0) {
      comms = NULL;
      continue;
    }

    str[command_length-1] = '\0';
    comms = get_commands(str);           
    
    if (comms == NULL) {
      continue;
    }

    // count number of commands, num_commands
    command_length = 0;
    command_walker = comms;
    while (command_walker->argv != NULL) {
      command_length += 1;
      command_walker += 1;
    }
    for (i = 0; i < command_length; i++) {
      // Single Command, No Pipes, Possible Redirection
      if (command_length == 1) {
        command_name = comms[i].argv[0];        
        if (strcmp((const char *)command_name, (const char *) "cd") == 0) {
          if (comms[i].argc > 1) {
            error_chdir = chdir(comms[i].argv[1]);
          } else {            
            error_getenv = getenv("HOME");
            if (error_getenv != NULL) {
              // WARNING: race condition, but we will catch the error with chdir, most likely
              myhomedirectory = strcpy(myhomedirectory, (const char *) error_getenv);              
              error_chdir = chdir((const char *) myhomedirectory);               
            } else {
              printf("cursh: Unable to locate your home directory.\n");   
              break;
            }            
          }
          if (error_chdir == 0) {
            // success, run getcwd again
            getcwd(cwd, sizeof(cwd));
          } else {
            perror("cursh: ");
            break;
          }
        } else if (strcmp((const char *)command_name, (const char *) "exit") == 0) {
          exit(EXIT_SUCCESS);
        } else {
          // TODO: Start from here.
          continue;
        }
        
      }
    }
    // NO CODE ALLOWED HERE - must go straight to while loop.
  }
}
