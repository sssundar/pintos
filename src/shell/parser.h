/**
 * @file
 * @author Hamik Mukelyan
 */

#ifndef SRC_SHELL_PARSER_H_
#define SRC_SHELL_PARSER_H_

//------------------------------ Includes -------------------------------------

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h> // So we can use "bool", "true", and "false" as in C++.

//----------------------------- Definitions -----------------------------------

/**
 * Represents a single command like "ls -l > out.txt". Something like
 * "ls -l | grep donniespassword" should be split into two command structs.
 */
typedef struct command {

	// Array of pointers to C strings, each of which is an argument for this
	// program. The first "argument" is actually the program name. This array
	// IS null terminated even though "argc" can be used to mark the end of
	// the array.
	char **argv;

	// The length of the "arguments" array.
	int argc;

	// Can be an absolute or relative path. NULL if should use standard in.
	char *ifile;

	// Can be an absolute or relative path. NULL if should use standard out.
	char *ofile;

	// True if the append characters ">>" were used for output redirection
	// instead of ">", which overwrites.
	bool append;
} command;

//------------------------------ Prototypes -----------------------------------

/**
 * Tokenizes the input string by whitespace, <, >, and |, taking care to
 * treat characters in quotation marks as single tokens. We assume for
 * simplicity that quotation marks can't be escaped. This can be called
 * explicitly but our intention is for "get_commands" to take care of calls
 * to this function.
 *
 * @param input a single line (or concatenated lines without "\\" sequences)
 * consisting of some or all of the following: program names, arguments, pipes,
 * redirection operators, or quotation marks. For example, input might be
 * "ls -l > hi.txt", "ls -l | grep donniespassword".
 *
 * @return NULL-terminated array of tokens, NULL if input is empty or NULL.
 *
 * @warning Each token in the tokens array and the array itself should be
 * freed by the caller.
 */
char **get_tokens(char *input);

/**
 * Takes "input", tokenizes it, then generates an array of "command"
 * structs. Returns NULL if the input can't be parsed. Prints error message
 * to stderr. IMPORTANT: the caller should stop traversing the returned array
 * at the first command whose "argv" member is NULL.
 *
 * @param input a single line (or concatenated lines without "\\" sequences)
 * consisting of some or all of the following: program names, arguments, pipes,
 * redirection operators, or quotation marks. For example, input might be
 * "ls -l > hi.txt", "ls -l | grep donniespassword".
 *
 * @return Array of commands, NULL if can't parse input line.
 *
 * @warning Each command in the commands array and the array itself should be
 * freed by the caller. IMPORTANT: the caller should stop traversing the array
 * at the first command whose "argv" member is NULL.
 */
command *get_commands(char *input);

/**
 * Gets the substring consisting of the chars in the interval [i, j) out of
 * "str". If the left index is out of bounds uses 0. If the right index is
 * out of bounds uses the index before the null-terminator. If i and j are
 * out of order prints an error message and exits.
 *
 * @param str The string from which to extract a substring.
 * @param i Start index, inclusive
 * @param j End index, exclusive
 *
 * @return Pointer to a newly-allocated string containing the desired substr.
 */
char *substr(char *str, int i, int j);

/**
 * Frees each string in the given tokens array as well as the whole array.
 *
 * @param tokens
 */
void free_tokens(char **tokens);

/**
 * Frees each command in the given commands array as well as the whole array.
 * Deep-frees by freeing the referenced strings.
 *
 * @param commands
 */
void free_commands(command *commands);

#endif /* SRC_SHELL_PARSER_H_ */
