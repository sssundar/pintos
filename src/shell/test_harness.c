#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

void test_parser() {
	int i;
	char *str1 = "Hi you are nice.";
	//            01234567890123456
	char *strs[] = {
			"ls",
			"ls -l",
			"ls -l > out.txt",
			"ls -l | grep tmp_dir > out.txt",
			"render 200 200 < scene.iv > out.ppm",
			"< scene.iv render 200 200 > out.ppm",
			"< scene.iv > out.ppm render 200 200",
			"render 200 200 < scene.iv >> out.ppm",
			"render 200 200 < scene.iv | display -",
			"echo \"mon enfant, ma soeur, songe a la douceur...\" > out",
			"echo \"mon>enfant|ma<soeur,songe\ta la douceur...\" > out",
			"  echo\t\"sup\"",
			"\techo\t\"sup\"",
			"\techo    \"sup\"\t  ",
			NULL
	};
	char **s_ptr;
	char **t_ptr, **toks;
	command *c_ptr, *commands;

	char *tmp;

	// Test that the substring functions works correctly.
	printf("substr(str1, 0, 2) = \"%s\"\n", tmp = substr(str1, 0, 2));
	free(tmp);
	printf("substr(str1, -2, 2) = \"%s\"\n", tmp = substr(str1, -2, 2));
	free(tmp);
	printf("substr(str1, 0, 1000) = \"%s\"\n", tmp = substr(str1, 0, 1000));
	free(tmp);
	printf("substr(str1, 2, 5) = \"%s\"\n", tmp = substr(str1, 2, 5));
	free(tmp);
	printf("substr(str1, 3, 1) = \"%s\"\n", tmp = substr(str1, 3, 1));
	free(tmp);

	// Test the tokenizer.
	s_ptr = strs;
	while(*s_ptr != NULL) {
		printf("Going to print tokenized \"%s\":\n", *s_ptr);
		toks = get_tokens(*(s_ptr++));

		t_ptr = toks;
		while(*t_ptr != NULL) {
			printf("  \"%s\"\n", *(t_ptr++));
		}

		t_ptr = toks;
		while (*t_ptr != NULL) {
			free(*t_ptr);
			t_ptr++;
		}
		free(toks);
	}

	// Test get_commands.
	s_ptr = strs;
	while(*s_ptr != NULL) {
		printf("Dealing with command \"%s\":\n", *s_ptr);
		commands = get_commands(*(s_ptr++));

		c_ptr = commands;
		i = 0;
		while(c_ptr->argv != NULL) {

			printf("  Command %d: \n", (int)(c_ptr - commands));

			// Print contents of argv
			for (i = 0; i < c_ptr->argc; i++) {
				printf("    argv[%d] = \"%s\"\n", i, c_ptr->argv[i]);
			}

			printf("    argc = %d\n", c_ptr->argc);
			printf("    in file = \"%s\"\n", c_ptr->ifile);
			printf("    out file = \"%s\"\n", c_ptr->ofile);
			printf("    append = %s\n", c_ptr->append ? "true" : "false");

			c_ptr++;
		}
	}
}

// Run some simple tests.
int main() {

	test_parser();

	return 0;
}
