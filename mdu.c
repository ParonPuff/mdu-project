#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdbool.h>
#include <errno.h>
/**
 * @brief a stack implemented as a singly linked list
 * 
 */
typedef struct stack{
    char *path;
    struct stack *next;
} stack;

/**
 * @brief an object containing utilities for a thread object
 * 
 */
typedef struct utils{
    sem_t *availableJobs;
    stack **sharedStack;
    pthread_mutex_t *stackMutex;
    pthread_mutex_t *errorMutex;
    int *errorFlag;
    int *amountActive;
    pthread_cond_t *noneActive;
    pthread_cond_t *stackEmpty;
    int *sum;
} utils;

/**
 * @brief an object containg objects used by awaitFinish
 * 
 */
typedef struct awaitArguments{
    stack **sharedStack;
    pthread_mutex_t *stackMutex;
    pthread_cond_t *noneActive;
    pthread_cond_t *stackEmpty;
    int *amountActive;
} awaitArguments;

//**********UTILITY FUNCTIONS*************/
/**
 * @brief allocates memory
 * 
 * @param n a size
 * @return void* a pointer
 */
void *safeMalloc(size_t n);
/**
 * @brief duplicates a string
 * 
 * duplicates a string and stores its copy on the heap.
 * the returned string must be freed by the user
 * 
 * @param string a string
 * @return char* a string
 */
char *duplicateString(char *string);

/**
 * @brief sends a signal to a condition variable
 * 
 * @param condition a refrence to a condition variable
 */
void safeCondSignal(pthread_cond_t *condition);
/**
 * @brief awaits a signal to the condition variable
 * 
 * @param condition a refrence to a condition variable
 * @param mutex a refrence to a mutex
 */
void safeCondWait(pthread_cond_t *condition, pthread_mutex_t *mutex);

/***********MAIN LOOP STUFF***************/

/**
 * @brief get program options
 * 
 * @param argc the number of elements in argv
 * @param argv a string containg the desired options
 * @return int An integer representing the number of desired threads
 */
int getOptions(int argc, char *argv[]);

/**
 * @brief waits for threads to finish recursivley calculating size of directory or file
 * 
 * @param args an object containing the necessary arguments for awaitFinish
 */
void awaitFinish(awaitArguments *args);

/**********MUTLITHREADING STUFF************/

/**
 * @brief a threadpool used to calculate size of direcory or file
 * 
 * @param arg Arguments used by the threadPool
 * @return void* always NULL
 */
void *workLoop(void *arg);

/**
 * @brief traverses a directory and puts all file paths inside on the stack
 * 
 * @param toolBox a refrence to a utils object
 * @param path a string representing the path to a directory
 */
void traverseDir(utils *toolBox, char *path);

/**
 * @brief concatenates paths
 * 
 * given a path and the name of a file a string containing
 * "path/file" will be returned.
 * 
 * @param path a path
 * @param appendage a file name
 * @return char* a path
 */
char *concatPaths(char *path, char *appendage);

/*********STACK**********/

/**
 * @brief pushes a path onto the stack in toolBox 
 * 
 * @param path a path
 * @param toolBox a refrence to the utils object 
 */
void stackPush(char *path, utils *toolBox);

/**
 * @brief removes and returns a path from the stack in toolBox
 * 
 * @param toolBox a refrence to the utils object
 * @return char* a path
 */
char *stackPop(utils *toolBox);

int main(int argc, char *argv[]){

    //****************SETUP****************
    int numThreads = getOptions(argc, argv);

    pthread_t threads[numThreads];
    utils threadUtils[numThreads];

    sem_t availableJobs;
    stack *sharedStack = NULL;
    pthread_mutex_t stackMutex;
    pthread_mutex_t errorMutex;
    int errorFlag = 0;
    int amountActive = 0;
    pthread_cond_t noneActive;
    pthread_cond_t stackEmpty;

    pthread_mutex_init(&stackMutex, NULL);
    pthread_mutex_init(&errorMutex, NULL);

    if(pthread_cond_init(&noneActive, NULL) != 0){
        fprintf(stderr,"pthread_cond_init failed\n");
        exit(EXIT_FAILURE);
    }
    if(pthread_cond_init(&stackEmpty, NULL) != 0){
        fprintf(stderr,"pthread_cond_init failed\n");
        exit(EXIT_FAILURE);
    }

    if(sem_init(&availableJobs, 0, 0) != 0){
        perror("sem_init");
        exit(EXIT_FAILURE);
    }

    for(int i = 0; i < numThreads; i++){
        threadUtils[i].sum = safeMalloc(sizeof(int));
        threadUtils[i].availableJobs = &availableJobs;
        threadUtils[i].sharedStack = &sharedStack;
        threadUtils[i].stackMutex = &stackMutex;
        threadUtils[i].errorMutex = &errorMutex;
        threadUtils[i].errorFlag = &errorFlag;
        threadUtils[i].amountActive = &amountActive;
        threadUtils[i].noneActive = &noneActive;
        threadUtils[i].stackEmpty = &stackEmpty;

        if(pthread_create(threads +i, NULL, &workLoop, (void *)(threadUtils + i)) != 0){
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    awaitArguments awaitArgs = {
        .amountActive = &amountActive,
        .noneActive = &noneActive,
        .sharedStack = &sharedStack,
        .stackEmpty = &stackEmpty,
        .stackMutex = &stackMutex
    };

    //***********************MAIN LOOP*********************************
    for(char *path = argv[optind]; optind < argc; path = argv[++optind]){
        stackPush(duplicateString(path),threadUtils);
        awaitFinish(&awaitArgs);

        int sum = 0;

        for(int i = 0; i < numThreads; i++){
            sum += *threadUtils[i].sum;
            *threadUtils[i].sum = 0;
        }

        printf("%d\t%s\n",sum,path);
    }

    //********************CLEANUP*************************************
    for(int i = 0; i<numThreads; i++){
        sem_post(&availableJobs);
    }

    for(int i = 0; i < numThreads; i++){
        free(threadUtils[i].sum);

        if(pthread_join(threads[i],NULL) != 0){
            perror("pthread_join");
            exit(EXIT_FAILURE);
        }
    }

    pthread_mutex_destroy(&stackMutex);
    pthread_mutex_destroy(&errorMutex);

    if(pthread_cond_destroy(&noneActive) != 0){
        fprintf(stderr, "pthread_cond_destroy failed\n");
        exit(EXIT_FAILURE);
    }
    if(pthread_cond_destroy(&stackEmpty) != 0){
        fprintf(stderr, "pthread_cond_destroy failed\n");
        exit(EXIT_FAILURE);
    }

    if(sem_destroy(&availableJobs) != 0){
        perror("sem_destroy");
        exit(EXIT_FAILURE);
    }

    exit(errorFlag);
}

//**********UTILITY FUNCTIONS*************/

void *safeMalloc(size_t n){
    void *pointer;
    if((pointer = malloc(n)) == NULL){
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    return pointer;
}

char *duplicateString(char *string){
    char *newString;
    if((newString = strdup(string)) == NULL){
        perror("strdup");
        exit(EXIT_FAILURE);
    }
    return newString;
}

void safeCondSignal(pthread_cond_t *condition){
    if(pthread_cond_signal(condition) != 0){
        fprintf(stderr,"pthread_cond_signal failed\n");
        exit(EXIT_FAILURE);
    }
}

void safeCondWait(pthread_cond_t *condition, pthread_mutex_t *mutex){
    if(pthread_cond_wait(condition,mutex)){
        fprintf(stderr,"pthread_cond_wait failed\n");
        exit(EXIT_FAILURE);
    }
}
/***********MAIN LOOP STUFF***************/

int getOptions(int argc,char *argv[]){
    int opt;
    int numThreads = 1;

    while((opt = getopt(argc, argv, "j:")) != -1){
        switch(opt){
            case 'j':
                numThreads = atoi(optarg);
                if(numThreads < 1){
                    fprintf(stderr, "\"-j %d\" is invalid, the number of threads must be greater or equal to one\n",numThreads);
                    exit(EXIT_FAILURE);
                }

                break;
            default:
                fprintf(stderr, "Usage mdu -j {number of threads} {file} [files ...]\n");
                exit(EXIT_FAILURE);
                break;
        }
    }
    if(optind >= argc){
        fprintf(stderr, "Usage mdu -j {number of threads} {file} [files ...]\n");
        exit(EXIT_FAILURE);
    }
    return numThreads;
}

void awaitFinish(awaitArguments *args){
    pthread_mutex_lock(args -> stackMutex);
    stack **sharedStack = args -> sharedStack;
    while((*sharedStack) != NULL || (*args -> amountActive) > 0){

        if((*sharedStack) != NULL)
            safeCondWait(args ->stackEmpty,args -> stackMutex);
        if((*args -> amountActive) > 0)
            safeCondWait(args ->noneActive,args -> stackMutex);
    }
    pthread_mutex_unlock(args -> stackMutex);
}


/**********MUTLITHREADING STUFF************/

void *workLoop(void *arg){
    /**********INITALIZE VARIABLES**********/
    utils *toolBox = (utils *)arg;
    char *path;
    (*toolBox -> sum) = 0;

    /***********MAIN LOOP******************/
    while(1){
        if((path = stackPop(toolBox)) == NULL){
            return NULL;
        }

        struct stat buf;

        if(lstat(path,&buf) == 0){ 
            (*toolBox -> sum) += buf.st_blocks;

            if(S_ISDIR(buf.st_mode))
                traverseDir(toolBox, path);
        }
        else{
            perror(path);
            pthread_mutex_lock(toolBox -> errorMutex);
            (*toolBox -> errorFlag) = errno;
            pthread_mutex_unlock(toolBox -> errorMutex);
        }
        
        free(path);

        pthread_mutex_lock(toolBox -> stackMutex);

        (*toolBox -> amountActive)--;
        if((*toolBox -> amountActive) <= 0)
            safeCondSignal(toolBox -> noneActive);

        pthread_mutex_unlock(toolBox -> stackMutex);
    }
}

void traverseDir(utils *toolBox, char *path){
    DIR *directory;
    struct dirent *object;

    if((directory = opendir(path)) != NULL){
        while((object = readdir(directory)) != NULL){
            if(strcmp(object -> d_name,".\0") != 0 && strcmp(object -> d_name,"..\0") != 0)
            {
                char *totalPath = concatPaths(path,object -> d_name);
                stackPush(totalPath, toolBox);
            }
        }
        if(closedir(directory) != 0){
            perror(path);
            exit(EXIT_FAILURE);
        }
    }
    else{
        perror(path);
        pthread_mutex_lock(toolBox -> errorMutex);
        (*toolBox -> errorFlag) = errno;
        pthread_mutex_unlock(toolBox -> errorMutex);
    }

}
char *concatPaths(char *path, char *appendage){
    char *dest = safeMalloc(sizeof(char)*(strlen(path)+strlen(appendage)+2));
    if(sprintf(dest,"%s/%s",path,appendage) < 0){
        perror("sprintf");
        exit(EXIT_FAILURE);
    }
    return dest;
}

/*********STACK**********/
void stackPush(char *path, utils *toolBox){
    stack *newHead = safeMalloc(sizeof(stack));
    newHead -> path = path;

    pthread_mutex_lock(toolBox -> stackMutex);

    stack **sharedStack = toolBox -> sharedStack;
    newHead -> next = *sharedStack;
    *sharedStack = newHead;

    pthread_mutex_unlock(toolBox -> stackMutex);

    sem_post(toolBox -> availableJobs);
}

char *stackPop(utils *toolBox){
    char *retVal;
    sem_wait(toolBox -> availableJobs);

    pthread_mutex_lock(toolBox -> stackMutex);

    stack **sharedStack = toolBox -> sharedStack;

    if(*sharedStack == NULL){
        pthread_mutex_unlock(toolBox -> stackMutex);
        return NULL;
    }

    retVal = (*sharedStack) -> path;
    stack *newHead = (*sharedStack) -> next;
    free(*sharedStack);
    *sharedStack = newHead;

    if(newHead == NULL)
        safeCondSignal(toolBox -> stackEmpty);

    (*toolBox -> amountActive)++;

    pthread_mutex_unlock(toolBox -> stackMutex);

    return retVal;
}