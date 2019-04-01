#define _POSIX_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>

#define BUFFSIZE 513

typedef struct program
{
    char *name;
    char **argv;
    int argc;
    bool parseError;
    char *outputFilePath;
    char *inputFilePath;
} Program;

typedef struct data
{
    bool end;
    char buff[BUFFSIZE];
    pthread_mutex_t mutex;
    pthread_mutex_t mutex_child;
    pid_t child_pid;
    pthread_cond_t condition;
    bool background;
} Data;

static Data data;

void condition_wait(Data *data)
{
    pthread_mutex_lock(&data->mutex);
    pthread_cond_wait(&data->condition, &data->mutex);
    pthread_mutex_unlock(&data->mutex);
}

void condition_signal(Data *data)
{
    pthread_mutex_lock(&data->mutex);
    pthread_cond_signal(&data->condition);
    pthread_mutex_unlock(&data->mutex);
}

void parse_args(Data *data, Program *program)
{
    //remove new line \n
    size_t n = strlen(data->buff) - 1;
    if (data->buff[n] == '\n')
        data->buff[n] = '\0';

    char *token;
    token = strtok(data->buff, " ");
    program->name = (char *)malloc(sizeof(char) * (strlen(token) +1));
    program->argv = (char **)malloc(sizeof(char *));
    program->argv[program->argc] = (char*)malloc(sizeof(char)* strlen(token));
    
    if(program->name == NULL || program->argv == NULL || program->argv[program->argc]==NULL){
        perror("malloc :");
        exit(1);
    }

    strcpy(program->name, token);
    strcpy(program->argv[program->argc], token);
    token = strtok(NULL, " ");
    
    while (token != NULL)
    {
        //backgound
        if (strcmp(token, "&") == 0)
        {
            data->background = true;
            //last split must be null
            if (strtok(NULL, " ") != NULL)
            {
                program->parseError = true;
            }
            break;
        }
        //outputfile
        else if (strcmp(token, ">") == 0)
        {
            token = strtok(NULL, " ");
            if (token == NULL || program->outputFilePath != NULL)
            {
                program->parseError = true;
                break;
            }
            program->outputFilePath = (char *)malloc(sizeof(char) * strlen(token));
            strcpy(program->outputFilePath, token);
        }
        //input file
        else if (strcmp(token, "<") == 0)
        {
            token = strtok(NULL, " ");
            if (token == NULL || program->inputFilePath != NULL)
            {
                program->parseError = true;
                break;
            }
            program->inputFilePath = (char *)malloc(sizeof(char) * strlen(token));
            strcpy(program->inputFilePath, token);
        }
        else{
            program->argc++;
            program->argv = (char **)realloc(program->argv, (program->argc+1)*sizeof(char*));
            
            //error handle
            if (program->argv == NULL)
            {
                data->end = true;
            }
            program->argv[program->argc] = (char*)malloc(sizeof(char)* (strlen(token) +1));
            if(program->argv[program->argc] == NULL){
                perror("malloc :");
                exit(1);
            }
            strcpy(program->argv[program->argc], token);
        }
        
        token = strtok(NULL, " ");
    }
    //NULL
    program->argv = (char **)realloc(program->argv, (program->argc+2)*sizeof(char*));
    /*
    for(size_t i = 0; i <= program->argc; i++)
    {
        printf("argv[%ld] = %s\n", i, program->argv[i]);
    }
    */
}

void exec_program(Data *data, Program *program)
{
    pid_t id;

    id = fork();

    if (id == 0)
    {
        //child
        int in,out = -1;
        if(program->outputFilePath != NULL){
            out = open(program->outputFilePath, O_WRONLY | O_TRUNC);
            if(out < 0){
                //create file
                FILE* fp = fopen(program->outputFilePath, "w");
                fclose(fp);
                //try again
                out = open(program->outputFilePath, O_WRONLY);
                if(out < 0){
                    perror("open error");
                    exit(1);
                }
            }
            if(dup2(out, STDOUT_FILENO) == -1){
                perror("dup2 error");
                exit(1);
            }
        }

        if(program->inputFilePath != NULL){
            in = open(program->inputFilePath, O_RDONLY);
            if(in < 0){
                perror("open error");
                exit(1);
            }
            if(dup2(in, STDIN_FILENO) == -1){
                perror("dup2 error");
                exit(1);
            }
        }
        
        execvp(program->name, program->argv);
        //error
        perror("execvp error");
        exit(1);
    }
    else if (id > 0)
    {
        //parent
        if (!data->background)
        {
            //Ctrl + C end process on foreground
            pthread_mutex_lock(&data->mutex_child);
            data->child_pid = id;
            pthread_mutex_unlock(&data->mutex_child);
            waitpid(id, NULL, 0);
        }
    }
    else
    {
        //fork error
        data->end = true;
        perror("fork error");
        exit(1);
    }
}

void *exec_thread_function(void *arg)
{
    Data *data = (Data *)arg;
    Program program;

    while (!data->end)
    {
        condition_wait(data);

        program.argc = 0;
        program.name = NULL;
        program.argv = NULL;
        data->background = false;
        program.parseError = false;
        program.outputFilePath = NULL;
        program.inputFilePath = NULL;

        parse_args(data, &program);

        if (strcmp("exit", data->buff) == 0)
        {
            data->end = true;
        }
        else if (program.parseError)
        {
            fprintf(stderr, "Parse error\n");
        }
        else
        {
            exec_program(data, &program);
        }

        condition_signal(data);
        //free memory
        free(program.name);
        free(program.outputFilePath);
        free(program.inputFilePath);
        
        for(size_t i = 0; i <= program.argc; i++)
            free(program.argv[i]);
        
        free(program.argv);
    }

    return (void *)0;
}

void *read_thread_function(void *arg)
{
    Data *data = (Data *)arg;
    int n = 0;

    while (!data->end)
    {
        printf(">$");
        fflush(stdout);
        memset(data->buff, 0, BUFFSIZE);

        n = read(STDIN_FILENO, data->buff, 513);

        data->buff[BUFFSIZE - 1] = '\0';
        if (n >= BUFFSIZE)
        {
            fprintf(stderr, "File too long!\n");
            while (getchar() != '\n')
                ;
            continue;
        }
        else if (n == 1)
        {
            //just new line
            continue;
        }

        condition_signal(data);
        condition_wait(data);
    }
    return (void *)0;
}

void sigint_handler(int sig)
{
    pthread_mutex_lock(&data.mutex_child);
    if (data.child_pid != 0)
    {
        kill(data.child_pid, SIGTERM);
    }
    data.child_pid = 0;
    pthread_mutex_unlock(&data.mutex_child);
}

void sigchld_handler(int sig)
{
    if (!data.background)
    {
        return;
    }
    int status;
    pid_t pid = wait(&status);
    if (WIFEXITED(status))
        fprintf(stderr, "Child %d terminated with status: %d\n", pid, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        fprintf(stderr, "Child %d received signal: %d\n", pid, WTERMSIG(status));
    else
        fprintf(stderr, "Child %d terminated\n", pid);

    printf(">$%s", data.buff);
    fflush(stdout);
}

int main(int argc, char const *argv[])
{
    pthread_t read_thread, exec_thread;
    pthread_attr_t attr;
    struct sigaction sigint, sigchild;

    data.end = false;
    data.child_pid = 0;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    sigint.sa_flags = 0;
    sigemptyset(&sigint.sa_mask);
    sigint.sa_handler = sigint_handler;

    if (sigaction(SIGINT, &sigint, NULL) == -1)
    {
        perror("SIGINT()");
        return (1);
    }

    sigchild.sa_flags = 0;
    sigemptyset(&sigchild.sa_mask);
    sigchild.sa_handler = sigchld_handler;

    if (sigaction(SIGCHLD, &sigchild, NULL) == -1)
    {
        perror("SIGCHLD()");
        return (1);
    }
    pthread_mutex_init(&(data.mutex), NULL);
    pthread_mutex_init(&(data.mutex_child), NULL);
    pthread_cond_init(&(data.condition), NULL);

    pthread_create(&read_thread, &attr, read_thread_function, &data);
    pthread_create(&exec_thread, &attr, exec_thread_function, &data);

    pthread_join(read_thread, NULL);
    pthread_join(exec_thread, NULL);

    pthread_mutex_destroy(&(data.mutex));
    pthread_mutex_destroy(&(data.mutex_child));
    pthread_cond_destroy(&(data.condition));
    pthread_attr_destroy(&attr);

    return 0;
}
