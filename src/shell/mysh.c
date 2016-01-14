#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>

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

/*
 * function fork_yourself:
 * @param IS_INTERNAL: bool flag is it an internal command?
 * @param IS_ALONE: bool flag is it a single command?
 * @param *IS_PARENT: pointer will be set if fork results in a parent.
 * @param *IS_CHILD: pointer will be set if fork results in a child.
 * @return bool: returns -1 for an error, 0 if forked successfully.
 */

bool fork_yourself(bool IS_INTERNAL, bool IS_ALONE, bool *IS_PARENT, bool *IS_CHILD) {
  // First determine if we even need to fork:

  if (IS_INTERNAL && IS_ALONE){
    // Don't fork.
    // Set flags regardless
    *IS_PARENT = true;
    *IS_CHILD = false;

    return -1;
  } else{

    pid_t fpid;
    fpid = fork();
    
 
   if (fpid == -1) {            
      perror(SHELL_ERROR_IDENTIFIER); 
      // Done executing this command set, go back to the shell
      *IS_PARENT = true;
      *IS_CHILD = false;
      return fpid;
    }
    
    if (fpid == 0) {
      // I am the child
      // Set child and parent flags
      *IS_CHILD = true;
      *IS_PARENT = false;
      
      if (redirection(comms[i].ifile, comms[i].ofile, comms[i].append) == 0) {
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
    }
    
  }
}




int main(void)
{
  // Helpful Flags
  bool IS_NOT_ALONE, IS_ALONE, IS_INTERNAL, IS_EXTERNAL, IS_CD, IS_EXIT;
  bool IS_PARENT, IS_CHILD, IS_REDIRECTED, IS_FIRST, IS_LAST, IS_MIDDLE;

  // Pipe Holders
  int *pipe_left;
  int *pipe_right; 
  int *pipe_temp; 
  
  // Holder for command string from STDIO.
  // Max size is 1 KiB
  char str[1024];
  
  // This gets the current username
  char *login;
  login = getlogin();

  // This is the current working directory
  // This will be updated on cd commands run internally
  char cwd[MAXPATH];
  getcwd(cwd, sizeof(cwd));

  // Error Flag Holders
  char *error_fgets;  // fgets return flag
  char *error_getenv; // getenv return flag
  int error_chdir;    // chdir return flag
  int error_waitpid;  // waitpid return flag
  int status;         // waitpid child status holder
  int error_pipe;     // pipe return flag
  int cpid;           // fork return flag
  
  // for execution & internal/external checking
  char *command_name;

  int command_length;  
  // parsed command holder + pointer for counting number of commands
  command *comms = NULL;
  command *command_walker;

  // for loop iteration variable
  int i;
  
  


  // This will never be freed until we exit (and then by default).
  pipe_left = malloc(2*sizeof(int));
  pipe_right = malloc(2*sizeof(int));
  if ((pipe_left == NULL) || (pipe_right == NULL)) {
    fprintf(stderr, "%s: Could not allocate holder for potential pipes. Exiting.\n", SHELL_ERROR_IDENTIFIER);
    exit(EXIT_FAILURE);
  }
  // *(pipe_left) = 1;
  // *(pipe_left+1) = 2;
  // *(pipe_right) = 3;
  // *(pipe_right+1) = 4;

  // Holds $HOME result from getenv call.
  // This will never be freed until we exit (and then by default).
  char *myhomedirectory = malloc(sizeof(char) * MAXPATH);  
  if (myhomedirectory == NULL) {
    fprintf(stderr, "%s: Could not allocate holder for home directory. Exiting.\n", SHELL_ERROR_IDENTIFIER);
    exit(EXIT_FAILURE);
  }


  /* 
    Curiosity Shell Control Flow
  */
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
    
    // Create another prompt in the next iteration
    if (comms == NULL) {
      continue;
    }

    // count number of commands, command_length
    command_length = 0;
    command_walker = comms;
    while (command_walker->argv != NULL) {
      command_length += 1;
      command_walker += 1;
    }

    /* Handle Latest Set of Commands */
    for (i = 0; i < command_length; i++) {
      /*
        Set Flags for THIS command
        IS_FIRST, IS_MIDDLE, IS_LAST, IS_ALONE, IS_NOT_ALONE, IS_INTERNAL, IS_EXTERNAL, IS_CD, IS_EXIT
      */

      /*
        Pipe & Fork Handler,
        Set IS_PARENT, IS_CHILD
      */       

      /*
        Redirection Handler      
        Set IS_REDIRECTED flag
      */       

      /*
        Run/Wait Handler
      */       

      /*
        Parent STDIO Reset
        In case of Single Internal Command with Redirection
      */       
    }
