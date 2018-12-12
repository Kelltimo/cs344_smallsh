/************************************************************
 * Title: Program 3 - smallsh (CS344-400)
 * Author: Daniel Jarc (jarcd)
 * Date: March 5, 2018
 * Description: Simple shell
 * *********************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

#define TOKEN_BUFFER_SIZE 512
#define TOKEN_DELIM " \t\r\n\a"


char *DIR;			// Starting directory
int in; 			// In flag
int out; 			// Out flag
char *input;		// Input filename
char *output;		// Output filename
int status;			// Exit status of processes
int running = 1; 	// Controls main loop
int fg_mode = 0;	// Foreground-only mode flag
int bg_bool = 0;	// Background process flag
int bg_pid[64];		// Array of background pids
int bg_cnt = 0; 	// background pid count

// Signal actions
struct sigaction SIGINT_action = {0};
struct sigaction SIGTSTP_action = {0};


/* ***********************************************************
 * cycle_fg_mode()
 * Description: Cycles foreground mode in response to SIGTSTP
 * *********************************************************/
void cycle_fg_mode(int signo) {
	
	if (fg_mode == 0) {	
		char *message = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 50);
		fg_mode = 1;
	} else {
		char *message = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 30);
		fg_mode = 0; 
	}
	char *prompt = ": ";
	write(STDOUT_FILENO, prompt, 2);
}

/* ***********************************************************
 * add_bg_pid()
 * Description: Adds pid to tracker array
 * *********************************************************/
void add_bg_pid(int pidno) {
	
    bg_pid[bg_cnt] = pidno;
	bg_cnt++;	
}

/* ***********************************************************
 * remove_bg_pid()
 * Description: Remove pid from tracker array
 * *********************************************************/
void remove_bg_pid(int pidno) {
	
	int i; 
    for (i = 0; i < bg_cnt; i++) {
		if (bg_pid[i] == pidno) {
			while (i < bg_cnt - 1) {
				bg_pid[i] = bg_pid[i + 1];
				i++;
			}
			bg_cnt--;
			break;
		}
    }
}

/* ***********************************************************
 * pid_replace()
 * Description: Replaces value in source string
 * Reference: stackoverflow.com 
 * *********************************************************/
void pid_replace(char *source, const char *searchValue, const char *replaceValue) {

    size_t searchValue_len = strlen(searchValue); 		// get the search value length (should be 2)
    size_t replaceValue_len = strlen(replaceValue); 	// the replace value length (should be 5)
	char buffer[2048] = { 0 };							// Initialize temporary buffer to hold modified string
    char *insert_point = &buffer[0];					// Set insert point to first character in buffer
    const char *tmp = source;							// Set char pointer temp to source
	
    while (1) {
		
		// Find occurrences of $$ in source
        const char *p = strstr(tmp, searchValue);

		// No more occurrences, copy remaining
        if (p == NULL) {
            strcpy(insert_point, tmp);
            break;
        }

        // Copy part segment before $$
        memcpy(insert_point, tmp, p - tmp);
        insert_point += p - tmp;

        // Copy pid into place
        memcpy(insert_point, replaceValue, replaceValue_len);
        insert_point += replaceValue_len;

        // Adjust pointer, move on
        tmp = p + searchValue_len;
    }

    // Write altered string back to source
    strcpy(source, buffer);
}

/* *********************************************************
 * read_line()
 * Description: Get's a line of input from user and replaces
 * $$ with pid
 * *********************************************************/
char* read_line() {
	
	// Prompt 
	printf(": ");
	fflush(stdout);
	
	// Get line of input
	char *line = NULL;
	ssize_t bufsize = 0;
	getline(&line, &bufsize, stdin);
	
	// Insert pid if necessary
	char *p = strstr(line, "$$");
	if (p) {
		char pidStr[6];
		sprintf(pidStr, "%d", getpid());
		pid_replace(line, "$$", pidStr);
	}
	return line;
}

/* **********************************************************
 * parse_line()
 * Description: Parses user input - creates args array
 * *********************************************************/
char** parse_line(char *line) {
	
	// Break string into tokens
	char **tokens = malloc(TOKEN_BUFFER_SIZE * sizeof(char*));
	char *token; 
	int position = 0;
	in = 0;
	out = 0;
	
	// Get first token
	token = strtok(line, TOKEN_DELIM);
		
	while (token != NULL) {
		
		// Output redirection
		if (strcmp(token, ">") == 0) {
			out = 1;
			output = strtok(NULL, TOKEN_DELIM);
			token = strtok(NULL, TOKEN_DELIM);
			tokens[position] = NULL;
			position++;
			continue;
		}
		
		// Input redirection
		if (strcmp(token, "<") == 0) {
			in = 1;
			input = strtok(NULL, TOKEN_DELIM);
			token = strtok(NULL, TOKEN_DELIM);
			tokens[position] = NULL;
			position++;
			continue;
		}
		
		// Background process
		if (strcmp(token, "&") == 0) {
			tokens[position] = NULL;
			if (fg_mode) {
				bg_bool = 0;
			} else {
				bg_bool = 1;
			}
			break;
		}			
		
		// Add to arguments
		tokens[position] = token; 
		position++;
		token = strtok(NULL, TOKEN_DELIM);
	}
	
	// Set last argument to NULL
	tokens[position] = NULL;
	return tokens;
}

/* **********************************************************
 * launch()
 * Description: Launch non built-in commands
 * *********************************************************/
int launch(char **args) {
	
	// Track pids
	pid_t spawnPid;
	pid_t waitPid;
	
	// Spawn child
	spawnPid = fork();
	
	if(spawnPid < 0) {
		
		perror("fork");
		exit(1);
	
	} else if (spawnPid == 0) {

		// Child process
		fflush(stdout);

		// Set input if specified
		if (in) {
			int fd0 = open(input, O_RDONLY);
			if (fd0 == -1) {
				printf("cannot open %s for input\n", input);
				fflush(stdout);
				exit(1);
			} else {
				if (dup2(fd0, STDIN_FILENO) == -1) {
					perror("dup2");
				}
				close(fd0);	
			}
		}

		// Set output if specified
		if (out) {
			int fd1 = creat(output, 0644);
			if (fd1 == -1) {
				printf("cannot create %s for output\n", output);
				fflush(stdout);
				exit(1);			
			} else {
				if (dup2(fd1, STDOUT_FILENO) == -1) {
					perror("dup2");
				}
				close(fd1);	
			}
		}
		
		// Background specific..
		if (bg_bool) {
			
			// Set input to dev/null if not specified
			if (!in) {
				int fd0 = open("/dev/null", O_RDONLY);
				if (fd0 == -1) {
					printf("cannot set /dev/null to input\n");
					fflush(stdout);
					exit(1);
				} else {
					if (dup2(fd0, STDIN_FILENO) == -1) {
						perror("dup2");
					}
					close(fd0);	
				}	
			}
			
			// Set output to dev/null if not specified
			if (!out) {
				int fd1 = creat("/dev/null", 0644);
				if (fd1 == -1) {
					printf("cannot set /dev/null to output\n");
					fflush(stdout);
					exit(1);			
				} else {
					if (dup2(fd1, STDOUT_FILENO) == -1) {
						perror("dup2");
					}
					close(fd1);	
				}
			}	
		}

		// Sets SIGINT action to default for foreground child
		if (!bg_bool) {
			
			SIGINT_action.sa_handler = SIG_DFL;
			SIGINT_action.sa_flags = 0;
			sigaction(SIGINT, &SIGINT_action, NULL);
		}
		
		// Command execution
		if (execvp(args[0], args)) {
			perror(args[0]);
			exit(1);		
		}
	
	} else {
		
		// Parent process
		if (!bg_bool) {
			
			do {
				// Wait for foreground child processes
				waitPid = waitpid(spawnPid, &status, WUNTRACED);

				if (waitPid == -1) { 
					perror("waitpid"); 
					exit(1); 
				}
				
				if (WIFSIGNALED(status)) {
					printf("terminated by signal %d\n", WTERMSIG(status));
					fflush(stdout);
				}
				
				if (WIFSTOPPED(status)) {
					printf("stopped by signal %d\n", WSTOPSIG(status));
					fflush(stdout);
				}

			} while (!WIFEXITED(status) && !WIFSIGNALED(status));
	
		} else {
			
			// Don't wait for background child processes.. but store pids for later reference
			printf("background pid is %d\n", spawnPid); 
			fflush(stdout); 
			add_bg_pid(spawnPid);
			bg_bool = 0; // reset bg bool in parent
		}
	}
	return 0;
}

/* **********************************************************
 * execute_command()
 * Description: Executes built in commands directly, launches
 * non built-in commands via spawning child processes.
 * *********************************************************/
void execute_command(char **args) {
	
	// If it's a blank line or comment
	if ((args[0] == NULL) || (strchr(args[0],'#'))) {
	
		// Do nothing...
		
	} else if (strcmp(args[0], "exit") == 0) {
			
		// Send SIGTERM to background processes
		while (bg_cnt > 0) {
			kill(bg_pid[0],SIGTERM);
			remove_bg_pid(bg_pid[0]);
		}
		running = 0;
		
	} else if (strcmp(args[0], "cd") == 0) {
		
		// Change directory
		if (args[1] == NULL) {
			if(chdir(getenv("HOME")) != 0) {
				perror("chdir");
			}
		} else {
			if(chdir(args[1]) != 0) {
				perror("chdir");
			}
		}	
	} else if (strcmp(args[0], "status") == 0) {	
		
		// Status
		if (WIFEXITED(status)) {
			printf("exit value %d\n", WEXITSTATUS(status));
			fflush(stdout);
		} else if (WIFSIGNALED(status)){
			printf("terminating signal %d\n", WTERMSIG(status));
			fflush(stdout);	
		}
	} else if (strcmp(args[0], "bp") == 0) {
		
		// Prints background pid array for testing
		if (bg_cnt > 0) {
			printf("bg pid tracker:\n");
			fflush(stdout);
			int i; 
			for (i= 0; i < bg_cnt; i++) {
				printf("[%d] %d\n", i, bg_pid[i]);
				fflush(stdout);
			}	
		} else {
			printf("no background pids at this time\n");
			fflush(stdout);
		}	
	} else {
		
		// Launch any other command via child process
		launch(args);	
	}
}

/* **********************************************************
 * check_processes()
 * Description: Checks to see if any background pids are done
 * *********************************************************/
void check_processes() {
	
	// Check to see if a background process terminated
	pid_t bg_pid;
	bg_pid = waitpid(-1, &status, WNOHANG);
	
	while (bg_pid > 0) {
		
		// If so, remove it from the array
		remove_bg_pid(bg_pid);
		
		// Print the exit status or termination signal
		if (WIFEXITED(status)) {
			printf("background pid %d is done. exit value %d\n", bg_pid, WEXITSTATUS(status)); 
			fflush(stdout);
		} else if (WIFSIGNALED(status)) {
			printf("background pid %d is done. terminated by signal %d\n", bg_pid, WTERMSIG(status)); 
			fflush(stdout);	
		}
		
		// Check if there are additional processes ending, loop if necessary
		bg_pid = waitpid(-1, &status, WNOHANG);
	}	
}

/* **********************************************************
 * input_loop()
 * Description: Main loop, checks processes, gets user input,
 * executes commands, frees memory and resets pointers 
 * *********************************************************/
void input_loop() {
	
	char *line;
	char **args;
	
	while(running) {
		
		// Check background processes
		if (bg_cnt > 0) {	
			check_processes();
		}
		
		// Handle input
		line = read_line();
		args = parse_line(line);
		execute_command(args);
		
		// Free memory, reset
		if (line != NULL) {
			free(line);
		}
		if (args != NULL) {
			free(args);
		}
		input = NULL;
		output = NULL;
	}
}

/* **********************************************************
 * main()
 * Description: Initializes settings, runs loop, restores
 * settings
 * *********************************************************/
int main (int argc, char* argv[]) {
	
	// Store directory
	DIR = getenv("PWD");
	
	// SIGINT handling - default is ignore
	SIGINT_action.sa_handler = SIG_IGN;
	sigaction(SIGINT, &SIGINT_action, NULL);

	// SIGTSTP handling - foreground only mode
	SIGTSTP_action.sa_handler = cycle_fg_mode;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
	
	// Loop 
	input_loop();
	
	// Reset directory
	chdir(DIR);
	
	// Exit
	return 0;
}