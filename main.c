/**
 * @file main.c
 * @author Andrej Klocok (xkloco00@stud.fit.vutbr.cz)
 * @brief 
 * @version 0.1
 * @date 2019-04-26
 * 
 */
#define _GNU_SOURCE
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1 /* XPG 4.2 - needed for WCOREDUMP() */

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

#define BUFFSIZE 513 //line_size

/**
 * @brief Structure that represents program
 * 
 */
typedef struct program
{
    char *name;             // command`s name
    char **argv;            // command`s arguments
    int argc;               // number of arguments
    bool parseError;        // parse error occures
    char *outputFilePath;   // output file path
    char *inputFilePath;    // input file path
} Program;   

/**
 * @brief 
 * 
 */
typedef struct data
{
    bool end;                       // while(end)
    char buff[BUFFSIZE];            // input buffer
    pthread_mutex_t mutex;          // thread sync
    pthread_mutex_t mutex_child;    // child signal sync
    pid_t child_pid;                // pid of child
    pthread_cond_t condition;       // contidion variable
    bool background;                // execution of process in background
} Data;

//prototypes
void condition_wait(Data *data);
void condition_signal(Data *data);
void parse_args(Data *data, Program *program);
void exec_program(Data *data, Program *program);
void *exec_thread_function(void *arg);
void *read_thread_function(void *arg);
void sigint_handler(int sig);
void sigchld_handler(int sig);

//global variable (signals)
static Data data;

int main(int argc, char const *argv[])
{
    pthread_t read_thread, exec_thread;     // main threads
    pthread_attr_t attr;
    struct sigaction sigint, sigchild;      // signal actions

    // init data struct
    data.end = false;
    data.child_pid = 0;

    // init attr
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    // register sigint
    sigint.sa_flags = 0;
    sigemptyset(&sigint.sa_mask);
    sigint.sa_handler = sigint_handler;

    if (sigaction(SIGINT, &sigint, NULL) == -1)
    {
        perror("SIGINT()");
        return (1);
    }

    // register sigterm
    sigchild.sa_flags = 0;
    sigemptyset(&sigchild.sa_mask);
    sigchild.sa_handler = sigchld_handler;

    if (sigaction(SIGCHLD, &sigchild, NULL) == -1)
    {
        perror("SIGCHLD()");
        return (1);
    }
    //init mutex and condition variable
    pthread_mutex_init(&(data.mutex), NULL);
    pthread_mutex_init(&(data.mutex_child), NULL);
    pthread_cond_init(&(data.condition), NULL);

    //spawn threads
    pthread_create(&read_thread, &attr, read_thread_function, &data);
    pthread_create(&exec_thread, &attr, exec_thread_function, &data);

    //join
    pthread_join(read_thread, NULL);
    pthread_join(exec_thread, NULL);

    //destroy
    pthread_mutex_destroy(&(data.mutex));
    pthread_mutex_destroy(&(data.mutex_child));
    pthread_cond_destroy(&(data.condition));
    pthread_attr_destroy(&attr);

    return 0;
}

/**
 * @brief Thread synchronization function -> wait for condition variable.
 * 
 * @param data struct
 */
void condition_wait(Data *data)
{
    pthread_mutex_lock(&data->mutex);
    pthread_cond_wait(&data->condition, &data->mutex);
    pthread_mutex_unlock(&data->mutex);
}

/**
 * @brief Thread synchronization function -> signal condition variable.
 * 
 * @param data struct
 */
void condition_signal(Data *data)
{
    pthread_mutex_lock(&data->mutex);
    pthread_cond_signal(&data->condition);
    pthread_mutex_unlock(&data->mutex);
}

/**
 * @brief Function parses command arguments from data struct and stores it to program data.
 * 
 * @param data      struct
 * @param program   struct
 */
void parse_args(Data *data, Program *program)
{
    // remove new line \n
    size_t n = strlen(data->buff) - 1;
    if (data->buff[n] == '\n')
        data->buff[n] = '\0';
    
    char *pos_out, *pos_back, *pos_in;
    char* delim;

    //find occurences
    pos_back = strchr(data->buff, '&');
    pos_out = strchr(data->buff, '>');
    pos_in = strchr(data->buff, '<');
    
    if(pos_back != NULL){
        data->background = true;
        *pos_back = '\0';
    }

    if(pos_out != NULL){
        *pos_out = '\0';
        pos_out++;
    }

    if(pos_in != NULL){
        *pos_in = '\0';
        pos_in++;
    }

    // store pointers
    program->outputFilePath = pos_out;
    program->inputFilePath = pos_in;
    
    program->name = data->buff;
    program->argv = (char **)malloc(sizeof(char *));
    program->argv[program->argc] = data->buff;
    
    if(program->argv == NULL){
        perror("malloc :");
        exit(1);
    }

    //parse whole buffer
    delim = strchr(data->buff, ' ');
    char next;
    while(  delim!= NULL ){
        next = delim[1];
        
        if(next == ' '){
            *delim = '\0';
            delim++;
            delim = strchr(delim, ' ');
            continue;
        }
        if( next == '\0'){
            *delim = '\0';
            break;
        }
        program->argc++;
        program->argv = (char **)realloc(program->argv, (program->argc+1)*sizeof(char*));
        
        //error handle
        if (program->argv == NULL)
        {
            perror("malloc :");
            exit(1);
        }
        *delim = '\0';
        delim++;
        program->argv[program->argc] = delim;
        
        delim = strchr(delim, ' ');
    }
    
    //NULL
    program->argv = (char **)realloc(program->argv, (program->argc+2)*sizeof(char*));
    if (program->argv == NULL)
    {
        perror("malloc :");
        exit(1);
    }
    program->argv[program->argc+1] =NULL;
}

/**
 * @brief Function executes command described in program structure as child (fork).
 * 
 * @param data 
 * @param program 
 */
void exec_program(Data *data, Program *program)
{
    pid_t id;

    id = fork();

    if (id == 0)
    {
        //child
        int in,out = -1;
        //duplicate fd stdin, stdout
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
        //exec program
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
/**
 * @brief Thread function that parses data buffer and execudes commands.
 * 
 * @param arg       data structre
 * @return void*    status
 */
void *exec_thread_function(void *arg)
{
    Data *data = (Data *)arg;

    //thread cycle
    while (!data->end)
    {
        Program program;
        //wait for read thread
        condition_wait(data);
        
        //init struct
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
        //free memory
        free(program.argv);
        program.argv = NULL;
        condition_signal(data);
    }

    return (void *)0;
}
/**
 * @brief Read thread function reads input from stdin
 * 
 * @param arg       data structure
 * @return void*    status
 */
void *read_thread_function(void *arg)
{
    Data *data = (Data *)arg;
    int n = 0;

    //thread loop
    while (!data->end)
    {
        printf(">$");
        fflush(stdout);
        memset(data->buff, 0, BUFFSIZE);

        //read 513 chars
        n = read(STDIN_FILENO, data->buff, 513);
        
        //last char will be end of string char
        data->buff[BUFFSIZE-1] = '\0';
        if (n >= BUFFSIZE)
        {
            fprintf(stderr, "File too long!\n");
            while (getchar() != '\n');
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

/**
 * @brief SIGINT handler function
 * 
 * @param sig number
 */
void sigint_handler(int sig)
{
    pthread_mutex_lock(&data.mutex_child);
    if (data.child_pid != 0)
    {
        printf("\nKilling foreground process: %d\n", data.child_pid);
        kill(data.child_pid, SIGKILL);
    }
    data.child_pid = 0;
    pthread_mutex_unlock(&data.mutex_child);
}
/**
 * @brief SIGCHLD handler function
 * 
 * @param sig number
 */
void sigchld_handler(int sig)
{
    if (!data.background)
    {
        return;
    }
    int status;
    pid_t pid = wait(&status);
    if (WIFEXITED(status))
        printf("\nChild %d terminated with status: %d\n", pid, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("\nChild %d received signal: %d\n", pid, WTERMSIG(status));
    else
        printf("\nChild %d terminated\n", pid);
    
    printf(">$");
    fflush(stdout);
}