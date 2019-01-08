/* Derrick Li - CS 344 Section 400 - Mar. 5, 2018
 * Program 3 - smallsh
 *
 * A simple shell.
 */

#define _GNU_SOURCE  //needed for getline() to work with c99
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

/* Constants */
#define MAX_BUFFER_SIZE 2048  //maximum characters in read buffer
#define MAX_NUM_OF_ARG 512  //maximum number of arguments to pass to exec..() function
#define INITIAL_SIZE_OF_BACKGROUND_ARRAY 1000  //initial size of array holding background pids

/* Function prototypes */
void setUpSignal();
void catchSIGTSTP(int signo);
void reapBackgroundProcesses(int **backgroundProcesses, int *backgroundIndex);
void killAllBackgroundProcesses(int **backgroundProcesses, int *backgroundIndex);
void notifyBgChangeStatus();
void subInProcessId (char **readBuffer, int shellPid);
int processInput(int numCharsEntered, char *readBuffer, char **argv, char **inputRedirection,
					char **outputRedirection, bool *runInBackground, bool *ignoreLine);
void builtInCd(char **argv);
void executeCommand(char **argv, char *inputRedirection, char *outputRedirection,
					bool runInBackground, int *exitMethod, int **backgroundProcesses,
					int *backgroundIndex, int *sizeOfBackgroundArray, char **readBuffer, int *exitStatus);
void freeAll(char **argv, char **readBuffer, int **backgroundProcesses);

/* Global variables */
bool bgOn = true;  //controls foreground-only mode
bool bgChanged = false;  //notify parent shell of change

int main(int argc, char* argv[])
{
	setUpSignal();  //set up signal handlers for parent process (the shell)

	pid_t shellPid = getpid();  //get process id of shell for later use

	/* Initialize array to hold child background process id -- will reap child zombies using this array */
	int sizeOfBackgroundArray = INITIAL_SIZE_OF_BACKGROUND_ARRAY;
	int *backgroundProcesses = calloc(sizeOfBackgroundArray, sizeof(int));
	int backgroundIndex = 0;  //next available array index for adding a background pid

	int exitMethod = 0;  //exit method set by return of child process. 0 = exited normally, 1 = terminated by signal.
	int exitStatus = 0;  //exit status of child process if exited normally, or signal number if terminated by signal.

	bool runShell = true;  //"exit" command will set this to false when user decides to quit

	while(runShell)
	{
		/* Initialize variables to hold user input */
		char **argv = calloc(MAX_NUM_OF_ARG + 1, sizeof(char *));   //argv to hold arguments; argv[0] will hold command
		char *inputRedirection = NULL;  //char pointer to input file
		char *outputRedirection = NULL;  //char pointer to output file
		bool runInBackground = false;  //bool flag for running process in background
		bool ignoreLine = false;  //bool flag for ignoring line if blank or comment line

		/* Reap any available background processes */
		reapBackgroundProcesses(&backgroundProcesses, &backgroundIndex);

		/* Notify user if foreground-only mode has been turned on/off */
		notifyBgChangeStatus();

		/* Display prompt */
		printf(":");
		fflush(stdout);

		/* Initialize read buffer and get input from user */
		char *readBuffer = calloc(MAX_BUFFER_SIZE, sizeof(char));
		size_t bufferSize = MAX_BUFFER_SIZE;
		int numCharsEntered = getline(&readBuffer, &bufferSize, stdin);;

		if(numCharsEntered == -1)  //In case signal interrupts getline() call, reprompt line
		{
			clearerr(stdin);
			continue;
		}

		readBuffer[strcspn(readBuffer, "\n")] = 0;  //get rid of newline character in readBuffer

		/* Substitute in process Id */
		subInProcessId (&readBuffer, shellPid);

		/* Parse input and set runInBackground & ignoreLine bool flags*/
		int result = processInput(numCharsEntered, readBuffer, argv, &inputRedirection,
									&outputRedirection, &runInBackground, &ignoreLine);

		/* If parse input is successful and ignoreLine is false */
		if(result == 0 && !ignoreLine)
		{
			if(strcmp(argv[0], "exit") == 0)  //built-in "exit" command
			{
				/* Kill all background processes and clear runShell bool flag to exit while loop. */
				killAllBackgroundProcesses(&backgroundProcesses, &backgroundIndex);
				runShell = false;
			}
			else if (strcmp(argv[0], "cd") == 0)  //built-in "cd" command
			{
				builtInCd(argv);  //pass arguments to builtInCd() function to deal with directory change
			}
			else if (strcmp(argv[0], "status") == 0)  //built in "status" command
			{
				if(exitMethod == 0)  //if child process exited normally, display exit status
				{
					printf("exit value %d\n", exitStatus);
					fflush(stdout);
				}
				else  //if child process was terminated by signal, display signal number
				{
					printf("terminated by signal %d\n", exitStatus);
					fflush(stdout);
				}
			}
			else  //else a unix command, call executeCommand() to set up and execute execvp() call
			{
				executeCommand(argv, inputRedirection, outputRedirection, runInBackground, &exitMethod,
					&backgroundProcesses, &backgroundIndex, &sizeOfBackgroundArray, &readBuffer, &exitStatus);
			}
		}

		/* Free argv and read buffer */
		free(argv);
		free(readBuffer);
		argv = NULL;
		readBuffer = NULL;
	}

	/* Free backgroundProcesses before exiting shell */
	free(backgroundProcesses);
	backgroundProcesses = NULL;

	return 0;
}

/*
 * Setup signal handlers to parent process (the shell). SIGINT (Ctrl-C) is ignored by parent shell.
 * SIGTSTP (Ctrl-Z) toggles foreground-only mode in parent shell.
 */
void setUpSignal()
{
	struct sigaction SIGINT_action = {{0}};
	struct sigaction SIGTSTP_action = {{0}};

 	/* SIGINT will cause system calls to restart itself if interrupted */
	SIGINT_action.sa_handler = SIG_IGN;  //SIGINT will be ignored upon receipt of signal
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = SA_RESTART;

	/* SIGTSTP will cause system calls to return error if interrupted */
	SIGTSTP_action.sa_handler = catchSIGTSTP;  //catchSIGTSTP() is called upon receipt of SIGTSTP
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;

	/* Register the struct sigaction with the parent shell */
	sigaction(SIGINT, &SIGINT_action, NULL);
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

/*
 * Invoked when shell catches a SIGTSTP. Changes the global bgChanged flag to notify program that the
 * foreground-only mode has been toggled and toggle the foreground-only mode.
 */
void catchSIGTSTP(int signo)
{
	bgChanged = true;

	if(bgOn == true)
	{
		bgOn = false;
	}
	else
	{
		bgOn = true;
	}
}

/*
 * Reap background child processes. Goes through an array of pids and checks if the process
 * has terminated. If not, continue on to next pid; or if so, print exit status and remove
 * pid from array.
 */
void reapBackgroundProcesses(int **backgroundProcesses, int *backgroundIndex)
{
	pid_t reapedPid;
	int childExitMethod;
	int exitStatus;

	/* Goes through each pid in the backgroundProcesses array. */
	for(int i = 0; i < *backgroundIndex; i++)
	{
		/* Checks if specific pid has terminated. WNOHANG flag is used so shell isn't blocked
		 * if child has not terminated. */
		reapedPid = waitpid((*backgroundProcesses)[i], &childExitMethod, WNOHANG);

		/* If child has terminated. */
		if(reapedPid != 0)
		{
			/* Print out pid of finished process. */
			printf("background pid %d is done: ", reapedPid);
			fflush(stdout);

			/* Print exit status if exit normally or signal number if terminated by signal. */
			if(WIFEXITED(childExitMethod) != 0)  //child exited normally
			{
				exitStatus = WEXITSTATUS(childExitMethod);  //extract exit status
				printf("exit value %d\n", exitStatus);
				fflush(stdout);
			}
			else if(WIFSIGNALED(childExitMethod) != 0)  //child terminated by signal
			{
				exitStatus = WTERMSIG(childExitMethod);  //extract signal that terminated process
				printf("terminated by signal %d\n", exitStatus);
				fflush(stdout);
			}

			/* Remove pid from array by left-shifting array contents 1 element towards beginning. */
			for(int j = i; j < *backgroundIndex - 1; j++)
			{
				(*backgroundProcesses)[j] = (*backgroundProcesses)[j + 1];
			}

			(*backgroundProcesses)[*backgroundIndex - 1] = 0;  //set the void created to 0 (extra precaution)

			(*backgroundIndex)--;  //adjust backgroundIndex due to pid removal
			i--;  //adjust index i due to pid removal
		}
	}
}

/*
 * Send SIGTERM signals to all background processes listed in backgroundProcesses array.
 */
void killAllBackgroundProcesses(int **backgroundProcesses, int *backgroundIndex)
{
	pid_t reapedPid;
	int childExitMethod;

	for(int i = 0; i < *backgroundIndex; i++)
	{
		/* Checks if specific background process has terminated. WNOHANG flag is used so shell isn't
		 * blocked if process has not terminated. */
		reapedPid = waitpid((*backgroundProcesses)[i], &childExitMethod, WNOHANG);

		/* If not yet terminated, send SIGTERM signal to kill background process. */
		if(reapedPid == 0)
		{
			kill((*backgroundProcesses)[i], SIGTERM);
		}
	}
}

/*
 * Print change foreground-only change status to screen.
 */
void notifyBgChangeStatus()
{
	if(bgChanged)  //if bgChanged flag is set, then display message
	{
		if(bgOn)  //display appropriate message depending on if bgOn is set or cleared
		{
			printf("\nExiting foreground-only mode\n");
			fflush(stdout);
		}
		else
		{
			printf("\nEntering foreground-only mode (& is now ignored)\n");
			fflush(stdout);
		}

		bgChanged = false;  //clear bgChanged flag
	}
}

/*
 * Substitute in the parent shell pid any time "$$" shows up in the input received from user.
 */
void subInProcessId (char **readBuffer, int shellPid)
{
	char *inputString = *readBuffer;  //pointer to keep track of reading position

	/* Initialize a temp buffer to build new string with correctly substituted parent shell pid. */
	char tempBuffer[MAX_BUFFER_SIZE];
	memset(tempBuffer, '\0', sizeof(tempBuffer));
	int tempIndex = 0;

	/* Convert the parent shell pid from an int into a string. */
	char intBuffer[MAX_BUFFER_SIZE];
	memset(intBuffer, '\0', sizeof(intBuffer));
	int intLength = sprintf(intBuffer, "%d", shellPid);

	/* Pointer p will point to the next occurrence of "$$" in input. While loop will find the
	 * next occurrence, copy all character starting at pointer p to the occurence of "$$" into temp buffer,
	 * put in the parent shell pid, and repeat for remaining "$$"s. */
	char *p;
	while((p = strstr(inputString, "$$")) != NULL)
	{
		/* Copy all characters from pointer p up to the character right before the "$$". */
		strncpy(tempBuffer + tempIndex, inputString, p - inputString);
		tempIndex += p - inputString;  //adjust the temp index

		/* Copy in the parent shell pid */
		strncpy(tempBuffer + tempIndex, intBuffer, intLength);
		tempIndex += intLength;  //adjust the temp index

		inputString = p + 2;  //have inputString point to the character immediately after the "$$""
	}

	//Copy remainder of string if necessary
	strncpy(tempBuffer + tempIndex, inputString, strchr(inputString, '\0') - inputString);

	//Copy tempBuffer into readBuffer to return to main
	strcpy(*readBuffer, tempBuffer);
}

/*
 * Parse input received from the user. Place the command and arguments into argv. Optionally, set the inputRedirection
 * and outputRedirection char pointers, runInBackground bool flag, and ignoreLine bool flag.
 */
int processInput(int numCharsEntered, char *readBuffer, char **argv, char **inputRedirection,
					char **outputRedirection, bool *runInBackground, bool *ignoreLine)
{
	/* Setup string tokenizer function to delimit on space and newline character. */
	char delim[] = " \n";
	char *token = strtok(readBuffer, delim);
	char *previousToken = NULL;

	if(numCharsEntered == 1)  //check if blank line (the removed newline character was counted as a char)
	{
		*ignoreLine = true;
	}
	else if(*token == '#')  //check if comment line (line started with a '#')
	{
		*ignoreLine = true;
	}
	else  //build argv and optionally set redirection char pointers and runInBackground bool flag
	{
		argv[0] = token;  //command assigned to element 0
		int numOfArg = 0;  //argument counter

		/* Build argv */
		do
		{
			/* Use a trailing previousToken to assist in detecting if last word is "&". */
			previousToken = token;
			token = strtok(NULL, delim);

			if(token != NULL)
			{
				/* If token read is "<" or ">", break loop so next loop can handle redirection. */
				if(strcmp(token, "<") == 0 || strcmp(token, ">") == 0)
				{
					break;
				}
				/* Else, add argument to argv. */
				else
				{
					numOfArg++;
					argv[numOfArg] = token;

					/* If number of argument has reached max, check if there is a next token. If
					 * not and the previous token is an "&", then set runInBackground flag and
					 * remove the just-placed "&" from the argv array. In any case, break out of
					 * while loop because maximum number of arguments have been reached. */
					if(numOfArg == MAX_NUM_OF_ARG)
					{
						previousToken = token;
						token = strtok(NULL, delim);
						if(token == NULL && strcmp(previousToken, "&") == 0)
						{
							argv[numOfArg] = NULL;
							numOfArg--;

							*runInBackground = true;
						}

						break;
					}
				}
			}
			else
			{
				/* If no next available token, check if previous token is an "&". If so, then
				 * it was already added to the argv array. Thus, remove it from the array and set
				 * the runInBackground bool flag.*/
				if(strcmp(previousToken, "&") == 0)
				{
					argv[numOfArg] = NULL;
					numOfArg--;

					*runInBackground = true;
					break;
				}
			}

		} while(token != NULL);

		/* Get I/O redirection. */
		while(token != NULL)
		{
			/* If input redirection is specified, assign inputRedirection char pointer
			 * to point to name of file. */
			if(strcmp(token, "<") == 0)
			{
				token = strtok(NULL, delim);  //get input file name

				/* Print error if no input file provided and return from function. */
				if(token == NULL || strcmp(token, "&") == 0)
				{
					fprintf(stderr, "Input redirection not specified!\n");
					fflush(stderr);
					return 1;
				}

				*inputRedirection = token;  //do inputRedirection char pointer assignment
			}
			/* If output redirection is specified, assign outputRedirection char pointer
			 * to point to name of file. */
			else if(strcmp(token, ">") == 0)
			{
				token = strtok(NULL, delim);  //get output file name

				/* Print error if no output file provided and return from function. */
				if(token == NULL || strcmp(token, "&") == 0)
				{
					fprintf(stderr, "Output redirection not specified!\n");
					fflush(stderr);
					return 1;
				}

				*outputRedirection = token;  //do outputRedirection char pointer assignment
			}
			else  //any other token besides "<" or ">" will cause the while loop to break
			{
				break;
			}
			token = strtok(NULL, delim);
		}

		/* At this point, argv has been built (no further changes will be made to it), and
		 * redirections may or may not have been made. The following will set the runInBackground
		 * bool if the last word entered is an "&". If the runInBackground bool flag has already
		 * been previously set in the construction of argv, then this block has no effect. */
		previousToken = NULL;
		while(token != NULL)
		{
			previousToken = token;
			token = strtok(NULL, delim);
		}

		if(token == NULL && previousToken != NULL && strcmp(previousToken, "&") == 0)
		{
			*runInBackground = true;
		}
	}

	return 0;
}

/*
 * The shell built-in "cd" command. '~' will be expanded to the user's home directory path.
 */
void builtInCd(char **argv)
{
	int result;

	/* If no path provide, or in case user tries to run with background '&' option,
	 * go to home directory. */
	if(argv[1] == NULL || strcmp(argv[1], "&") == 0)
	{
		result = chdir(getenv("HOME"));
	}
	else
	{
		/* Absolute path involving home directory, '~'. */
		if(*argv[1] == '~')
		{
			/* Expland '~' to the user's home directory. */
			char myPath[MAX_BUFFER_SIZE];
			memset(myPath, '\0', sizeof(myPath));

			strcpy(myPath, getenv("HOME"));
			strcat(myPath, argv[1] + 1);  //concatenate everything after the '~'

			result = chdir(myPath);  //execute the change directory

		}
		/* Absolute and relative path not involving '~' */
		else
		{
			result = chdir(argv[1]);  //execute the change directory
		}
	}

	if(result == -1)  //print error if cannot change directory
	{
		perror("Error in changing directory");
		fflush(stderr);
	}
}

/*
 * Execute the command requested by user. Fork off new child, perform any necessary redirection,
 * and call execvp() function. If run in background is requested, then command line control is
 * given directly back to user; otherwise, the process will run in foreground and shell will wait
 * until process finishes execution before returning commmand line control to user.
 */
void executeCommand(char **argv, char *inputRedirection, char *outputRedirection, bool runInBackground,
					int *exitMethod, int **backgroundProcesses, int *backgroundIndex, 
					int *sizeOfBackgroundArray, char **readBuffer, int *exitStatus)
{
	pid_t spawnPid;
	int childExitMethod;

	spawnPid = fork();  //fork new child

	if(spawnPid == -1)  //if fork() fails
	{
		perror("Bad spawn");
		fflush(stderr);
	}
	else if(spawnPid == 0)  //in child process
	{
		/* If process will run in background but inputRedirection is not assigned, then
		 * assign "/dev/null" to inputRedirection. */
		if(runInBackground && inputRedirection == NULL)
		{
			inputRedirection = "/dev/null";
		}

		/* If process will run in background but outputRedirection is not assigned, then
		 * assign "/dev/null" to outRedirection. */
		if(runInBackground && outputRedirection == NULL)
		{
			outputRedirection = "/dev/null";
		}

		/* If inputRedirection is assigned something, then open up the file pointed by
		 * inputRedirection and use dup2() to assign stdin to point to the file. */
		if(inputRedirection != NULL)
		{
			int sourceFD;  //will hold file descriptor for opened file
			sourceFD = open(inputRedirection, O_RDONLY);  //open file for read-only

			if(sourceFD == -1)  //if error opening file, print message, free mem, and exit
			{
				fprintf(stderr, "cannot open %s for input\n", inputRedirection);
				fflush(stderr);

				freeAll(argv, readBuffer, backgroundProcesses);  //needed to prevent mem leaks
				exit(1);
			}

			int result = dup2(sourceFD, 0);  //assign stdin to point to sourceFD
			if(result == -1)  //if error in dup() operation, print message, free mem, and exit
			{
				perror("Source dup2() error");
				fflush(stderr);

				freeAll(argv, readBuffer, backgroundProcesses);  //needed to prevent mem leaks
				exit(1);
			}
		}

		/* If outputRedirection is assigned something, then open up the file pointed by
		 * outputRedirection and use dup2() to assign stdout to point to the file. */
		if(outputRedirection != NULL)
		{
			int targetFD;  //will hold file descriptor for opened file

			/* If opening "/dev/null", then open for write-only. Otherwise, in addition
			 * to opening file for write-only, if non-existent, then create the file, and
			 * if file already exist, truncate the file. */
			if(strcmp(outputRedirection, "/dev/null") == 0)
			{
				targetFD = open("/dev/null", O_WRONLY);
			}
			else
			{
				/* Opened file permission set to 644: user(RW-), group(R--), and others(R--). */
				targetFD = open(outputRedirection, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			}

			if(targetFD == -1)  //if error opening file, print message, free mem, and exit
			{
				fprintf(stderr, "cannot open %s for output\n", outputRedirection);
				fflush(stderr);

				freeAll(argv, readBuffer, backgroundProcesses);  //needed to prevent mem leaks
				exit(1);
			}

			int result = dup2(targetFD, 1);  //assign stdout to point to targetFD
			if(result == -1)  //if error in dup() operation, print message, free mem, and exit
			{
				perror("Target dup2() error");
				fflush(stderr);

				freeAll(argv, readBuffer, backgroundProcesses);  //needed to prevent mem leaks
				exit(1);
			}
		}

		/* Create signal handlers for child process */
		struct sigaction ignore_action = {{0}};
		struct sigaction default_action = {{0}};

		/* Struct sigaction to ignore signal */
		ignore_action.sa_handler = SIG_IGN;
		sigfillset(&ignore_action.sa_mask);
		ignore_action.sa_flags = 0;

		/* Struct sigaction to take default action */
		default_action.sa_handler = SIG_DFL;
		sigfillset(&default_action.sa_mask);
		default_action.sa_flags = 0;

		/* Register child processes to ignore SIGTSTP */
		sigaction(SIGTSTP, &ignore_action, NULL);

		/* If child process is to run in background (i.e. runInBackground flag is set
		 * AND bgOn flag is set to enable background processes), then register the
		 * child process to ignore the SIGINT. */
		if(runInBackground && bgOn)
		{
			sigaction(SIGINT, &ignore_action, NULL);
		}
		/* Else, child will run in foreground. Register child process
		 * to take default action on receipt of SIGINT. */
		else
		{
			sigaction(SIGINT, &default_action, NULL);
		}

		/* Execute the command in argv[0] and pass argv. */
		execvp(argv[0], argv);

		/* The following section will only execute if execvp returns an error. */
		fprintf(stderr, "%s: no such file or directory\n", argv[0]);
		fflush(stderr);

		freeAll(argv, readBuffer, backgroundProcesses);  //needed to prevent mem leaks
		exit(1);
	}
	else  //in parent process
	{
		/* If child process is running in foregrond (i.e. runInBackground flag is not set, or
		 * bgOn is not set, that is, foreground-only mode is ON), then wait for process
		 * to terminated. Determine if the child exited normarlly or was terminated by signal
		 * and extract and save the exit status. Print out message if child process was terminated
		 * by a signal. */
		if(!runInBackground || !bgOn)
		{
			/* Wait until child process has finished -- while loop is necessary if waitpid()
			 * is interrupted by TSTP signal. */
			int receivedPid = 0;
			while((receivedPid = waitpid(spawnPid, &childExitMethod, 0)) != spawnPid);

			if(WIFEXITED(childExitMethod) != 0)  //child exited normally
			{
				*exitStatus = WEXITSTATUS(childExitMethod);  //extract exit status
				*exitMethod = 0;  //set exitMethod to 0, indicating normal exit
			}
			else if(WIFSIGNALED(childExitMethod) != 0)  //child terminated by signal
			{
				*exitStatus = WTERMSIG(childExitMethod);  //extract signal that terminated process
				*exitMethod = 1;  //set exitMethod to 1, indicating terminated by signal

				printf("terminated by signal %d\n", *exitStatus);
				fflush(stdout);
			}
		}
		/* Else, child process is running in background. Print child pid to screen and add
		 * pid to backgroundProcesses array.  */
		else
		{
			printf("background pid is %d\n", spawnPid);
			fflush(stdout);

			/* If backgroundProcesses is at capacity, double the allocated memory size. */
			if(*backgroundIndex == *sizeOfBackgroundArray)
			{
				(*sizeOfBackgroundArray) *= 2;  //double array size
				*backgroundProcesses = realloc(*backgroundProcesses, *sizeOfBackgroundArray * sizeof(int));

				if(*backgroundProcesses == NULL)  //display message if error encountered in realloc
				{
					perror("Error in expanding background array\n");
					fflush(stderr);

					freeAll(argv, readBuffer, backgroundProcesses);
					exit(1);
				}
			}

			(*backgroundProcesses)[*backgroundIndex] = spawnPid;  //add child pid to backgroundProcesses array
			(*backgroundIndex)++;  //increment backgroundIndex
		}
	}
}

/*
 * Free the memory allocated for argv, readBuffer, and backgroundProcesses. Used in cases where process
 * needs to adruptly exit due to error condition.
 */
void freeAll(char **argv, char **readBuffer, int **backgroundProcesses)
{
		free(argv);
		free(*readBuffer);
		free(*backgroundProcesses);
		argv = NULL;
		*readBuffer = NULL;
		*backgroundProcesses = NULL;
}