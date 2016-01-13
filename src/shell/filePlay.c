#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/**
 * Opens a file named log.txt, writes a line to it, closes it,
 * then immediately reads from it. 
 */
int main () {	
	const char *mypath = "log.txt";
	char *mytext = "Hello World!\n";		
	int flags = O_WRONLY | O_TRUNC | O_CREAT;
	mode_t mode = S_IRWXU | S_IRGRP | S_IWGRP;
	int myfile = open(mypath, flags, mode);
	if (myfile == -1) {
		perror(NULL);
		exit(1);
	}	

	if (write(myfile, (void *) mytext, 14) == -1) {
		perror(NULL);
		exit(2);	
	}

	if (close(myfile) == -1) {
		perror(NULL);		
		// not necessarily an error! file descriptor was definitely closed. 
		// it's ok to proceed after this.
		exit(3);
	}
	
	flags = O_RDONLY;	
	myfile = open(mypath, flags);
	if (myfile == -1) {
		perror(NULL);
		exit(4);
	}
	char *buffer = malloc(14 * sizeof(char));
	if (buffer == NULL) {
		printf("Couldn't allocate read buffer.\n");
		exit(5);
	}
	int result = read(myfile, (void *) buffer, 14);
	if (result == -1) {
		perror(NULL);
		exit(6);	
	} else {
		printf("Result: %s\n", buffer);
	}	
	return 0;
}