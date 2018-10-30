#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#define LINE_LEN 1024

int cmd_cd(char* path);
int cmd_cd_return(void);
void cmd_help(void);

void loop(void);

void pipe_add(char* buf);
void pipe_split(void);
void pipe_fork(void);
void pipe_wait(void);
void pipe_clean(void);

// Cmd control
typedef struct
{
    bool last;                          // Last cmd?

    int cmd_argc;                       // Argument count
    char** cmd_argv;                    // Arguments

    pid_t cmd_pid;                      // Child pid num
    int cmd_status;                     // Child status

}cmd_t;

char linebuf[LINE_LEN];// User input
int cmd_count;
cmd_t* cmd_list;

// Cd built-in cmd
int cmd_cd(char* path)
{
    int status;

    if(path[0] != '/')// path without leading '/'
    {
        char path_cpy[LINE_LEN];
        char cwd[LINE_LEN];

        strcpy(path_cpy, path);
        getcwd(cwd, sizeof(cwd));
        strcat(cwd, "/");
        strcat(cwd, path_cpy);
        status = chdir(cwd);// 0, if ok
    }
    else
    {
        status = chdir(path);// 0, if ok
    }
    return status;
}

// Cd.. built-in cmd
int cmd_cd_return(void)
{
    int status;
    char cwd[LINE_LEN];
    getcwd(cwd, sizeof(cwd));

    char* last_token;
    last_token = strrchr(cwd, '/');// last occurrence

    if(last_token != NULL)
    {
        *last_token = '\0';
        status = chdir(cwd);
    }
    return status;
}

// Help built-in cmd
void cmd_help(void)
{
    printf("========================================================\n");
    printf("Built-in commands:\n");
    printf("1. help\n");
    printf("2. exit\n");
    printf("3. cd\n");
    printf("4. cd..\n\n");
    printf("Other features:\n");
    printf("1. | (multiple piping)\n");
    printf("2. standard shell executable commands (ls, sort, etc.)\n");
    printf("========================================================\n");
}

// Take user input and execute command
void loop(void)
{
    while(1)
    {
        char cwd[LINE_LEN];
        getcwd(cwd, sizeof(cwd));

        printf("[BasicShell] %s >>> ", cwd);
        fflush(stdout);

        fgets(linebuf, sizeof(linebuf), stdin);

        if(strcmp(linebuf, "exit\n") == 0)
        {
            exit(0);
        }
        else if(strcmp(linebuf, "help\n") == 0)
        {
            cmd_help();
        }
        else if(strcmp(linebuf, "cd..\n") == 0)
        {
            cmd_cd_return();
        }
        else if(strncmp(linebuf, "cd\n", 2) == 0)
        {
            char* first_token;
            char* rest_str = linebuf;

            first_token = strtok_r(rest_str, " ", &rest_str);

            if(first_token != NULL && rest_str != NULL)
            {
                // Remove newline
                char* newline_pos;
                newline_pos = strchr(rest_str, '\n');
                if(newline_pos != NULL)
                {
                   *newline_pos = '\0';
                }

                if(cmd_cd(rest_str) != 0)
                {
                    printf("Path or dir specified does not exist\n");
                }
            }
            else
            {
                printf("Path not specified\n");
            }
        }
        else
        {
            pipe_split();
            pipe_fork();
            pipe_wait();
            pipe_clean();
        }
    }
}

// Add single command to pipe
void pipe_add(char* pipe)
{
    char* token;
    char* rest_str = pipe;
    cmd_t* cmd;

    cmd_list = realloc(cmd_list, (cmd_count + 1) * sizeof(cmd_t));

    cmd = &cmd_list[cmd_count];
    memset(cmd, 0, sizeof(cmd_t));

    cmd_count++;

    while(1)
    {
        token = strtok_r(rest_str, " \t", &rest_str);
        if(token == NULL)
            break;

        cmd->cmd_argv = realloc(cmd->cmd_argv,(cmd->cmd_argc + 2) * sizeof(char**));
        cmd->cmd_argv[cmd->cmd_argc] = token;
        cmd->cmd_argv[cmd->cmd_argc + 1] = NULL;

        cmd->cmd_argc++;
    }
}

// Read in and split up command
void pipe_split(void)
{
    char* token;
    char* rest_str = linebuf;
    cmd_t* cmd;

    token = strchr(linebuf, '\n');
    if(token != NULL)
        *token = 0;

    while(1)
    {
        token = strtok_r(rest_str, "|", &rest_str);

        if(token == NULL)
            break;

        pipe_add(token);
    }

    cmd = &cmd_list[cmd_count - 1];
    cmd->last = true;
}

// Fork elements of pipe
void pipe_fork(void)
{
    int fd_prev = -1;
    int fd_pipe[2] = { -1, -1 };

    for(cmd_t* cmd = cmd_list; cmd < &cmd_list[cmd_count]; ++cmd)
    {
        // Both parent and child closes output side of previous pipe
        close(fd_pipe[1]);
        fd_pipe[1] = -1;

        // Create a new pipe for the output of the current child
        if(cmd->last)
        {
            fd_pipe[0] = -1;
            fd_pipe[1] = -1;
        }
        else
            pipe(fd_pipe);

        cmd->cmd_pid = fork();
        if(cmd->cmd_pid < 0)
        {
            printf("Pipefork: fork fail -- %s\n", strerror(errno));
            exit(1);
        }

        // Save input side of pipe for next stage
        if(cmd->cmd_pid != 0)
        {
            close(fd_prev);
            fd_prev = fd_pipe[0];
            continue;
        }

        // Connect input to previous pipe output
        if(fd_prev >= 0)
        {
            dup2(fd_prev, 0);

            close(fd_prev);
            fd_prev = -1;
        }

        // Connect pipe output to stdout
        if(fd_pipe[1] >= 0)
        {
            dup2(fd_pipe[1], 1);

            close(fd_pipe[1]);
            fd_pipe[1] = -1;
        }

        // Child will not read its own output
        close(fd_pipe[0]);
        fd_pipe[0] = -1;

        // Execute cmd
        execvp(cmd->cmd_argv[0], cmd->cmd_argv);
        exit(1);
    }

    close(fd_pipe[0]);
    close(fd_pipe[1]);

    fd_pipe[0] = -1;
    fd_pipe[1] = -1;
}

// Wait for pipe stages to complete
void pipe_wait(void)
{
    pid_t pid;
    int status;
    int done_count = 0;

    while(done_count < cmd_count)
    {
        pid = waitpid(0, &status, 0);
        if(pid < 0)
            break;

        for(cmd_t* cmd = cmd_list; cmd < &cmd_list[cmd_count]; ++cmd)
        {
            if(pid == cmd->cmd_pid)
            {
                cmd->cmd_status = status;
                done_count++;
                break;
            }
        }
    }
}

// Free storage
void pipe_clean(void)
{
    for(cmd_t* cmd = cmd_list; cmd < &cmd_list[cmd_count]; ++cmd)
    {
        free(cmd->cmd_argv);
    }
    cmd_count = 0;
}

// Main program
int main(int argc,char **argv)
{
    cmd_help();
    loop();

    return 0;
}
