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
#include <math.h>

typedef struct stack{
    char *path;
    struct stack *next;
} stack;

typedef struct utils{
    sem_t *availableJobs;
    stack **sharedStack;
    pthread_mutex_t *stackMutex;
    sem_t *errorSemaphore;
    int *amountActive;
    pthread_cond_t *noneActive;
    pthread_cond_t *stackEmpty;
} utils;

typedef struct threadArgs{
    utils *toolBoxArray;
    uint arraySize;
}threadArgs;

typedef struct awaitArguments{
    stack **sharedStack;
    pthread_mutex_t *stackMutex;
    pthread_cond_t *noneActive;
    pthread_cond_t *stackEmpty;
    int *amountActive;
} awaitArguments;


/***********MAIN LOOP STUFF***************/

static inline int getOptions(int argc, char *argv[]);

static inline void awaitFinish(awaitArguments *args);

/**********MUTLITHREADING STUFF************/

/* taken from:
 * https://stackoverflow.com/questions/3272424/compute-fast-log-base-2-ceiling
*/
static inline int ceil_log2(unsigned long long x);
void *createThread(void *arg);

static inline int workLoop(utils *utilities);

static inline void traverseDir(utils *toolBox, char *path);

static inline char *concatPaths(char *path, char *appendage);

/*********STACK**********/

inline void stackPush(char *path, utils *toolBox);

static inline char *stackPop(utils *toolBox);

int main(int argc, char *argv[]){

    //****************SETUP****************
    int numThreads = getOptions(argc, argv);

    utils threadUtils[numThreads];

    sem_t availableJobs;
    stack *sharedStack = NULL;
    pthread_mutex_t stackMutex;
    sem_t errorSemaphore;
    int amountActive = 0;
    pthread_cond_t noneActive;
    pthread_cond_t stackEmpty;

    pthread_mutex_init(&stackMutex, NULL);

    pthread_cond_init(&noneActive, NULL);
    pthread_cond_init(&stackEmpty, NULL);

    sem_init(&availableJobs, 0, 0);
    sem_init(&availableJobs,0,0);
    for(int i = 0; i < numThreads; i++){
        threadUtils[i].availableJobs = &availableJobs;
        threadUtils[i].sharedStack = &sharedStack;
        threadUtils[i].stackMutex = &stackMutex;
        threadUtils[i].errorSemaphore = &errorSemaphore;
        threadUtils[i].amountActive = &amountActive;
        threadUtils[i].noneActive = &noneActive;
        threadUtils[i].stackEmpty = &stackEmpty;
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
        stackPush(strdup(path),threadUtils);

        threadArgs *arguments = malloc(sizeof(threadArgs));
        arguments -> arraySize = numThreads-1;
        arguments -> toolBoxArray = threadUtils;
        pthread_t thread;

        pthread_create(&thread,NULL,&createThread,(void *)arguments);

        awaitFinish(&awaitArgs);

        for(int i = 0; i<numThreads; i++){
            sem_post(&availableJobs);
        }
        int *sum = NULL;
        pthread_join(thread,(void *)&sum);
        if(sum){

            printf("%d\t%s\n",*sum,path);
            free(sum);
        }
    }
    //********************CLEANUP*************************************

    int errorFlag;
    sem_getvalue(&errorSemaphore, &errorFlag);

    sem_destroy(&errorSemaphore);

    pthread_mutex_destroy(&stackMutex);

    pthread_cond_destroy(&noneActive);
    pthread_cond_destroy(&stackEmpty);

    sem_destroy(&availableJobs);

    exit(errorFlag);
}

//**********UTILITY FUNCTIONS*************/

/***********MAIN LOOP STUFF***************/

static inline int getOptions(int argc,char *argv[]){
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

static inline void awaitFinish(awaitArguments *args){
    pthread_mutex_lock(args -> stackMutex);
    stack **sharedStack = args -> sharedStack;
    while((*sharedStack) != NULL || (*args -> amountActive) > 0){

        if((*sharedStack) != NULL)
            pthread_cond_wait(args ->stackEmpty,args -> stackMutex);
        if((*args -> amountActive) > 0)
            pthread_cond_wait(args ->noneActive,args -> stackMutex);
    }
    pthread_mutex_unlock(args -> stackMutex);
}


/**********MUTLITHREADING STUFF************/

/* taken from:
 * https://stackoverflow.com/questions/3272424/compute-fast-log-base-2-ceiling
*/
static inline int ceil_log2(unsigned long long x)
{
  static const unsigned long long t[6] = {
    0xFFFFFFFF00000000ull,
    0x00000000FFFF0000ull,
    0x000000000000FF00ull,
    0x00000000000000F0ull,
    0x000000000000000Cull,
    0x0000000000000002ull
  };

  int y = (((x & (x - 1)) == 0) ? 0 : 1);
  int j = 32;
  int i;

  for (i = 0; i < 6; i++) {
    int k = (((x & t[i]) == 0) ? 0 : j);
    y += k;
    x >>= k;
    j >>= 1;
  }

  return y;
}

void *createThread(void *arg){
    threadArgs *arguments = (threadArgs *)arg;
    int size = arguments ->arraySize;

    int numThreads = ceil_log2(size+1);
    pthread_t thread[numThreads];
    int count = 0; 
    for(int num = size; num > 0; num--){
        int newNum = num/2;
        num = newNum + num%2;
        threadArgs *newArgs = malloc(sizeof(threadArgs));
        newArgs -> arraySize = newNum;
        newArgs -> toolBoxArray = arguments -> toolBoxArray + num;
        pthread_create(thread + count ,NULL,&createThread,newArgs);
        count++;
    }
    int *sum = malloc(sizeof(int));
    (*sum) = workLoop(arguments -> toolBoxArray);

    int *recievedSum;
    for(int i = 0; i < count; i++){
        pthread_join(thread[i],(void *)&recievedSum);
        (*sum) += *recievedSum;
        free(recievedSum);
    }
    free(arguments);
    return sum;
}

static inline int workLoop(utils *toolBox){
    char *path;
    int sum = 0;

    while(1){
        if((path = stackPop(toolBox)) == NULL){
            return sum;
        }

        struct stat buf;

        if(lstat(path,&buf) == 0){ 
            sum += buf.st_blocks;

            if(S_ISDIR(buf.st_mode))
                traverseDir(toolBox, path);
        }
        else{
            perror(path);
            sem_post(toolBox -> errorSemaphore);
        }
        
        free(path);

        pthread_mutex_lock(toolBox -> stackMutex);

        (*toolBox -> amountActive)--;
        if((*toolBox -> amountActive) <= 0)
            pthread_cond_signal(toolBox -> noneActive);

        pthread_mutex_unlock(toolBox -> stackMutex);
    }
}

static inline void traverseDir(utils *toolBox, char *path){
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
        closedir(directory);
    }
    else{
        perror(path);
        sem_post(toolBox -> errorSemaphore);
    }

}
static inline char *concatPaths(char *path, char *appendage){
    char *dest = malloc(sizeof(char)*(strlen(path)+strlen(appendage)+2));
    sprintf(dest,"%s/%s",path,appendage);
    return dest;
}

/*********STACK**********/
inline void stackPush(char *path, utils *toolBox){
    stack *newHead = malloc(sizeof(stack));
    newHead -> path = path;

    pthread_mutex_lock(toolBox -> stackMutex);

    stack **sharedStack = toolBox -> sharedStack;
    newHead -> next = *sharedStack;
    *sharedStack = newHead;

    pthread_mutex_unlock(toolBox -> stackMutex);

    sem_post(toolBox -> availableJobs);
}

static inline char *stackPop(utils *toolBox){
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
        pthread_cond_signal(toolBox -> stackEmpty);

    (*toolBox -> amountActive)++;

    pthread_mutex_unlock(toolBox -> stackMutex);

    return retVal;
}