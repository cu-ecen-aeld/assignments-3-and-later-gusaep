#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    return system(cmd) == 0;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    int status = -1;
    int wstatus;

    pid_t fork_val = fork();
    switch (fork_val)
    {
    case -1:
        perror("fork");
        break;
    case 0:{

            // this runs on different processs
            int exit_val = execv(command[0], command);
            // This should not return
            perror("execv");
            exit(exit_val);
        }
        break;

    default:
        break;
    }

    va_end(args);
    printf("fork val %d\n", fork_val);
    if (waitpid(fork_val, &wstatus, 0) == -1) {
        return false;   
    } else if (WIFEXITED(wstatus)) {
        status = WEXITSTATUS(wstatus);
    }

    return status == 0;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
    int status = -1;
    int wstatus;
    int output_fd = open(outputfile, O_WRONLY|O_CREAT,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (output_fd < 0) {
        perror("open");
        exit(status);
    }

    pid_t fork_retval =fork();
    switch (fork_retval)
    {
    case -1:
        perror("fork");
        break;
    case 0:
        int dup_status = dup2(output_fd, 1); // replace the stdout with the new file descriptor in the new process
        if (dup_status < 0) {
            perror("dup2");
        }
        close(output_fd);
        
        int exit_val = execv(command[0], command);
        // This should not return
        perror("execv");
        exit(exit_val);
        break;
    default:
        break;
    }
    va_end(args);

    if (waitpid(fork_retval, &wstatus, 0) == -1) {
        return false;
    } else if (WIFEXITED(wstatus)) {
        status = WEXITSTATUS(wstatus);
    }

    return status == 0;
}
