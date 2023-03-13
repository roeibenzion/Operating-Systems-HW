#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include<sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#define READ 0
#define WRITE 1
/*FLAGS FOR STDOUT AND STDIN*/

/*General explenation - 
1) The command reading - check if it's an & command by checking the last position of the arglist given,
After that, check if there's a "special" sign (i.e > or |) and mark it's location, need the location to use the arglist given to pass to 
execvp(), by marking NULL the sign (in it's location) and "spliting" the array to 2 different commands.
2) The command execution - If none of the above detected (i.e not &, > or |) then fork and execvp.
If & is detected, need to run the child process in the background, i.e not killing the process on signals, so define ignore behavior on sigint, 
i defined it using Eran's trick (taken from the same StackOverflow page) with signal(SIGCHLD, SIG_IGN).
If | detected, command1 executed in first child process, command 2 in executed in second child process (another fork() in parent). Parent wait for both.
If > detected, exec the command with file number as the standard output.
*/
int prepare(void) 
{
    /*Ignore Sigint when running the shell, that's because of section 5 bullet 2 in the function behavior section in assignment description*/
    signal(SIGINT, SIG_IGN);
    return 0;
}
int finalize(void)
{
    return 0;
}
int process_arglist(int count, char** arglist)
{
    int i, ampersand,indexPipe, indexRedirect, status, statusSec, fd[2];
    pid_t pid, pidSec;
    char *pipeSign = "|", *redirectSign = ">", *command1, *command2;
    /*Pipe*/
    if(pipe(fd) < 0)
    {
        fprintf(stderr,"An error has occured\n");
        exit(1);
    }
    /*Check for wait sign*/
    if(strcmp(arglist[count-1],"&") == 0)
    {
        ampersand = 1;
        arglist[count-1] = NULL;
        count--;
    }
    else 
        ampersand = 0;
    
    indexPipe = -1;
    indexRedirect = -1;
    /*Check if and where there is a pipe or redirect sign*/
    for(i = 1; i < count; i ++)
    {
        if(strcmp(arglist[i], pipeSign) == 0)
        {
            indexPipe = i;
            break; 
        }
        if(strcmp(arglist[i], redirectSign) == 0)
        {
            indexRedirect = i;
            break; 
        }
    }
    if(indexPipe == -1 && indexRedirect == -1)
    {
        /*No pipe or redirect found so if process is child, run the program in the first word on the arguments given with execvp. 
        Parent waiting depends on & detection*/
        command1 = arglist[0];
        /*ERAN'S TRICK*/
        signal(SIGCHLD, SIG_IGN);
        pid = fork();
        if(pid < 0)
        {
            fprintf(stderr,"An error has occured\n");
            exit(1);
        }
        /*Child process - execvp*/
        if(pid == 0)
        {
            /*Foreground process, kill on SIGINT (which is default)*/
            if(ampersand == 0)
            {
                signal(SIGINT, SIG_DFL);
            }
            /*Background process, ignore signals*/
            else
            {
                signal(SIGINT, SIG_IGN);
            }
            if(execvp(command1, arglist) < 0)
            {
                fprintf(stderr,"Invalid shell command\n");
                exit(1);
            }
        }
        /*Foreground process, parent waits for execution*/
        if(ampersand == 0)
        {
            if(waitpid(pid, &status, 0)<0)
            {
                if(errno != ECHILD && errno != EINTR)
                {
                    fprintf(stderr,"Error\n");
                    exit(0);
                }
            }
        }
        return 1;
    }
    else if(indexRedirect == -1)
    {
        /*This is if a pipe symbol found.*/
        arglist[indexPipe] = NULL;
        /*Split by putting null in '|' location, everything from the beginning to there is command1, everything from 
        there till the end is command 2*/
        command1 = arglist[0];
        command2 = arglist[indexPipe+1];
        /*ERAN'S TRICK*/
        signal(SIGCHLD, SIG_IGN);
        pid = fork();
        if(pid < 0)
        {
            /*fork() faild*/
            fprintf( stderr,"Fork failed\n");
            exit(1);
        }
        if(pid == 0)
        {
            /*First child process, run the command and write to file desc in WRITE (=1) location*/
            /*dup2() duplicates fd[WRITE] onto stdout*/
            signal(SIGINT, SIG_DFL);
            close(fd[READ]);
            if(dup2(fd[WRITE], WRITE) < 0)
            {
                fprintf(stderr,"Error child process\n");
                exit(1);
            }
            if(execvp(command1, arglist) < 0)
            {
                fprintf(stderr,"Invalid shell command\n");
            }
            close(fd[WRITE]);
            exit(1);
        }
        else
        {
            /*Parent process, create another child process for the second command*/
            pidSec = fork();
            if(pidSec < 0)
            {
                /*fork() faild*/
                fprintf( stderr,"fork falid\n");
                exit(1);
            }
            if(pidSec == 0)
            {   
                signal(SIGINT, SIG_DFL);
                /*Second child process, run the command and */
                close(fd[WRITE]);
                if(dup2(fd[READ], READ) < 0)
                {
                    fprintf(stderr,"Error child process\n");
                    exit(1);
                }
                close(fd[READ]);
                if(execvp(command2, arglist + indexPipe + 1) < 0)
                {
                    fprintf( stderr,"Invalid shell command\n");
                }
                exit(1);
            }
            close(fd[READ]);
            close(fd[WRITE]);
            if(waitpid(pid, &status, 0)<0)
            {
                if(errno != ECHILD && errno != EINTR)
                {
                    fprintf(stderr,"Error\n");
                    exit(0);
                }
            }
            if(waitpid(pidSec, &statusSec, 0)<0)
            {
                if(errno != ECHILD && errno != EINTR)
                {
                    fprintf(stderr,"Error\n");
                    exit(0);
                }
            }
        }
        return 1;
    }
    else
    {
        /*This is if a redirect symbol found.*/
        arglist[indexRedirect] = NULL;
        command1 = arglist[0];
        /*command2 here is just a file name*/
        command2 = arglist[indexRedirect+1];
        /*Open file with flages taken from - https://man7.org/linux/man-pages/man2/open.2.html*/
        int file = open(command2, O_RDWR | O_APPEND | O_CREAT, 0644);
        /*ERAN'S TRICK*/
        signal(SIGCHLD, SIG_IGN);
        pid = fork();
        if(pid < 0)
        {
            exit(1);
        }
        if(pid == 0)
        {
            /*Child process, dup2 makes file a duplication of sdtout*/
            signal(SIGINT, SIG_DFL);
            close(fd[READ]);
            if(dup2(file, WRITE) < 0)
            {
                fprintf(stderr,"Error child process\n");
                exit(1);
            }
            close(fd[WRITE]);
            if(execvp(command1, arglist) < 0)
            {
                fprintf( stderr,"Invalid shell command\n");
                exit(1);
            }
        }
        if(waitpid(pid, &status, 0))
        {
            if(errno != ECHILD && errno != EINTR)
            {
                fprintf(stderr,"Error\n");
                exit(0);
            }
        }
        return 1;
    }
    return 1;
}