#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <threads.h>
#include <sys/stat.h>
#include <dirent.h>

/*General explanation - 
We'll hold a queue of directory struct to be scanned, as well as a queue of sleeping threads.
At the first phase of the program, we initilize the threads and make them wait for everyone to be initilized (then brodacting)
with yawn variable as such - if the thread thats been initilized was able to get to condition wait of the brodcast from the main thread, meaning it was able to
complete initilization, yawn will be 1 and it will wait for the brodcast, else yawn will be 0, meaning it is not been initilized yet and will wait for a signal to wake 
(named creation_cnd).
After everyone initilized at the first time (i.e initilized) they immidiatly wait for the main thread to send the to_start signal, and start the program.
Now each thread will check the queue of directories, dequeue what it can and scan it.
To keep fifo order of the threads we have the sleeping threads queue, every thread that is going to sleep will first insert it's id to the queue, and will be 
dequeued when and if some other thread will enqueue some directoty to the directory queue.
In order to avoid "thread has woke up just to sleep" we'll hold an "after sleep" array of directories. 
Every time a thread id is dequeued from the sleeping queue, there was a directory that has been dequeued, so it will be assigned to 
that array in the propper location and when the thread will wake up it'll start working on it. That guarentees at least initile work for woke thread.
Lastly, termination happens whan a counter of sleeping threads reaches the number of threads && the queue has no work in it.*/

/*Directory struct*/
typedef struct dir
{
    char *name;
    struct dir* next;
}dir;

/*queue of directories*/
typedef struct queue
{
    dir* head;
    dir* tail;
}queue;

/*Sleeping thread struct*/
typedef struct sleeping_thd
{
    long id;
    struct sleeping_thd* next;
}sleeping_thd;

/*queue of sleeping threads*/
typedef struct waiting_queue
{
    sleeping_thd* head;
    sleeping_thd* tail;
}waiting_queue;


static queue* q;
static waiting_queue* wake_up_q;
static char** thd_work;
static char* term;
static int yawn = 0;
static int waiting_for_queue = 0;
static int num_threads = 0;
static int count_term_found = 0;
static int error_occured = 0;

/*the locks and conditions:
1) initilisation (creation) of all lock.
2) start scanning together lock.
3) queues operations lock.
4) counting lock.
5) terminstion lock
6) thread condition wake.*/
cnd_t creation_cnd;
mtx_t creation_mtx;
cnd_t to_start_cnd;
mtx_t to_start_mtx;
mtx_t lock_q_mtx;
cnd_t lock_q_cnd;
mtx_t count_mtx;
cnd_t finish_cnd;
mtx_t finish_mtx;
cnd_t* conditions;

void scan_dir(char* d, long id);
/*enqueue path to directory queue, return 0 on success and -1 on failure*/
int enqueue(char* path)
{
    dir* d;
    d = malloc(sizeof *d);
    if(d == NULL)
    {
        return -1;
    }
    d->name = malloc((sizeof(char))*strlen(path)+1);
    strcpy(d->name, path);
    if(q->tail == NULL)
    {
        q->head = d;
        q->tail = d;
    }
    else
    {
        q->tail->next = d;
        q->tail = d;
    }
    d->next = NULL;
    return 0;
}

/*dequeue path from directory queue*/
char* dequeue()
{
    char* path;
    dir* d;
    d = q->head;
    if(q->head == NULL)
    {
        return NULL;
    }
    path = malloc(sizeof(char)*strlen(q->head->name)+1);
    strcpy(path,q->head->name);
    free(q->head->name);
    q->head = (dir*)q->head->next;
    if(q->head == NULL)
    {
        q->tail = NULL;
    }
    free(d);
    return path;
}
/*enqueue thread id to sleeping threads queue, return 0 on success and -1 on failure*/
int enqueue_sleeping(int id)
{ 
    sleeping_thd* thd;
    thd = malloc(sizeof*thd);
    if(thd == NULL)
    {
        error_occured = 1;
        return -1;
    }
    thd->id = id;
    if(wake_up_q->tail == NULL)
    {
        wake_up_q->head = thd;
        wake_up_q->tail = thd;
    }
    else
    {
        wake_up_q->tail->next = thd;
        wake_up_q->tail = thd;
    }
    thd->next = NULL;
    return 0;
}
/*dequeue thread id from directory queue*/
long dequeue_sleeping()
{
    sleeping_thd* thd;
    long id;
    if(wake_up_q->head == NULL)
    {
        return -1;
    }
    thd = wake_up_q->head;
    wake_up_q->head = wake_up_q->head->next;
    if(wake_up_q->head == NULL)
    {
        wake_up_q->tail = NULL;
    }
    id = thd->id;
    free(thd);
    return id;
}


int thread_func(void *thread_param) {
    /*lock to_start to make sure nobody starting in initilazation*/
    mtx_lock(&to_start_mtx);
    mtx_lock(&creation_mtx);
    /*yawn = 1 <-> was able to initilze, now waiting for the rest*/
    yawn = 1;
    /*signal created*/
    cnd_signal(&creation_cnd);
    mtx_unlock(&creation_mtx);
    /*waiting for broadcast*/
    cnd_wait(&to_start_cnd, &to_start_mtx);
    mtx_unlock(&to_start_mtx);
    char *path = malloc(sizeof(char)*PATH_MAX+1);
    long id = (long)thread_param;
    while(1)
    {
            /*lock the queue*/
            mtx_lock(&lock_q_mtx);
            if(q->head == NULL)
            {
                waiting_for_queue++;
                if(waiting_for_queue == num_threads)
                {
                    printf("Done searching, found %d files\n", count_term_found);
                    /*signal finished*/
                    cnd_signal(&finish_cnd);
                    if(error_occured)
                        exit(1);
                    exit(0);
                }
                /*no work to do - insert to queue and go to sleep*/
                enqueue_sleeping(id);
                cnd_wait(&conditions[id], &lock_q_mtx);
                /*thread woke up <-> someone called thread and assigned initial work to it*/
                strcpy(path,thd_work[id]);
                /*mark finished initile work*/
                strcpy(thd_work[id],"");
            }
            else
            {
                /*found something in public queue*/
                strcpy(path,dequeue());
            }
            mtx_unlock(&lock_q_mtx);  
            /*unlock queue and go to scan directory*/
            scan_dir(path, id);
        }
    }

void scan_dir(char* path, long id)
{
    /*https://stackoverflow.com/questions/20265328/readdir-beginning-with-dots-instead-of-files*/
    DIR *dirv;
    struct dirent *dp;
    dirv = opendir(path);
    if(dirv == NULL)
    {
        error_occured = 1;
        return;
    }
    int is_file;
    struct stat path_stat;
    char* to_print = malloc(sizeof(char)*PATH_MAX+1);
    char* tempath = malloc(sizeof(char)*PATH_MAX+1);
    while ((dp=readdir(dirv)) != NULL) {
        /*constract the whole path*/
        strcpy(to_print, path);
        strcat(to_print, "/");
        strcat(to_print, dp->d_name);
        if(!strcmp(dp->d_name,".") || !strcmp(dp->d_name,".."))
        {
            continue;
        }
        /*https://stackoverflow.com/questions/4989431/how-to-use-s-isreg-and-s-isdir-posix-macros*/
        if(stat(to_print, &path_stat) == -1)
        {
            continue;
        }
        is_file = S_ISDIR(path_stat.st_mode);
        if(!is_file)
        {
            /*check if term is a substring*/
            if (strstr(dp->d_name, term) != NULL)
            {
                mtx_lock(&count_mtx);
                count_term_found++;
                mtx_unlock(&count_mtx);
                printf("%s\n", to_print);
            }
        }
        else
        {
            /*it is a directory- check access*/
            if(access(to_print, X_OK | R_OK) < 0)
            {
                printf("Directory %s: Permission denied.\n", to_print);
            }
            else
            {
                /*can access - lock the queue lock, enqueue path, try to assign work to the oldest sleeping thread*/
                mtx_lock(&lock_q_mtx);
                strcpy(tempath, to_print);
                enqueue(tempath);
                long temp = dequeue_sleeping();
                if(temp >=0 && temp < num_threads)
                {
                    strcpy(thd_work[temp], dequeue());
                    waiting_for_queue--;
                    cnd_signal(&conditions[temp]);
                    mtx_unlock(&lock_q_mtx);

                }
                else{
                mtx_unlock(&lock_q_mtx);
                }
            }
        }
    }
    free(to_print);
    free(tempath);
    closedir(dirv);
}
int main(int argc, char* argv[])
{
    char* path;
    thrd_t *thread_ids;
    if(argc != 4)
    {
        fprintf(stderr, "Error - arcg is not 3");
        exit(EXIT_FAILURE);
    }
    path = argv[1];
    if(access(path, X_OK | R_OK) < 0)
    {
        fprintf(stderr, "Error - main path not accessible");
        exit(EXIT_FAILURE);
    }
    term = argv[2];
    num_threads = atoi(argv[3]);
    if(num_threads <= 0)
    {
        fprintf(stderr, "Error - invalid number of threads");
        exit(EXIT_FAILURE);
    }
    q = malloc(sizeof(queue));
    if(q == NULL)
    {
        fprintf(stderr, "Error - malloc queue");
        exit(EXIT_FAILURE);
    }
    q->head = NULL;
    q->tail = NULL;
    if(enqueue(path) < 0)
    {
        fprintf(stderr, "Error - couldn't enqueue initial path");
        exit(EXIT_FAILURE);
    }
    thread_ids = malloc((sizeof(thrd_t))*num_threads);
    thd_work = malloc(sizeof(char*)*num_threads);
    wake_up_q = malloc(sizeof(waiting_queue));
    conditions = malloc(sizeof(cnd_t)*num_threads);
    if(thread_ids == NULL ||  thd_work == NULL || wake_up_q == NULL || conditions == NULL)
    {
        fprintf(stderr, "Error - malloc");
        exit(EXIT_FAILURE);
    }
    wake_up_q ->head = NULL;
    wake_up_q ->tail = NULL;
    /*initilisations same as recitations 8,9*/
    mtx_init(&creation_mtx, mtx_plain);
    cnd_init(&creation_cnd);
    mtx_init(&to_start_mtx, mtx_plain);
    cnd_init(&to_start_cnd);
    mtx_init(&lock_q_mtx, mtx_plain);
    cnd_init(&lock_q_cnd);
    mtx_init(&count_mtx, mtx_plain);
    for (size_t i = 0; i < num_threads; i++) {
        int rc = thrd_create(&thread_ids[i], thread_func, (void*)i);
        cnd_init(&conditions[i]);
        thd_work[i] = malloc(sizeof(char)*PATH_MAX+1);
        strcpy(thd_work[i], "");
        if (rc != thrd_success) {
            fprintf(stderr, "Failed creating thread\n");
            exit(EXIT_FAILURE);
        }
        else
        {
           mtx_lock(&creation_mtx);
            if(yawn == 0)
                cnd_wait(&creation_cnd, &creation_mtx);
            yawn = 0;
           mtx_unlock(&creation_mtx);
        }
    }
    mtx_lock(&to_start_mtx);
    cnd_broadcast(&to_start_cnd);
    mtx_unlock(&to_start_mtx);  

    mtx_lock(&finish_mtx);
    cnd_wait(&finish_cnd, &finish_mtx);
    mtx_unlock(&finish_mtx);   

    for(size_t i=0; i < num_threads; i++)
    {
        free(thd_work[i]);
    }
    free(q);
    free(thread_ids);
    free(wake_up_q);
    free(conditions);
    if(error_occured)
    {
        exit(1);
    }   
    exit(0);
    return 0;
}