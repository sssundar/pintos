/**
 * @file
 * @author Hamik Mukelyan
 */

#include "parser.h"

/**
 * There can be at most 1024 bytes tokens or commands, where the extra one is
 * for '\0'. This is our own constraint, not the one suggested in the problem
 * set.
 */
const int MAX_TOKS = (1 << 13) | 1;

char *substr(char *str, int i, int j) {
	int k = 0;
	char *rst;

	// Check for erroneous bounds.
	if (j > strlen(str)) {
		j = strlen(str);
	}
	if (i < 0) {
		i = 0;
	}
	if (i > j) {
		i = 0;
		j = 0;
	}

	rst = (char *) malloc (sizeof(char) * (j - i + 1)); // extra for '\0'
	if (rst == NULL) {
		fprintf(stderr, "Token string allocation failed.");
		exit(1);
	}
	while (i < j) {
		rst[k++] = str[i++];
	}
	rst[k] = '\0';
	return rst;
}

char **get_tokens(char *input) {
	// True whenever we're processing a quoted token.
	bool quoted = false;

	// Starting index of token.
	int tok_start = -1;

	// All the tokens.
	char **tokens;

	// Stores the number of tokens. Functions as both a counter while adding
	// tokens and as a final tally. Not null-terminated.
	int num_tok = 0;

	// Position in the string "input".
	int i = 0;

	if (input == NULL || strlen(input) == 0) {
		return NULL;
	}

	tokens = (char **) malloc (sizeof(char *) * MAX_TOKS);
	if (tokens == NULL) {
		fprintf(stderr, "Problem when allocating \"tokens\" array.\n");
		exit(1);
	}

	// Tokenize the input string into array of strings. When the current token
	// ends store it and undo any flags, like "quoted". When a new token begins
	// just set the corresponding flag.
	while(input[i] != '\0') {

		// Handle quoted arguments.
		if(input[i] == '"') {
			// If we're at the start of a quoted token, set "quoted" flag.
			if (!quoted) {
				quoted = true;
				tok_start = i++ + 1;
			}

			// If we're at the end of a quoted token undo the flag.
			else {
				tokens[num_tok++] = substr(input, tok_start, i++);
				quoted = false;
				tok_start = -1;
			}
		}
		// If we're in a quoted token just move forward.
		else if (quoted) {
			i++;
			// Do nothing.
		}
		// Handle output redirection operator. Check if next char is '>' and
		// use ">>" instead of ">" for this token in that case.
		else if(input[i] == '>') {
			if(input[i + 1] == '>') { // Use ">>" for this token.
				tokens[num_tok++] = substr(input, i, i + 2);
				i+= 2;
			}
			else { // Use ">" for this token.
				tokens[num_tok++] = substr(input, i, i + 1);
				i++;
			}
		}
		// Handle input redirection operator.
		else if(input[i] == '<') {
			tokens[num_tok++] = substr(input, i, i + 1);
			i++;
		}
		// Handle pipe operator.
		else if(input[i]== '|') {
			tokens[num_tok++] = substr(input, i, i + 1);
			i++;
		}
		// Handle whitespace. If we're at the end of a token---that is,
		// "tok_start" doesn't have its sentinel value of -1---record the
		// token and move on. Otherwise just move on.
		else if (input[i] == ' ' || input[i] == '\t') {
			if (tok_start != -1) { // We're at the end of a token.
				tokens[num_tok++] = substr(input, tok_start, i++);
				tok_start = -1;
			}
			else { // Otherwise just move on.
				i++;
			}
		}
		// Handle characters which aren't delimiters. If "tok_start" has its
		// sentinel value then assume this is the start of a new token.
		// Otherwise just move on.
		else {
			if (tok_start != -1) { // We're in the middle of a token, move on.
				i++;
			}
			else { // Assume this is the start of a new token.
				tok_start = i++;
			}

			// Look ahead to next char. If it's a non-whitespace delim
			// then store this token and move on.
			if (input[i] != '\0') {

				if (input[i] == '|' || input[i] == '<' ||
						input[i] == '>' || input[i] == '"') {

					if (tok_start != -1) { // We're at the end of a token.
						tokens[num_tok++] = substr(input, tok_start, i);
						tok_start = -1;
					}
				}
			}
		}
	}
	// Handle the final token.
	if (tok_start != -1) {
		tokens[num_tok++] = substr(input, tok_start, i++);
		tok_start = -1;
	}
	// Null-terminate the tokens array.
	tokens[num_tok] = NULL;

	return tokens;
}

command *get_commands(char *input) {

	//--------------------------- Local variables -----------------------------

	// Reference to beginning of tokens array; pointer into tokens array.
	char **tokens, **t_ptr;

	// Temporary string pointer.
	char *str;

	// Number of tokens, used to see if we're at the last token.
	int num_toks = 0;

	// All the commands.
	command *commands;

	// Index of token we're looking at, index of command.
	int ti, ci;

	//------------------------------- Real work -------------------------------

	// Get the tokens, make sure the list isn't empty.
	tokens = get_tokens(input);
	if (tokens == NULL) {
		// Don't need to free b/c can't free a NULL pointer...
		return NULL;
	}

	// Get the number of tokens, return NULL if no tokens.
	t_ptr = tokens;
	while(*(t_ptr++) != NULL) {
		num_toks++;
	}
	if (num_toks == 0) {
		// The "tokens" pointer itself wasn't NULL; we had a NULL-terminated
		// empty list instead, so a malloc was performed. Free it.
		free_tokens(tokens);
		return NULL;
	}

	// If a pipe is the first or last token throw an error message and return
	// NULL because the command is unparseable.
	if(strcmp(tokens[0], "|") == 0 || strcmp(tokens[num_toks - 1], "|") == 0) {
		fprintf(stderr, "'|' can't be first or last token in command.\n");
		free_tokens(tokens);
		return NULL;
	}

	// Allocate the commands array.
	commands = (command *) malloc (sizeof(command) * MAX_TOKS);
	if (commands == NULL) {
		fprintf(stderr, "Error when allocating \"commands\" array.\n");
		// Don't need to free tokens before leaving because we're exiting the
		// program...
		exit(1);
	}

	// Initialize everything in the commands array.
	for (ci = 0; ci < MAX_TOKS; ci++) {
		commands[ci].argv = NULL;
		commands[ci].argc = 0;
		commands[ci].ifile = NULL;
		commands[ci].ofile = NULL;
		commands[ci].append = false;
	}

	// Loop over the array of tokens, filling in command struct members
	// between pipes.
	ci = 0; // command index
	for(ti = 0; ti < num_toks; ti++) { // Iterate over the tokens.

		// Process pipe tokens by incrementing the command index. It's safe to
		// assume a pipe isn't the first token or last token because of an
		// earlier check, but we do need to make sure that the bare minimum
		// struct members have been initialized.
		if (strcmp(tokens[ti], "|") == 0) {

			// Make sure that the current command has a non-null "argv" and
			// argc >= 1.
			if (commands[ci].argc <= 0 || commands[ci].argv == NULL) {
				fprintf(stderr, "Parser didn't set at least program name.\n");
				free_tokens(tokens);
				return NULL;
			}

			// If we get here the current command has the minimum fields set,
			// so move onto the next command.
			commands[ci].argv[commands[ci].argc] = NULL;
			ci++;
		}
		// Process output redirection operator. The next token must be a file
		// path, whether relative or absolute. Corollary: this token can't be
		// the final one and this token can't be right before a pipe.
		else if (strcmp(tokens[ti], ">") == 0) {

			// Make sure next token exists and is legitimate.
			if (ti == num_toks - 1 ||
					strcmp(tokens[ti + 1], "|") == 0 ||
					strcmp(tokens[ti + 1], ">") == 0 ||
					strcmp(tokens[ti + 1], ">>") == 0 ||
					strcmp(tokens[ti + 1], "<") == 0) {
				fprintf(stderr, "There must be a valid file after \">\".\n");
				free_tokens(tokens);
				return NULL;
			}

			// If we get here the next token is legitimate. Use it and skip it.
			str = (char *) malloc (sizeof(char) *
					(strlen(tokens[ti + 1]) + 1));
			strcpy(str, tokens[ti + 1]);
			commands[ci].ofile = str;
			ti++; // Skip the next token.
		}
		// Process output redirection append operator. The next token must be a
		// file path, whether relative or absolute. Corollary: this token can't
		// be the final one.
		else if (strcmp(tokens[ti], ">>") == 0) {

			// Make sure next token exists and is legitimate.
			if (ti == num_toks - 1 ||
					strcmp(tokens[ti + 1], "|") == 0 ||
					strcmp(tokens[ti + 1], ">") == 0 ||
					strcmp(tokens[ti + 1], ">>") == 0 ||
					strcmp(tokens[ti + 1], "<") == 0) {
				fprintf(stderr, "There must be a valid file after \">>\".\n");
				free_tokens(tokens);
				return NULL;
			}

			// If we get here the next token is legitimate. Use it and skip it.
			str = (char *) malloc (sizeof(char) *
					(strlen(tokens[ti + 1]) + 1));
			strcpy(str, tokens[ti + 1]);
			commands[ci].ofile = str;
			ti++; // Skip the next token.
			commands[ci].append = true;
		}
		// Process output redirection operator. The next token must be a file
		// path, whether relative or absolute. Corollary: this token can't be
		// the final one.
		else if (strcmp(tokens[ti], "<") == 0) {

			// Make sure next token exists and is legitimate.
			if (ti == num_toks - 1 ||
					strcmp(tokens[ti + 1], "|") == 0 ||
					strcmp(tokens[ti + 1], ">") == 0 ||
					strcmp(tokens[ti + 1], ">>") == 0 ||
					strcmp(tokens[ti + 1], "<") == 0) {
				fprintf(stderr, "There must be a valid file after \"<\".\n");
				free_tokens(tokens);
				return NULL;
			}

			// If we get here the next token is legitimate. Use it and skip it.
			str = (char *) malloc (sizeof(char) *
					(strlen(tokens[ti + 1]) + 1));
			strcpy(str, tokens[ti + 1]);
			commands[ci].ifile = str;
			ti++; // Skip the next token.
		}
		// Process arguments or program names. Redirection filename tokens are
		// already handled by the >, <, and >> conditionals. If the "argv"
		// array is still null then initialize it. Once it's initialized (or
		// if it was already initialized) add tokens by copying them into
		// a new chunk of memory then logging the pointer in "argv".
		else {

			// Initialize if necessary. There can be only as many arguments
			// as tokens. We need to allocate space for a null terminator, too.
			if (commands[ci].argv == NULL) {
				commands[ci].argv = (char **)
						malloc (sizeof(char *) * (num_toks + 1));
				if (commands[ci].argv == NULL) {
					fprintf(stderr, "Problem allocating argv.\n");
					exit(1);
				}
			}

			// By here "argv" should have been allocated. Copy the current
			// token and append it to "argv".
			str = (char *) malloc (sizeof(char) * (strlen(tokens[ti]) + 1));
			strcpy(str, tokens[ti]);
			commands[ci].argv[commands[ci].argc] = str;
			commands[ci].argc++;
		}
	}
	// Make sure that the current command has a non-null "argv" and argc >= 1.
	if (commands[ci].argc <= 0 || commands[ci].argv == NULL) {
		fprintf(stderr, "Parser didn't set at least program name.\n");
		free_tokens(tokens);
		return NULL;
	}
	commands[ci].argv[commands[ci].argc] = NULL; // NULL-terminate last "argv".

	free_tokens(tokens); // It's OK to free tokens here b/c they were copied.

	return commands;
}

void free_tokens(char **tokens) {
	char **t_ptr = tokens;

	// Don't free a null pointer.
	if (tokens == NULL) {
		return;
	}

	while (*t_ptr != NULL) {
		free(*t_ptr);
		t_ptr++;
	}
	free(tokens);
}

void free_commands(command *commands) {
	command *c_ptr = commands;

	// Don't free a null pointer.
	if (c_ptr == NULL) {
		return;
	}

	// Iterate while the arguments array "argv" isn't NULL.
	while (c_ptr->argv != NULL) {

		// Otherwise free everything we can.
		free_tokens(c_ptr->argv);
		if (c_ptr->ifile != NULL) {
			free(c_ptr->ifile);
		}
		if (c_ptr->ofile != NULL) {
			free(c_ptr->ofile);
		}

		c_ptr++;
	}
	free(commands);
}
