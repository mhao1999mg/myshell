#include <stdio.h>
#include <unistd.h> 
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

pid_t fgpid; // current foreground process
const int MAX_JOBS = 200; // the maximum number of running jobs
const int MAX_PATH_SIZE = 500; // maximum path size (pwd)
const int MAX_SIZE = 1024; // maximum file size (pipe) (might change this later)

// signal handler
static void sigHandler(int sig) {
    // kill signal CTRL+C. do nothing if there is no process to kill
    if (sig == SIGINT && fgpid != 0) {
        kill(fgpid, SIGKILL); // kill foreground process
    }
}

// modify signals function (might add this to main)
void modify_signals() {
    // stop signal CTRL+Z. ignore the signal
    if (signal(SIGTSTP, SIG_IGN) == SIG_ERR) {
        printf("ERROR! Could not bind the signal handler.\n");
        exit(EXIT_FAILURE);
    }
}

// parse arguments from STDIN
int parser(char *line, char *args[], int *bg) {
	int i = 0; // token index
    char *token; // word tokens
    *bg = 0; // set background to false

    //char *line2 = line; // new line pointer to prevent memory leak

	// separate string into tokens
    while ((token = strsep(&line2, " \t\n")) != NULL) {
        for (int j = 0; j < strlen(token); j++) {
            if (token[j] <= 32) {
                token[j] = '\0';
            }
        }
        if (strlen(token) > 0) {
            //char *tmp = strdup(token); // duplicate token to free later
            args[i++] = tmp;
        }
    }

    // check if '&' is only found at the end of the string
    if (i > 0 && strcmp(args[i - 1], "&") == 0) {
        *bg = 1; // specify background
        args[--i] = NULL;
    }
    return i; // number of arguments
}

// get arguments from input
int getcmd(char *args[], int *background) {
    int length = 0; // the length of the input
    char *line = NULL; // the location of the input
    size_t linecap = 0; // the length of the input (in memory)
    
    printf("\n>> "); // print the prompt
    length = getline(&line, &linecap, stdin); // get the input as a line of text
    
	// end parsing if the input is incorrect
    if (length <= 0) {
        exit(-1);
    } else {
        int args = parser(line, args, background); // parse the input string
        //free(line);
        return args;
    }
}

// cd: change directory
void execute_cd(char *directory) {
    int success = chdir(directory);
    if (success == -1) {
        printf("Invalid path.\n");
    }
}

// pwd: print the current directory
void execute_pwd() {
    // get the directory and print it
    char path[MAX_PATH_SIZE];
    getcwd(path, MAX_PATH_SIZE);
    printf("%s\n", path);
}

// exit: exit shell
void execute_exit() {
    exit(EXIT_SUCCESS);
}

// fg: execute a background job in the foreground
void execute_fg(char *job_nb, pid_t jobs[]) {
    int job = atoi(job_nb);
    // check if the job is valid and if it is running in the background
    if (job > -1 && job <= MAX_JOBS && jobs[job] != 0 && kill(jobs[job],0) == 0) {
        kill(jobs[job], SIGCONT); // run the job in the foreground
        waitpid(jobs[job], NULL, WUNTRACED); // wait for the child process to finish
    } else {
        printf("Invalid job number.\n");
    }
}

// jobs: list background jobs
void execute_jobs(pid_t jobs[]) {
    printf("--------------------------------------------------------\n");

    printf("Background jobs:\tStatus\t\t\tPID\n");
    // iterate through all jobs
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i] != 0) {
            waitpid(jobs[i], NULL, WNOHANG); // wait for child process to finish
            // print the job if it is a background job
            if (kill(jobs[i], 0) == 0) {
                printf("[%d]\t\t\tRunning\t\t\t%d\n", i, jobs[i]);
            }
        } else {
            jobs[i] = 0; // if the process is not running
        }
    }

    printf("--------------------------------------------------------\n");
}

// save the PID of a background job
void save_jobs(pid_t pid, pid_t jobs[], int *job_index) {
    jobs[*job_index] = pid;
    *job_index = (*job_index + 1) % MAX_JOBS;
}

// THE MAIN SHELL LOOP
int main() {
    int bg; // background variable
    pid_t jobs[MAX_JOBS]; // array of job PIDs
    int latest_job_index = 0; // most recently added job

    int stdin_backup_fd = -1; // STDIN (0) backup
    int stdout_backup_fd = -1; // STDOUT (1) backup
    int input_red_fd = -1; // input redirection file descriptor
    int output_red_fd = -1; // output redirection file descriptor

    // check for stop signal (CTRL+Z)
    modify_signals();
    
    // THE ACTUAL LOOP
    while (1) {
        bg = 0;
        char *args[20] = { NULL }; // input arguments

        int isPipe = 0; // if piping is required  
        int special_index = -1; // redirection symbol index

        int n_args = getcmd(args, &bg); // get input arguments
        special_index = -1;
        
        // check for redirection or piping
        if (n_args > 2) {
            for (int i = 0; i < n_args; i++) {
                if (strcmp(args[i], ">") == 0 || strcmp(args[i], "<") == 0 || strcmp(args[i], "|") == 0) {
                    special_index = i;
                    break;
                }
            }

            // handle redirection or piping
            if (special_index != -1) {
                // output redirection
                if (strcmp(args[special_index], ">") == 0) {
                    stdout_backup_fd = dup(STDOUT_FILENO); // save STDOUT file descriptor
                    close(STDOUT_FILENO); // close STDOUT file descriptor

                    // open a new output file descriptor
                    output_red_fd = open(args[n_args - 1], O_CREAT | O_RDWR);

                    // clear the input arguments
                    args[special_index] = NULL;
                    args[n_args - 1] = NULL;
                    n_args -= 2;               
                }
                // input redirection
                else if (strcmp(args[special_index], "<") == 0) {
                    stdin_backup_fd = dup(STDIN_FILENO); // save STDIN file descriptor
                    close(STDIN_FILENO); // close STDIN file descriptor

                    // open a new input file descriptor
                    input_red_fd = open(args[n_args - 1], O_CREAT | O_RDWR);

                    // clear the input arguments
                    args[special_index] = NULL;
                    args[n_args - 1] = NULL;
                    n_args -= 2; 
                }
                // piping
                else if (strcmp(args[special_index], "|") == 0) {
                    isPipe = 1;
                    int fd_pipe[2]; // set the pipe locations

                    // create the pipe
                    if (pipe(fd_pipe) == -1 ) {
                        printf("Piping failure.\n");
                        exit(EXIT_FAILURE);
                    }

                    // fork the first child for the left argument (send output to STDOUT)
                    pid_t first_child = fork();
                    if (first_child == (pid_t) 0) {
                        // close STDOUT, connect write end to STDOUT
                        close(STDOUT_FILENO);
                        dup(fd_pipe[1]);
                        close(fd_pipe[0]);
                        close(fd_pipe[1]);                        

                        // remove right arguments
                        for (int i = special_index; i < n_args; i++) {
                            args[i] = NULL;
                        }
                        n_args -= (n_args - special_index);

                        // execute left argument
                        execvp(args[0], args);
                    } else if (first_child == (pid_t) -1) {
                        // fork error
                        printf("Fork error.\n");
                        exit(EXIT_FAILURE);
                    }

                    // fork the second child for the right argument (send input from STDIN)
                    pid_t second_child = fork();
                    if (second_child == (pid_t) 0) {
                        // close STDIN, connect read end to STDIN
                        close(STDIN_FILENO);
                        dup(fd_pipe[0]);
                        close(fd_pipe[1]);
                        close(fd_pipe[0]);  

                        // remove left arguments
                        for (int i = special_index; i < n_args - 1; i++) {
                            args[i - special_index] = args[i + 1];
                            args[i + 1] = NULL;
                        }
                        while (args[i - special_index] != NULL) {
                            args[i - special_index] = NULL;
                            i++;
                        }
                        n_args -= (special_index + 1);

                        // execute right argument
                        execvp(args[0], args);
                    } else if (second_child == (pid_t) -1) {
                        // fork error
                        printf("Fork error.\n");
                        exit(EXIT_FAILURE);
                    }

                    // close all pipes, wait for child processes to finish
                    close(fd_pipe[0]);
                    close(fd_pipe[1]);
                    waitpid(first_child, NULL, WUNTRACED);
                    waitpid(second_child, NULL, WUNTRACED);
                }
            }
        }

        // no arguments
        if (args[0] == NULL || isPipe == 1) {
        }
        // build-in commands
        else if (n_args == 2 && strcmp(args[0], "cd") == 0) {
            execute_cd(args[1]);
        } else if (n_args == 1 && strcmp(args[0], "pwd") == 0) {
            execute_pwd();
        } else if (n_args == 1 && strcmp(args[0], "exit") == 0) {
            execute_exit();
        } else if (n_args == 2 && strcmp(args[0], "fg") == 0 ) {
            execute_fg(args[1], jobs);
        } else if (n_args == 1 && strcmp(args[0], "jobs") == 0) {
            execute_jobs(jobs);
        } 
        // external commands
        else {
            // fork a child process
            pid_t pid = fork();
            if (pid == (pid_t) -1) {
                // fork error
                printf("Fork error.\n");
                exit(EXIT_FAILURE);
            }
            // child process
            else if (pid == (pid_t) 0) {
                // execute the argument
                execvp(args[0], args);
                printf("Command execution error.\n");
                exit(EXIT_FAILURE);
            }
            // parent process
            else {
                // process running in foreground
                if (bg == 0) {
                    fgpid = pid; // kill foreground process only

                    // check for kill signal (CTRL+C)
                    if (signal(SIGINT, sigHandler) == SIG_ERR) {
                        printf("ERROR! Could not bind the signal handler.\n");
                        exit(EXIT_FAILURE);
                    }                    

                    // wait for child process to finish
                    waitpid(pid, NULL, WUNTRACED);
                    fgpid = 0; // reset foreground PID when child process is finished
                }
                // process running in background
                else {
                    // ignore kill signal in background processes
                    if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
                        printf("ERROR! Could not bind the signal handler.\n");
                        exit(EXIT_FAILURE);
                    } 
                    save_jobs(pid, jobs, &latest_job_index); // save the PID of the background job
                }
            }
        }

        // restore file descriptors
        if (special_index != -1 && isPipe == 0) {
            close(input_red_fd); // close input redirection file descriptor
            close(output_red_fd); // close output redirection file descriptor
            dup2(stdin_backup_fd, STDIN_FILENO); // reset STDIN
            dup2(stdout_backup_fd, STDOUT_FILENO); // reset STDOUT
            close(stdin_backup_fd);
            close(stdout_backup_fd);
            input_red_fd = -1;
            output_red_fd = -1;
        } 
        //int i = 0;
        // free args to prevent memory leaks
        //while (args[i] != NULL) {
        //    free(args[i]);
        //    i++;
        //}
    }
    // exit if anything unforseen occurs
    return EXIT_SUCCESS;
}