#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include "parser.c"
#include "parser.h"

#define MAXPATH 260

char *SHELL_ERROR_IDENTIFIER = "cursh";

/**
 * Redirects ifile to STDIN if ifile is not NULL and exists.
 * Redirects ofile to STDOUT, if it is not NULL.
 * If the ofile does not exist, it is created. 
 * If it exists, it is truncated if the append flag is false, 
 * and otherwise appended to.
 * The output file will have 760 permissions requested.
 *
 * @param ifile
 * @param ofile
 * @param append_flag 
 * @returns 0 on success or -1 on any failure. NULL iofiles ensure successes.
 */
int redirection(char *ifile, char *ofile, bool append_flag) {

  int flags = O_RDONLY;            
  bool in_fail = false;
  bool out_fail = false;
  int indesc, outdesc;
  mode_t mode;

  if (ifile != NULL) {    
    indesc = open(ifile, flags);    
    if (indesc == -1) {
      perror(SHELL_ERROR_IDENTIFIER);        
      in_fail = true;
    } else {
      if (dup2(indesc, STDIN_FILENO) == -1) {
        perror(SHELL_ERROR_IDENTIFIER);                
        in_fail = true;
      }      
      close(indesc);
    }
  }

  // 760 permissions requested for redirected output 
  // subject to process permissions umask
  mode = S_IRWXU | S_IRGRP | S_IWGRP; 

  if (!in_fail) {
    
    if (append_flag) {
      flags = O_WRONLY | O_APPEND | O_CREAT;
    } else {
      flags = O_WRONLY | O_TRUNC | O_CREAT;
    }
  
    if (ofile != NULL) {
      outdesc = open(ofile, flags, mode);
      if (outdesc == -1) {
        perror(SHELL_ERROR_IDENTIFIER);
        out_fail = true;          
      } else {
        if (dup2(outdesc, STDOUT_FILENO) == -1) {
          perror(SHELL_ERROR_IDENTIFIER);            
          out_fail = true;
        } 
        close(outdesc);
      }        
    }      

    if (!out_fail) {
      return 0;
    }
  }            

  return -1;    
}

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
  int error_waitpid;

  char *command_name;
  command *comms = NULL;
  command *command_walker;
  int command_length;
  int num_commands; 
  int i;
  
  int cpid; 
  int status;  

  char *myhomedirectory = malloc(sizeof(char) * MAXPATH);
  if (myhomedirectory == NULL) {
    fprintf(stderr, "%s: Could not allocate holder for home directory. Exiting.\n", SHELL_ERROR_IDENTIFIER);
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
      /////////////////////////////////////////////////////
      // Single Command, No Pipes, Possible Redirections.//
      /////////////////////////////////////////////////////
      if (command_length == 1) {
        command_name = comms[i].argv[0];        
        if (strcmp((const char *)command_name, (const char *) "cd") == 0) {
          
          // Change Directory. Redirection ignored.
          if (comms[i].argc > 1) {
            error_chdir = chdir(comms[i].argv[1]);
          } else {            
            error_getenv = getenv("HOME");
            if (error_getenv != NULL) {
              // WARNING: race condition, but we will almost certainly catch the error with chdir
              // if the string gets overwritten halfway.
              myhomedirectory = strcpy(myhomedirectory, (const char *) error_getenv);              
              error_chdir = chdir((const char *) myhomedirectory);               
            } else {
              fprintf(stderr, "%s: Unable to locate your home directory.\n", SHELL_ERROR_IDENTIFIER);   
              break; // return to shell prompt
            }            
          }
          if (error_chdir == 0) {
            // success, run getcwd again for shell prompt as directory has changed.
            getcwd(cwd, sizeof(cwd));
          } else {
            perror(SHELL_ERROR_IDENTIFIER);
            break; // return to shell prompt
          }

        } else if (strcmp((const char *)command_name, (const char *) "exit") == 0) {
          
          // Exit Curiosity Shell. Redirection Ignored.
          exit(EXIT_SUCCESS);

        } else {
          
          // External Single Command. No Pipes, Possible Redirections.
          cpid = fork();
          if (cpid == -1) {            
            perror(SHELL_ERROR_IDENTIFIER); 
            // Done executing this command set, go back to the shell            
            break;
          } 

          if (cpid == 0) {
            // I am the child
            if (redirection(comms[i].ifile, comms[i].ofile,comms[i].append) == 0) {
              // Will not return if successful.                 
              execvp(comms[i].argv[0], comms[i].argv);    
              perror(SHELL_ERROR_IDENTIFIER);           
              exit(EXIT_FAILURE);
            }
            // Our implementation does not allow redirection of STDERR
            // Therefore if we are here, STDERR is the same as our shell,
            // and would be visible to the user.
            fprintf(stderr, "%s: Redirection for %s failed.\n", SHELL_ERROR_IDENTIFIER, comms[i].argv[0]);
            exit(EXIT_FAILURE);
          } else {
            // I am the parent

            // Wait for termination ONLY - not any other state changes.
            error_waitpid = waitpid(cpid, &status, 0);   
  
            if (error_waitpid == -1) {
              perror(SHELL_ERROR_IDENTIFIER); 
              // Stop executing this command set, go back to the shell              
              break;
            }

            /* If child didn't terminate normally, and with a success exit code, it failed.
             We do not notify the user in any of these cases. Someone else ought to have.             
             In a multi-command setting we'd want to only proceed to the next
             command on successful exit. For example: */
            if (WIFEXITED(status)) {
              if (WEXITSTATUS(status) == 0) {
                // Child exited successfully
                // Process Next Command! 
              } else break;
            } else break;                

            break;
          }          

        } 

      }

      // Multi Command Case Handling

    }
    // NO CODE ALLOWED HERE - must go straight to while loop.
  }
}
