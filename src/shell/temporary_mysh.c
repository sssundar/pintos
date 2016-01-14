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
