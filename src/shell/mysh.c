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
 * @returns bool true on success or false on IO failure. 
 */
bool redirection(char *ifile, char *ofile, bool append_flag) {

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
      return true;
    }
  }            

  return false;
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
/*
 * Swap pipe_left and pipe_right pointers for readability of code if argument is 0
 * Pipe to pipe_right if argument is 2, then check for pipe errors if argument is 1
 * @param int **ptr_pipe_left pointer to a pointer to an array capable of holding two integer file descriptors
 * @param int **ptr_pipe_right pointer to a pointer to an array capable of holding two integer file descriptors
 * @param int flag 0, 1 are interpreted as described above.
 * @returns false on any error, and true on total success
 */
bool pipe_creation_handler(int **ptr_pipe_left, int **ptr_pipe_right, int flag) {
  int error_pipe;     // pipe return flag
  int *ptr_pipe_temp;     // pointer swap holder
  switch (flag) {
    case 0:
        ptr_pipe_temp = *ptr_pipe_left;
        *ptr_pipe_left = *ptr_pipe_right;
        *ptr_pipe_right = ptr_pipe_temp;
      break;
    case 1:
      error_pipe = pipe(*ptr_pipe_right);
      if (error_pipe >= 0) {
        break;
      }
    default:
      return false;    
  }  
  return true;
}

int main(void) {
  // Helpful Flags
  bool IS_NOT_ALONE, IS_ALONE, IS_INTERNAL, IS_EXTERNAL, IS_CD, IS_EXIT;
  bool IS_PARENT, IS_CHILD, IS_REDIRECTED, IS_FIRST, IS_LAST, IS_MIDDLE;

  // Pipe Holders
  int *pipe_left;
  int *pipe_right;   
  
  // Holders for dup'd STDIN, OUT in case of redirected internal commands
  int ORIGINAL_STDIN, ORIGINAL_STDOUT; 

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
    
    // Set IS_ALONE, IS_NOT_ALONE Flags
    IS_ALONE = false;
    IS_NOT_ALONE = false;
    if (command_length == 1) IS_ALONE = true;
    if (command_length > 1) IS_NOT_ALONE = true;

    /* Handle Latest Set of Commands */
    for (i = 0; i < command_length; i++) {
      /*
        Set Flags for THIS command
        IS_FIRST, IS_MIDDLE, IS_LAST, IS_INTERNAL, IS_EXTERNAL, IS_CD, IS_EXIT
      */
      
      command_name = comms[i].argv[0];        

      IS_FIRST = false;
      IS_MIDDLE = false;
      IS_LAST = false;

      if (i == 0) {
        IS_FIRST = true;
      }
      if (i == (command_length-1)) {
        IS_LAST = true;
      }
      if ((!IS_FIRST) && (!IS_LAST)) {
        IS_MIDDLE = true;
      }

      IS_INTERNAL = false;
      IS_EXTERNAL = false;
      IS_CD = false;
      IS_EXIT = false;
      if (strcmp((const char *)command_name, (const char *) "cd") == 0) IS_CD = true;
      if (strcmp((const char *)command_name, (const char *) "exit") == 0) IS_EXIT = true;
      if (IS_CD || IS_EXIT) IS_INTERNAL = true;
      if (IS_INTERNAL) IS_EXTERNAL = false;

      /*
        Pipe & Fork Handler (fork_yourself), 
        Set IS_PARENT, IS_CHILD in fork_yourself, and use its return value to determine error
      */

      if (IS_FIRST && IS_ALONE) {
        // No piping        
        
        // Fork and set IS_PARENT, IS_CHILD
        if (!fork_yourself(IS_INTERNAL, IS_ALONE, &IS_PARENT, &IS_CHILD)) { 
          break; 
        }

        // No pipe handling

      } else if (IS_FIRST && IS_NOT_ALONE) {
        // pipe to pipe_right, check for pipe errors & return to prompt if errors found.
        if (!pipe_creation_handler(&pipe_left, &pipe_right, 1)) {
          fprintf(stderr, "%s: Right-pipe creation error for command %s.\n", SHELL_ERROR_IDENTIFIER, command_name);
          break;
        }
        
        // Fork and set IS_PARENT, IS_CHILD
        if (!fork_yourself(IS_INTERNAL, IS_ALONE, &IS_PARENT, &IS_CHILD)) { 
          break; 
        }

        // parent closes pipe_right_write
        // errors reported by close are inconsequential
        if (IS_PARENT) {
          close(pipe_right[1]);
        } 
        
        // child closes pipe_right_read
        // child duplicates pipe_right_write to stdout, then closes it.          
        // errors reported by close are inconsequential
        if (IS_CHILD) {
          close(pipe_right[0]);
          if (dup2(pipe_right[1], STDOUT_FILENO) == -1) {
            perror(SHELL_ERROR_IDENTIFIER);
            exit(EXIT_FAILURE);
          }
          close(pipe_right[1]);
        }
        
      } else if (IS_LAST && IS_NOT_ALONE) {
        // swap pipe_left/pipe_right
        // no piping        
        pipe_creation_handler(&pipe_left, &pipe_right, 0);                            
        
        // Fork and set IS_PARENT, IS_CHILD        
        if (!fork_yourself(IS_INTERNAL, IS_ALONE, &IS_PARENT, &IS_CHILD)) { 
          break; 
        }

        // parent closees pipe_left_read
        // errors reported by close are inconsequential
        if (IS_PARENT) {
          close(pipe_left[0]);
        }

        // child duplicates pipe_left_read to its stdin, then closes it
        // errors reported by close are inconsequential
        if (IS_CHILD) {
          if (dup2(pipe_left[0], STDIN_FILENO) == -1) {
            perror(SHELL_ERROR_IDENTIFIER);
            exit(EXIT_FAILURE);
          }
          close(pipe_left[0]);
        }

      } else if (IS_MIDDLE && IS_NOT_ALONE) {
        // swap pipe_left/pipe_right
        pipe_creation_handler(&pipe_left, &pipe_right, 0);                            
        // pipe to pipe_right, check for pipe errors & return to prompt if errors found.        
        if (!pipe_creation_handler(&pipe_left, &pipe_right, 1)) {
          fprintf(stderr, "%s: Right-pipe creation error for command %s.\n", SHELL_ERROR_IDENTIFIER, command_name);
          break;
        }        

        // Fork and set IS_PARENT, IS_CHILD
        if (!fork_yourself(IS_INTERNAL, IS_ALONE, &IS_PARENT, &IS_CHILD)) { 
          break; 
        }

        // parent closes pipe_left_read and pipe_right_write
        // errors reported by close are inconsequential
        if (IS_PARENT) {
          close(pipe_left[0]);
          close(pipe_right[1]);
        }

        // child closes pipe_right_read, duplicates pipe_left_read to stdin,
        // pipe_right_write to stdout, then closes both.
        // errors reported by close are inconsequential
        if (IS_CHILD) {
          close(pipe_right[0]);
          if (dup2(pipe_left[0], STDIN_FILENO) == -1) {
            perror(SHELL_ERROR_IDENTIFIER);
            exit(EXIT_FAILURE);
          }
          if (dup2(pipe_right[1], STDOUT_FILENO) == -1) {
            perror(SHELL_ERROR_IDENTIFIER);
            exit(EXIT_FAILURE);
          }
          close(pipe_left[0]);
          close(pipe_right[1]);
        }

      } else {
        // Unknown case.
        fprintf(stderr, "%s: Unexpected pipe handling case for command %s.\n", SHELL_ERROR_IDENTIFIER, command_name);
        break; 
      }

      /*
        Redirection Handler      
        Set IS_REDIRECTED flag
        Save STDIN/STDOUT/STDERR for later restoration
        This implementation possibly racks up "erroneous file handlers"
        unless dup() removes those on error.
      */       
      if (IS_PARENT && IS_INTERNAL) {        
        if (comms[i].ifile != NULL) {
          IS_REDIRECTED = true;
          ORIGINAL_STDIN = dup(STDIN_FILENO);
          if (ORIGINAL_STDIN == -1) {
            perror(SHELL_ERROR_IDENTIFIER);
            break;
          }
        }
        if (comms[i].ofile != NULL) {
          IS_REDIRECTED = true;
          ORIGINAL_STDOUT = dup(STDOUT_FILENO);
          if (ORIGINAL_STDOUT == -1) {
            perror(SHELL_ERROR_IDENTIFIER);
            break;
          }
        }
        // Not necessary as we do not handle STDERR redirection
        // ORIGINAL_STDERR = dup(STDERR_FILENO);
        // if (ORIGINAL_STDERR == -1) {
        //   perror(SHELL_ERROR_IDENTIFIER);
        //   break;
        // }        

        redirection(comms[i].ifile, comms[i].ofile, comms[i].append);
      } else if (IS_CHILD) {
        IS_REDIRECTED = true;
        redirection(comms[i].ifile, comms[i].ofile, comms[i].append);
      } 

      /*
        Run/Wait Handler
      */       


      /*
        Parent STDIO Reset
        In case of Single Internal Command with Redirection
      */       
    }
    /*
        On failure, MUST EXIT as user can no longer target STDIN or
        see our STDOUT, possibly.
    */
      if (IS_PARENT && IS_INTERNAL && IS_ALONE && IS_REDIRECTED) {
        if (comms[i].ifile != NULL) {
          if (dup2(ORIGINAL_STDIN, STDIN_FILENO) == -1) {
            perror(SHELL_ERROR_IDENTIFIER);
            exit(EXIT_FAILURE);
          }
          close(ORIGINAL_STDIN);
        }
        if (comms[i].ofile != NULL) {
          if (dup2(ORIGINAL_STDOUT, STDOUT_FILENO) == -1) {
            perror(SHELL_ERROR_IDENTIFIER);
            exit(EXIT_FAILURE);
          }
          close(ORIGINAL_STDOUT);
        }
        // Do not need to handle STDERR, we do not support it's redirection
      }

    }
  }
  exit(EXIT_SUCCESS);
}
