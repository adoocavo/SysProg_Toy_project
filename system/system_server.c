#include <stdio.h>
#include <sys/prctl.h>     //?? 왜 사용??
#include <sys/unistd.h>    //for getpid() syscall
#include <sys/types.h>     //for getpid() syscall
#include <sys/wait.h>      //for waitpid() syscall  
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <limits.h>
#include <mqueue.h>
#include <sys/stat.h>    //define mode constant (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH ....)
#include <fcntl.h>       //define O_* constants (O_RDONLY, O_CREAT....)
#include <semaphore.h>
#include <sys/types.h>    /*for potability*/
#include <sys/shm.h> 
#include <sys/mman.h>
#include <sys/inotify.h>
#include <dirent.h>

#include "/usr/include/linux/limits.h"        /* ??? : for NAME_MAX?*/
#include "./../hal/camera_HAL.h"
#include "system_server.h"
#include "./../ui/gui.h"
#include "./../ui/input.h"
#include "./../web_server/web_server.h"
#include "./../project_libs/time/currTime.h"
#include "./../project_libs/toy_message.h"
#include "./../project_libs/shared_memory.h"


#define TIMER_SIG SIGRTMAX      //POSIX RTS 사용
#define NUM_OF_THREADS 5

/***************************************************************************************/
/********************************  SV shm 괸련 선언 - start ********************************/
/***************************************************************************************/

//extern int shm_id[SHM_KEY_MAX - SHM_KEY_BASE];

static shm_sensor_t *the_sensor_info = NULL; 
static shm_str_msg_t *the_str_msg_info = NULL;


/***************************************************************************************/
/******************************** SV shm 괸련 선언 - end ********************************/
/***************************************************************************************/




/***************************************************************************************/
/******************************** message queue괸련 선언  - start ********************************/
/***************************************************************************************/

/** feature : system_server proc 내에서 생성되는 threads들의 msg queue mqd저장
 * @note idx 0~4 순서대로 watchdog_mqd, monitor_mqd, disk_mqd, camera_mqd
*/
#define NUM_OF_MQ 4             //open할 message queue file 개수
static mqd_t mqds[NUM_OF_MQ] = {
            0,
};

/** featute : 생성할 message queue filename 저장 ary
 * 
*/
static char *msg_queues_str[] = {
    "/watchdog_mq",
    "/monitor_mq",
    "/disk_mq",
    "/camera_mq"
};

/** feature : input command에 대한 처리 요청 후, 처리 종료여부 전달받기 위한 msg queue
 * @note 
*/
static mqd_t prompt_perm_mqd = 0;
static const char *prompt_perm_filename = NULL;

/***************************************************************************************/
/******************************** message queue괸련 선언 - end ********************************/
/***************************************************************************************/


/***************************************************************************************/
/******************************** timer signal 처리 괸련 선언  - start ********************************/
/***************************************************************************************/

/*
int posix_sleep_ms(unsigned int timeout_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / MILLISEC_PER_SECOND;
    sleep_time.tv_nsec = (timeout_ms % MILLISEC_PER_SECOND) * (NANOSEC_PER_USEC * USEC_PER_MILLISEC);

    return nanosleep(&sleep_time, NULL);
}
*/

/**
 * @note Set nonzero on receipt of SIGALRM : SIGALRM -> toy_timer = 1;
*/
static int toy_timer = 0;  
static bool global_timer_stopped = false;       //ALARM 발생시 log 처리 여부 : false(log 기록해라)

/** feature : signal_exit
 * @note alarm handler 종료시 log 출력하도록 alarm_log_thread에 pthread_cond_signal()
*/
void system_timeout_handler(void);

/** feature : TIMER_SIG handler (timer_expire_signal_handler)
 * @param {void} 
 * @return {int} 0
 * @todo : tick값() 출력 
*/
static void timerSig_handler(int sig, siginfo_t *si, void *uc);

void set_create_peridicTimer(long initial_sec, long initial_usec, long interval_sec, long interval_usec);

/** feature : signal_exit
 * @note alarm handler 종료시 log 출력하도록 alarm_log_thread에 pthread_cond_signal()
*/
void signal_exit(void);

/**
 * 
*/
const int get_totalSize_of_dir(const char *);


/***************************************************************************************/
/******************************** timer signal 처리 괸련 선언 - end ********************************/
/***************************************************************************************/


/***************************************************************************************/
/******************************** unnamed semaphore 괸련 선언  - start ********************************/
/***************************************************************************************/

//1. sem_t 변수 선언 
static sem_t timeout_handler_sem;
sem_t sem_for_monitor;

/***************************************************************************************/
/******************************** unnamed semaphore 괸련 선언 - end ********************************/
/***************************************************************************************/


/***************************************************************************************/
/******************************** mutex lock 괸련 선언  - start ********************************/
/***************************************************************************************/

/** feature : system_timeout_handler 수행관련 mutex 변수
 *  
*/
pthread_mutex_t toy_timer_mutex = PTHREAD_MUTEX_INITIALIZER;
// pthread_cond_t  toy_timer_cond = PTHREAD_COND_INITIALIZER;


/** feature : alarm_log_thread 수행관련 mutex 변수
 *  
*/
pthread_mutex_t system_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  system_loop_cond  = PTHREAD_COND_INITIALIZER;
bool            system_loop_exit = false;    ///< true if main loop should exit

/***************************************************************************************/
/******************************** mutex lock 괸련 선언 - end ********************************/
/***************************************************************************************/


/** feature : thread_func  
 * @note thread_funcs,threads_name idx 순서대로 ~
*/
void * watchdog_thread_func(void *);
void * monitor_thread_func(void *);
void * disk_service_thread_func(void *);
void * camera_service_thread_func(void *);
void * timer_thread_func(void *);



/** thread_func array 
 *  @note idx 순서대로 
*/
void * (*thread_funcs[NUM_OF_THREADS]) (void *) = {
    watchdog_thread_func,
    monitor_thread_func,
    disk_service_thread_func,
    camera_service_thread_func,
    timer_thread_func,
    
};

/** threads_name ary
 * @note idx 순서대로 
*/
char* threads_name[NUM_OF_THREADS] = {
    "watchdog_thread",
    "monitor_thread",
    "disk_service_thread",
    "camera_service_thread",
    "timer_thread"
};

/** feature : main process가 생성한 모든 process monitoring 
 * @param {void} 
 * @return {int} 0
 * @todo 
*/
int system_server()
{
    printf("나 system_server 프로세스!\n");
    
    /****************************************************************************************************/
    /************************************* message queue open - start *******************************************/
    /****************************************************************************************************/
    for(int i = 0; i < NUM_OF_MQ; ++i)
    {
        if(i == 1)
        {
            mqds[i] = mq_open(msg_queues_str[i], O_RDWR);
            continue; 
        }
        mqds[i] = mq_open(msg_queues_str[i], O_RDONLY);
        assert(mqds[i] != -1);
    }
    //prompt_perm_mqd = mq_open(prompt_perm_filename, O_RDWR);

    /****************************************************************************************************/
    /************************************* message queue open - end *******************************************/
    /****************************************************************************************************/



    /****************************************************************************************************/
    /******************* watchdog, monitor, disk_serviced, camera_service threads 생성 *******************/
    /****************************************************************************************************/
    //1. thread 초기 설정 - pthread_t, pthread_attr_t

    //1_1. pthread var 선언
    ///=> idx 0~3 순서로 watchdog, monitor, disk_serviced, camera_service threads 
    pthread_t tids[NUM_OF_THREADS];               //thread id
    pthread_attr_t attrs[NUM_OF_THREADS];         //thread attributes object

 
    //1_2. attr 설정
    for(int i = 0; i < NUM_OF_THREADS; ++i) 
    {
        //1. assign default values
        if(pthread_attr_init(&attrs[i])) perror("pthread_attr_init");

        //2. set detached 
        if(pthread_attr_setdetachstate(&attrs[i], PTHREAD_CREATE_DETACHED)) perror("pthread_attr_setdetachstate");
    }    


    //2. thread 생성 - pthread_create()
    for(int i = 0; i < NUM_OF_THREADS; ++i) 
    {

        // if(i == 2) continue;    //disk 출력 thread 생성X
        if(pthread_create(&tids[i], &attrs[i], thread_funcs[i], (void *)threads_name[i])) perror("pthread_create");
    }
    /****************************************************************************************************/
    /******************* watchdog, monitor, disk_serviced, camera_service threads 생성 *******************/
    /****************************************************************************************************/

    printf("system init done.  waiting...\n");


    // 여기에 구현하세요... 여기서 cond wait로 대기한다. 10초 후 알람이 울리면 <== system 출력
    pthread_mutex_lock(&system_loop_mutex);
    while (system_loop_exit == false) 
    {
        pthread_cond_wait(&system_loop_cond, &system_loop_mutex);
    }
    pthread_mutex_unlock(&system_loop_mutex);

    printf("<== system\n");

    printf("main thread : busy waiting\n");
    while(1) 
    {
        sleep(10);
        //posix_sleep_ms(5000);
    }
    
    return 0;
}





/** feature : system server process 생성 
 * @param {void} 
 * @return {int} 0
 * @todo 해당 함수 내부에서 parent process의 흐름이 멈추지 않고, child process 종료를 확인하도롣 구현 (by sig handler)
*/
int create_system_server()
{

    pid_t systemPid;                        //local var : system server process's PID
    const char *name = "system_server";     
  
    /**
     * @note fork -> child : system server process 수행 
     *              parent : system server process 생성 후 wait
    */ 
    // child process
    printf("여기서 system_server 프로세스를 생성합니다.\n");   
    if((systemPid = fork()) == 0)           
    {
        //printf("child(system_server) process!,  PID : %d\n", getpid());
        
        
        if(prctl(PR_SET_NAME, (unsigned long) name) < 0) perror("prctl()");
        
        ////system server process 동작 시작  
        system_server();     
    }

    else if(systemPid == -1) perror("fork");
    
    // parent process   
    else                                    
    {
        int childStatus;                   /** local var : system server process의 terminating 상태 저장*/
        int child_wPid;                    /** local var : system server 기다린 이후, return되는 child Pid 저장*/

        //printf("parent process!,  PID : %d\n", getpid());
        
        return systemPid;

    }

    return 0;
}



/** feature : watchdog_thread_func definition 
 * 
*/
void * watchdog_thread_func(void *arg)
{  
    char *str = arg;
    printf("나 %s\n", str);

    /** feature : receive data form "/watchdog_mq" (message queue를 사용한 data IPC)
     * @note mq_getattr().mq_msgsize : receive 하여 저장할 buffer size 지정 위해 사용
     * @note unsigned int prio : receive 받은 message의 우선 순위 저장
     * @note ssize_t numRead : 몇 byte message 수신했는지 저장
     * @note toy_msg_t *received_msg_buffer : mq_receive()로 받은 data를 저장할 buffer
    */
    //0. open mq file in parent process

    //1. receive 하기위한 setting : mq_getattr() -> received_msg_buffer 동적할당
    struct mq_attr attr;
    unsigned int prio_of_msg;
    toy_msg_t *received_msg_buffer;
    unsigned int current_msg_num;
    struct timespec set_timeout; 

    if(mq_getattr(mqds[0], &attr) == -1) perror("mq_getattr(watchdog_thread_func)");
    received_msg_buffer = malloc(attr.mq_msgsize);
    set_timeout.tv_sec = 500;
    set_timeout.tv_nsec = 0;

    //current_msg_num = attr.mq_curmsgs;

    //2. receive
    ssize_t numRead;           //몇 byte message 수신했는지 저징

    while(1)
    {
        //2_1. receive
        numRead = mq_receive(mqds[0], (char *)received_msg_buffer, attr.mq_msgsize, &prio_of_msg);
        //numRead = mq_timedreceive(mqds[0], received_msg_buffer, attr.mq_msgsize, &prio_of_msg, &set_timeout);
        assert(numRead != -1);

        //2_2. 받은 message 출력
        printf("(%s) Read %ld bytes; priority : %u\n\n", msg_queues_str[0], numRead, prio_of_msg);

        printf("msg_type : %d\n", ((toy_msg_t*)received_msg_buffer)->msg_type);
        printf("param1 : %d\n", ((toy_msg_t*)received_msg_buffer)->param1);
        printf("param2 : %d\n", ((toy_msg_t*)received_msg_buffer)->param2);

        printf("\n");
    }


    return NULL;
}

/** feature : monitor_thread_func definition 
 * 
*/
#define SENSOR_DATA 1
#define CMD_DATA_R_ELF 2 
#define DISK_INFO 3

void * monitor_thread_func(void *arg)
{  
    char *str = arg;
    printf("나 %s\n", str);

    /** feature : receive data form "/monitor_mq" (message queue를 사용한 data IPC)
     * @note mq_getattr().mq_msgsize : receive 하여 저장할 buffer size 지정 위해 사용
     * @note unsigned int prio : receive 받은 message의 우선 순위 저장
     * @note ssize_t numRead : 몇 byte message 수신했는지 저장
     * @note toy_msg_t *received_msg_buffer : mq_receive()로 받은 data를 저장할 buffer
     * @note key_t shm_key : for shmat() 
  
    */
    //0. open mq file in parent process

    //1. receive 하기위한 setting : mq_getattr() -> received_msg_buffer 동적할당
    struct mq_attr attr;
    unsigned int prio_of_msg;
    toy_msg_t *received_msg_buffer;
    unsigned int current_msg_num;
    struct timespec set_timeout; 
    key_t shm_key;
    int retcode;



    if(mq_getattr(mqds[1], &attr) == -1) perror("mq_getattr(monitor_thread_func)");
    received_msg_buffer = malloc(attr.mq_msgsize);
    
    set_timeout.tv_sec = 500;
    set_timeout.tv_nsec = 0;

    //2. receive
    ssize_t numRead;           //몇 byte message 수신했는지 저징
    
    while(1)
    {
        //2_1. receive
        numRead = mq_receive(mqds[1], (char *)received_msg_buffer, attr.mq_msgsize, &prio_of_msg);
        //numRead = mq_timedreceive(mqds[1], received_msg_buffer, attr.mq_msgsize, &prio_of_msg, &set_timeout);

        assert(numRead != -1);

        //2_2. 받은 message 출력
        printf("\n\n/******************** monitor - start ********************/");

        printf("\n[%s] Read %ld bytes; priority : %u\n\n", currTime("%T"), numRead, prio_of_msg);

        printf("msg_type : %d\n", received_msg_buffer->msg_type);
        printf("param1(sensor_shm_key) : %d\n", received_msg_buffer->param1);
        printf("param2 : %d\n", received_msg_buffer->param2);


        /** feature : SENSOR_DATA 출력
         * 
         * 
         * 
         */ 
        if (received_msg_buffer->msg_type == SENSOR_DATA) 
        {
            printf("/******************** sensor *************************/\n");

            shm_key = received_msg_buffer->param1;

            //3_1. attaching to monitor_thread(input process)
            the_sensor_info = (shm_sensor_t *)shmat(shm_key, NULL, SHMAT_FLAGS_R);

            //3_2. 출력
            printf("temp : %d Celsius\n", the_sensor_info->temp);
            printf("press : %d mV\n", the_sensor_info->press);
            printf("humidity : %d RH\n", the_sensor_info->humidity);

        }

        /** feature : CMD_DATA_R_ELF 출력ㄴ
         * 
         * 
         * 
         */ 
        if (received_msg_buffer->msg_type == CMD_DATA_R_ELF) 
        {
            printf("/******************** ELF file *************************/\n");

            /** feature : shmat()을 사용하여 전달받을 string data가 저장된 shm의 key 얻기  
             * 
            */
            char filename[SHM_STR_MSG_BUF_SIZE];
            shm_key = received_msg_buffer->param1;

            //3_1. attaching to monitor_thread(input process)
            the_str_msg_info = (shm_str_msg_t *)shmat(shm_key, NULL, SHMAT_FLAGS_R);

            //3_2. read할 filename 저장 + detach
            strncpy(filename, the_str_msg_info->buf, the_str_msg_info->cnt);
            retcode = shmdt((const void *)the_str_msg_info);
            assert(retcode != -1);

            /** feature : open() -> mmap() 사용하여 전달받은 file 출력 
             * @note : Elf64Hdr *mapped_base_addr;      //process에 mapping된 시작 주소(vms) 저장  
             * 
            */
            Elf64Hdr *mapped_base_addr;      //process에 mapping된 시작 주소(vms) 저장  
            int fd;
            
            //1. open
            fd = open((const char *)filename, O_RDONLY);
            if(fd < 0 ) 
            {
                perror("open");
            }

            //2. mmap()
            mapped_base_addr = (Elf64Hdr *)mmap(NULL, sizeof(Elf64Hdr), PROT_READ, MAP_PRIVATE, fd, 0);

            //3. 출력
            printf("Object file type : %u\n", mapped_base_addr->e_type);
            printf("Architecture : %u\n", mapped_base_addr->e_machine);
            printf("Object file version : %u\n", mapped_base_addr->e_version);
            printf("Entry point virtual address : %u\n", mapped_base_addr->e_entry);
        }



        /** feature : DISK_INFO 출력
         * 
         * 
         * 
         */ 
        if (received_msg_buffer->msg_type == DISK_INFO) 
        {
            printf("/******************** DISK_INFO *************************/\n");

            /** feature : shmat()을 사용하여 전달받을 DISK data가 저장된 shm의 key 얻기  
             * 
            */
            shm_diskInfo_msg_t *shm_diskInfo_msg_ptr; 
            shm_key = received_msg_buffer->param1;

            //1. attaching to monitor_thread(input process)
            shm_diskInfo_msg_ptr = (shm_diskInfo_msg_t *)shmat(shm_key, NULL, SHMAT_FLAGS_R);

            //2. 출력
            printf("Read %ld bytes from inotify fd\n", (long) (shm_diskInfo_msg_ptr->readByte));
            printf("        created filename = %s\n", shm_diskInfo_msg_ptr->filename);
            printf("\nTotal Directory size : %ld\n", (long) (shm_diskInfo_msg_ptr->totalSize_of_dir));
        
            retcode = shmdt((const void *)shm_diskInfo_msg_ptr);
            assert(retcode != -1);

        }

        printf("/******************** monitor - end ********************/\n\n");

    }

    return NULL;
}

/** feature : disk_service_thread_func definition 
 * @note  10초마다 disk 사용량 출력
*/
#define TARGET_DIR "/home/kahngju/devcourse/SysProg/Toy_project/toy/fs"
// #define INOTIFY_EVENT_BUF_LEN (10 * sizeof(struct dirent))      

/**
 * @note 1번 수신할 때, 여러개의 event(여러개의 event가 monitoring 대상)가 존재 가능 ->  num of event queue 내의 msg
 * @note num of event queue * (sizeof(struct inotify_event) + (max filename) + (1 : for null)  ) 
*/
#define INOTIFY_EVENT_BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1)) 

void * disk_service_thread_func(void *arg)
{  
    char *str = arg;
    printf("나 %s\n", str);
    
    /** feature : (detecting the file || directory event) + (direc open -> r/w)
     * @note inotify_init() : return fd refering to inotify event queue
     * @note inotify_add_watch() : return a fd of watching file(used to manipulate or remove the watch)
     * @note event queue에 watching fd를 추가 -> fd에 대한 변화 monitoring
     * 
     * @note opendir(TARGET_DIR) : retunrn the direc stream opened
     * @note readdir(DIR *dirp) : read the next entry from a directory stream(DIR *)
    */
    //1. inotify_init() -> inotify_add_watch() : TARGET_DIR를 watch directory로 지장
    int disk_inotify_fd = inotify_init();             /* inotify event queue에 대한 fd  */
    int watch_dir_fd = inotify_add_watch(disk_inotify_fd, TARGET_DIR, IN_CREATE);
    // int watch_dir_fd = inotify_add_watch(disk_inotify_fd, TARGET_DIR, IN_CREATE | IN_MODIFY);

    //2. opendir()   
    //DIR *dir_stream_ptr = opendir(TARGET_DIR);

    //3.
    shm_diskInfo_msg_t *shm_diskInfo_msg_ptr; 
    int shm_id = shmget(SHM_KEY_DISK, sizeof(shm_diskInfo_msg_t), SHMGET_FLAGS_CREAT);
    shm_diskInfo_msg_ptr = (shm_diskInfo_msg_t*)shmat(shm_id, NULL, SHMAT_FLAGS_RW);

    
    //4.inotify event queue에 등록된 event monitoring
    int readByte_of_inotifyEvent;
    char read_inotifyEvent_buffer[INOTIFY_EVENT_BUF_LEN];
    char *p;
    struct inotify_event *event;
    while(1)
    {

        /****************************************************************************************************/
        /************************************* inotify로 받은 event정보 shm에 저장 - start *******************************************/
        /****************************************************************************************************/
        //1. event 발생
        readByte_of_inotifyEvent = read(disk_inotify_fd, read_inotifyEvent_buffer, INOTIFY_EVENT_BUF_LEN);
        if(readByte_of_inotifyEvent == -1) perror("read(disk_inotify_fd)");
        if(readByte_of_inotifyEvent == 0) perror("read(return 0 byte)");

        /** ??? : 2개 이상의 event를 shm에 저장하는 방법??
         * 
        */
        //2. readByte 저장
        shm_diskInfo_msg_ptr->readByte = readByte_of_inotifyEvent;

        for (p = read_inotifyEvent_buffer; p < read_inotifyEvent_buffer + readByte_of_inotifyEvent; ) 
        {
            event = (struct inotify_event *) p;
            
            //3. event 발생시킨 filename 저장(IN_CREATE : 생성된 filen ame)
            // strncpy(shm_diskInfo_msg_ptr->filename, event->name, strlen(event->name));
            strncpy(shm_diskInfo_msg_ptr->filename, event->name, strlen(event->name)+1);

            p += sizeof(struct inotify_event) + event->len;
        }        
        /****************************************************************************************************/
        /************************************* inotify로 받은 event정보 shm에 저장 - start *******************************************/
        /****************************************************************************************************/
        
        /****************************************************************************************************/
        /************************************* 현재 direc내의 모든 file 용량 저장 - start *******************************************/
        /****************************************************************************************************/ 
        int totalSize;

        totalSize = get_totalSize_of_dir(TARGET_DIR);
        shm_diskInfo_msg_ptr->totalSize_of_dir = totalSize;
        /****************************************************************************************************/
        /************************************* 현재 direc내의 모든 file 용량 저장 - start *******************************************/
        /****************************************************************************************************/ 



        /***************************************************************************************/
        /******************************** message queue 전송(send) - start ********************************/
        /***************************************************************************************/
        //0. open 
        toy_msg_t msg_to_monitor;
        int mq_retcode;

        //1. send : 생성된 shm segment's kev value(seg id)를 전송
        msg_to_monitor.msg_type = 3;
        msg_to_monitor.param1 = shm_id;
        msg_to_monitor.param2 = 0;
        msg_to_monitor.param3 = NULL;


        //sem_wait(&sem_for_monitor);
       
        mq_retcode = mq_send(mqds[1], (const char *)&msg_to_monitor, sizeof(msg_to_monitor), 0);
        // mq_retcode = mq_send(mqds[1], &msg_to_monitor, MQ_MSG_SIZE, 0);
        if(mq_retcode == -1) perror("mq_retcode");
        assert(mq_retcode != -1);

        //sem_post(&sem_for_monitor);

        /***************************************************************************************/
        /******************************** message queue 전송(send) - end ********************************/
        /***************************************************************************************/

    }

    
    return NULL;
}

/** feature : camera_service_thread_func definition 
 * 
*/
#define CAMERA_TAKE_PICTURE 1
void * camera_service_thread_func(void *arg)
{  
    char *str = arg;
    printf("나 %s\n", str);

    toy_camera_open();

    /** feature : receive data form "/camera_mq" (message queue를 사용한 data IPC)
     * @note mq_getattr().mq_msgsize : receive 하여 저장할 buffer size 지정 위해 사용
     * @note unsigned int prio : receive 받은 message의 우선 순위 저장
     * @note ssize_t numRead : 몇 byte message 수신했는지 저장
     * @note toy_msg_t *received_msg_buffer : mq_receive()로 받은 data를 저장할 buffer
    */
    //0. open mq file in parent process

    //1. receive 하기위한 setting : mq_getattr() -> received_msg_buffer 동적할당
    struct mq_attr attr;
    unsigned int prio_of_msg;
    toy_msg_t *received_msg_buffer;
    unsigned int current_msg_num;
    struct timespec set_timeout; 


    if(mq_getattr(mqds[3], &attr) == -1) perror("mq_getattr(camera_service_thread_func)");
    received_msg_buffer = malloc(attr.mq_msgsize);
    set_timeout.tv_sec = 500;
    set_timeout.tv_nsec = 0;
    //current_msg_num = attr.mq_curmsgs;

    //2. receive
    ssize_t numRead;           //몇 byte message 수신했는지 저징

    while(1)
    {
        //2_1. receive
        numRead = mq_receive(mqds[3], (char *)received_msg_buffer, attr.mq_msgsize, prio_of_msg);
        //numRead = mq_timedreceive(mqds[3], received_msg_buffer, attr.mq_msgsize, &prio_of_msg, &set_timeout);
        assert(numRead != -1);

        //2_2. 받은 message 출력
        printf("(%s) Read %ld bytes; priority : %u\n\n", msg_queues_str[3], numRead, prio_of_msg);

        printf("msg_type : %d\n", ((toy_msg_t*)received_msg_buffer)->msg_type);
        printf("param1 : %d\n", ((toy_msg_t*)received_msg_buffer)->param1);
        printf("param2 : %d\n", ((toy_msg_t*)received_msg_buffer)->param2);

        if(((toy_msg_t*)received_msg_buffer)->msg_type == CAMERA_TAKE_PICTURE) toy_camera_take_picture();

        printf("\n");


    }

    return NULL;
}


/** feature : timer_thread definition 
 * @note alarm 올때마다 Log 찍는 기능 따로 구현
*/
void * timer_thread_func(void *arg)
{  
    sleep(5);
    char *str = arg;
    printf("나 %s\n", str);

    /****************************************************************************************************/
    /******************* 1초 타이머 생성 + TIMER_SIG handler 등록 *******************************************/
    /****************************************************************************************************/

    //1. sig hanlder 등록(TIMER_SIG)
    struct sigaction sa;
    timer_t *tidlist;
    memset(&sa, 0, sizeof(sigaction));

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = timerSig_handler;

    if(sigaction(TIMER_SIG, &sa, NULL) == -1) perror("sigaction");

    //2. init semaphore
    sem_init(&timeout_handler_sem, 0, 0); 
    
    //3. set + create timer
    //set_create_peridicTimer(1, 0, 1, 0);
    /****************************************************************************************************/
    /******************* 1초 타이머 생성 + TIMER_SIG handler 등록 *******************************************/
    /****************************************************************************************************/

    int retcode;
	while (!global_timer_stopped) 
    {
        int retcode = sem_wait(&timeout_handler_sem);
		if (retcode == -1 && errno == EINTR)   //EINTR : 시스템 콜 수행중 인터럽트가 걸려 수행이 중단된 경우
        {
		    continue;
		}
		if (retcode == -1) 
        {
		    perror("sem_wait");
		    exit(-1);
		}

		system_timeout_handler();
        signal_exit();
	
        sem_init(&timeout_handler_sem, 0, 0);

    }
	return 0;
}


/** feature : TIMER_SIG handler (timer_expire_signal_handler)
 * @param {void} 
 * @return {int} 0
 * @todo : tick값() 출력 
*/
static void timerSig_handler(int sig, siginfo_t *si, void *uc)
{
    sem_post(&timeout_handler_sem);
}

/** feature : system_timeout_handler
 * @note alarm handler 종료시 log 출력하도록 alarm_log_thread에 pthread_cond_signal()
*/
void system_timeout_handler(void)
{
    
    printf("\n\n\n\n/**************** system_timeout_handler - start ****************/\n\n");

    pthread_mutex_lock(&toy_timer_mutex);
    
    toy_timer++;
    printf("[%s] toy_timer: %d\n", currTime("%T"), toy_timer);
    
    pthread_mutex_unlock(&toy_timer_mutex);    

    printf("\n/**************** system_timeout_handler - end ****************/\n");

}

/** feature : timer set + create 
 * @param {long} initial_sec, initial_usec, interval_sec, interval_usec
 * @return {void} 
 * @todo  posix timer set + create (by using timer_create() and timer_create())
*/
void set_create_peridicTimer(long initial_sec, long initial_usec, long interval_sec, long interval_usec)
{
    timer_t *tidlist;

    //1. sigevent struct 설정 for timer_create() 
    struct sigevent sev;    
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = TIMER_SIG;

    //if(timer_create(CLOCK_REALTIME, &sev, &tidlist[0]) == -1)  perror("timer_create");
    if(timer_create(CLOCK_REALTIME, &sev, &tidlist) == -1)  perror("timer_create");


    //2. itimerspec struct 설정 for timer_settime() 
    struct itimerspec ts;

    //2_1. timer 시간 간격, 초기값 설정
    ///it_value : 첫 만기 시점(첫 번째 TIMER_SIG 발생 시점)
    ts.it_value.tv_sec = initial_sec;              //sec 값
    ts.it_value.tv_nsec = initial_usec;             //nano sec 값

    ///it_interval : 타이머의 반복 주기()
    ts.it_interval.tv_sec = interval_sec;         
    ts.it_interval.tv_nsec = interval_usec;

    //2_2. timer_settime
    //if(timer_settime(tidlist[0], 0, &ts, NULL) == -1)  perror("timer_settime");
    if(timer_settime(tidlist, 0, &ts, NULL) == -1)  perror("timer_settime");

}

/** feature : signal_exit
 * @note alarm handler 종료시 log 출력하도록 alarm_log_thread에 pthread_cond_signal()
*/
void signal_exit(void)
{
    /* 여기에 구현하세요..  종료 메시지를 보내도록.. */
    pthread_mutex_lock(&system_loop_mutex);
    system_loop_exit = true;
    pthread_mutex_unlock(&system_loop_mutex);
    pthread_cond_signal(&system_loop_cond);
    
}


/** feature : disk_report
 *  @note 10초마다 disk 사용량 출력
*/
#define POPEN_FMT "/bin/df -h ./"
#define PCMD_BIF_SIZE 1024
void disk_report(void)
{
    /****************************************************************************************************/
    /******************* popen : "/bin/df -h ./" 실행 - start *******************************************/
    /****************************************************************************************************/
    /** featuer : popen
     * @note popencmd : popen으로 실행할 command 저장 buffer, 
     * @note snprintf() : popencmd에 POPEN_FMT 저장
     * 
     * 
    */
    FILE* apipe;
    char popencmd[PCMD_BIF_SIZE];
    snprintf(popencmd, PCMD_BIF_SIZE, POPEN_FMT);
  
    /* feature : popen으로 shell 실행결과 출력 저장 char ary
     * 
    */
    char popen_result[PATH_MAX];

    int disk_check_cnt = 1;
    int pclose_status;
    while (1) 
    {
        /* popen 사용하여 10초마다 disk 잔여량 출력
         * popen으로 shell을 실행하면 성능과 보안 문제가 있음
         * 향후 파일 관련 시스템 콜 시간에 개선,
         * 하지만 가끔 빠르게 테스트 프로그램 또는 프로토 타입 시스템 작성 시 유용
         */
        sleep(10);
        // posix_sleep_ms(10000);
        printf("\n\n/********** disk 사용량 : %d 번쨰 점검 **********/ \n", disk_check_cnt);
        if(!(apipe = popen(popencmd, "r"))) perror("popen()");

        while(fgets(popen_result, PATH_MAX, apipe))
        {
            printf("%s", popen_result);
        } 
        ++disk_check_cnt;

        /** feature : close file
         * @note  shell 실행한 process(child)에 대한 resource dealloc 수행
         * @note  이미 main.c의 waitpid()에서, shell 실행한 process(child)에 대한 resource dealloc이 이뤄짐 -> error(반환할 status 없음)
        */
        if((pclose_status = pclose(apipe)) == -1)  
        {
            printf("pclose_status : %d\n", pclose_status);
            perror("pclose");
        }
        printf("/********** disk 사용량 점검 끝 **********/ \n\n");
    }
    /****************************************************************************************************/
    /******************* popen : "/bin/df -h ./" 실행 - end *******************************************/
    /****************************************************************************************************/
}




/** feature : get_totalSize_of_dir
 * @note ??? : ./fs의 meta data도 포함?
*/
#define FILEPATH_LEN 1024
const int get_totalSize_of_dir(const char *target_dirname)
{
    struct dirent *entry_ptr;
    struct stat statbuf;
    int stat_retcode;

    int sum = 0;
    int each_fileSize;
    int cnt_files = 0;

    char filePath[FILEPATH_LEN];
    
    // DIR *dir_stream_ptr = opendir(TARGET_DIR);
    DIR *dir_stream_ptr = opendir(target_dirname);

    //while((entry_ptr = readdir(target_dirname)) != NULL)
    while((entry_ptr = readdir(dir_stream_ptr)) != NULL)
    {
        if (strcmp(entry_ptr->d_name, ".") == 0 || strcmp(entry_ptr->d_name, "..") == 0)
        // if(strcmp(entry_ptr->d_name, "..") == 0)     /*  현재 directory(./fs)의 metadata 포함 */
            continue;

        //1. 각 file의 path 구하기
        sprintf(filePath, "%s/%s", target_dirname, entry_ptr->d_name);
        // printf("access to %s\n", filePath);

        //2. 각 file의 size 구하기
        if((stat_retcode = stat(filePath, &statbuf)) == -1) 
        {
            perror("stat");
        }
        assert(stat_retcode != -1);

        each_fileSize = statbuf.st_size;

        //3. 
        // ++cnt_files;

        /** feature : tree ds로 구현(dir)
         * 
        */
        if(S_ISDIR(statbuf.st_mode))
        // if(S_ISDIR(statbuf.st_mode) && (strcmp(entry_ptr->d_name, ".")))
        // if(S_ISDIR(statbuf.st_mode) && (strcmp(entry_ptr->d_name, ".")) != 0)
        {
            long dirSize = get_totalSize_of_dir(filePath) + each_fileSize;   //??
            sum += dirSize;
        }
        else
            sum += each_fileSize;

        //return sum;
        
    }

    // printf("num of file : %d\n", cnt_files);
    return sum;




}