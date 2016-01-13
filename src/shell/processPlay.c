#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* 
	The command being processed is 
	less log.txt | grep -o '^[^ ]\+'
*/

/* A structure describing a single parsed command */
typedef struct parsed_command {
	// can be an absolute or relative path. execvp() can handle it
	char *program_path; 												
	// makes it easier for us to loop
	// argument_count = length(arguments)-1	
	int argument_count; 			
	// null terminated
	char **arguments; 				
	// can be an absolute or relative path				
	char *input_redirection_path; 	
	// can be an absolute or relative path
	char *output_redirection_path; 	
} a_command;

int main (int argc, char **argv) {			

	/////////////////////
	/* Local Variables */
	/////////////////////

	// Commands to execute
	char *prog1 = "less"; 						
	int argcnt1 = 2;
	char *args1[] = {"less", "log.txt", NULL};
 	
 	char *prog2 = "grep"; 						
	int argcnt2 = 2;
	char *args2[] = {"grep", "-o", "^[^ ]+", NULL};

	// Holder for child process IDs
	// if we are the fork parent
	pid_t pid; 						
	// Returned by waitpid() 
	int status;
	int w;

	a_command *test_command = malloc(1 * sizeof(a_command));

	if (test_command == NULL) {
		printf("Unable to allocate a command structure.");		
		exit(EXIT_FAILURE);
	}
	
	// Flesh out the test command
	(*test_command).program_path = prog;
	(*test_command).argument_count = argcnt;
	(*test_command).arguments = args;
	(*test_command).input_redirection_path = NULL;
	(*test_command).output_redirection_path = NULL;
	
	// Fork 									
	pid = fork(); 						 	
	 										
 	if (pid == -1) {
 		// Error 
 		perror("cursh"); 
 		// Stop executing this command set, go back to the shell
 		exit(EXIT_FAILURE);
 	} 

 	if (pid == 0) {
		// Child

		int flags = O_RDONLY;
		char *infile = "log.txt";
		int indesc = open(infile, flags);
		if (indesc == -1) {
			perror("cursh");
			// Stop executing command, go back to shell
			exit(EXIT_FAILURE);
		}
		if (dup2(indesc, STDIN_FILENO) == -1) {
			perror("cursh");
			// Stop executing command, go back to shell
			exit(EXIT_FAILURE);
		}
		close(indesc);

		flags = O_WRONLY | O_TRUNC | O_CREAT;
		mode_t mode = S_IRWXU | S_IRGRP | S_IWGRP; // 760 permissions requested
		char *outfile = "out.txt";
		int outdesc = open(outfile, flags, mode);
		if (outdesc == -1) {
			perror("cursh");
			// Stop executing command, go back to shell
			exit(EXIT_FAILURE);	
		}
		if (dup2(outdesc, STDOUT_FILENO) == -1) {
			perror("cursh");
			// Stop executing command, go back to shell
			exit(EXIT_FAILURE);
		}	
		close(outdesc);

		execvp((*test_command).program_path, (*test_command).arguments); 		
		 						// Will not return if successful.									
		perror((*test_command).program_path); 
		exit(EXIT_FAILURE);	
	}

	// Parent	
	printf("Parent PID: %ld\n",(long) getpid());
	printf("Child PID: %ld\n",(long) pid);
	// Default is to wait for termination
	w = waitpid(pid, &status, 0); 	
	
	if (w == -1) {
		perror("cursh"); 
		// Stop executing this command set, go back to the shell
		exit(EXIT_FAILURE);	
	}

	// If I didn't terminate normally or with a success exit code, I failed.
	if (WIFEXITED(status)) {			
		if (WEXITSTATUS(status) == 0) {
			printf("cursh: Child exited successfully.\n");
			// Proceed to next command if it exists, else back to the shell
			exit(EXIT_SUCCESS);
		} else {
			// Stop executing this command set, go back to the shell
			printf("cursh: Child exited unsuccessfully.\n");
			exit(EXIT_FAILURE);
		}
	} else {		
		// Stop executing this command set, go back to the shell
		printf("cursh: Child did not exit normally.\n");
		exit(EXIT_FAILURE);		
	}	
}