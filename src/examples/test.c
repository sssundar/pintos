#include <stdio.h>
#include <syscall.h>

int main(int argc, char *argv[]) {
	bool success = true;
	printf("hiiii. argc = %d, argv = %p\n", argc, argv);

	// Open a file for testing.
	int fd_echo = open("echo");
	printf("echo's fd was %d\n", fd_echo);
	int fd_TESTER = open("TESTER");
	//printf("TESTER's fd was %d\n", fd_TESTER);

	// Write to TESTER TODO fix, doesn't work.
	printf("Number of bytes written to TESTER: %d\n",
	  write(fd_TESTER, "abcde", 5));
	printf("File pos is currently: %d\n", tell(fd_TESTER));

	// Read from TESTER TODO try it.
	char buf[21];
	int i;
	for (i = 0; i < 20; i++) {
	buf[i] = 'x';
	}
	buf[20] = '\0';

	printf("Number of bytes read from TESTER: %d\n",
	  read(fd_TESTER, (void *) buf, 20));
	printf("The actual contents read: %s\n", buf);

	printf("The length of TESTER is %d\n", filesize(fd_TESTER));

	printf("About to close echo and TESTER.\n");
	close(fd_TESTER);
	close(fd_echo);
	printf("We just closed echo and TESTER.\n");

	printf("TESTER2 creation was a %s\n",
			create("TESTER2", 100) ? "success!!" : "failure :-(");
	int fd_TESTER2 = open("TESTER2");
	printf("TESTER2's fd is %d\n", fd_TESTER2);
	write(fd_TESTER2, "hihiih", 6);
	printf("Number of bytes read from TESTER: %d\n",
			read(fd_TESTER2, (void *) buf, 20));
	printf("The actual contents read: %s\n", buf);

	printf("About to remove TESTER.\n");
	printf("Removal of TESTER was a %s\n",
			remove("TESTER") ? "success!!" : "failure :-(");

	halt();

	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
