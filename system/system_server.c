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
#include <sys/stat.h>
#include <fcntl.h>


#include "./../hal/camera_HAL.h"
#include "system_server.h"
#include "./../ui/gui.h"
#include "./../ui/input.h"
#include "./../web_server/web_server.h"
#include "./../project_libs/time/currTime.h"
#include "./../project_libs/toy_message.h"

#define TIMER_SIG SIGRTMAX      //POSIX RTS 사용
#define NUM_OF_THREADS 5
#define NUM_OF_MQ 4             //open할 message queue file 개수
/** feature : timer set + create 
 * @param {long} initial_sec, initial_usec, interval_sec, interval_usec
 * @return {void} 
 * @todo  posix timer set + create (by using timer_create() and timer_create())
*/
void set_create_peridicTimer(long initial_sec, long initial_usec, long interval_sec, long interval_usec)
{
    timer_t *tidlist;

    //1. sigevent struct 설정 for timer_create() 
    struct sigevent   sev;    
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = TIMER_SIG;

    if(timer_create(CLOCK_REALTIME, &sev, &tidlist[0]) == -1)  perror("timer_create");


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
    if(timer_settime(tidlist[0], 0, &ts, NULL) == -1)  perror("timer_settime");

}


/***************************************************************************************/
/******************************** message queue괸련 선언  - start ********************************/
/***************************************************************************************/

/** feature : system_server proc 내에서 생성되는 threads들의 msg queue mqd저장
 * @note idx 0~4 순서대로 watchdog_mqd, monitor_mqd, disk_mqd, camera_mqd
*/
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


/**
 * @note Set nonzero on receipt of SIGALRM : SIGALRM -> toy_timer = 1;
*/
static int toy_timer = 0;  
/*
int posix_sleep_ms(unsigned int timeout_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / MILLISEC_PER_SECOND;
    sleep_time.tv_nsec = (timeout_ms % MILLISEC_PER_SECOND) * (NANOSEC_PER_USEC * USEC_PER_MILLISEC);

    return nanosleep(&sleep_time, NULL);
}
*/

/** feature : signal_exit
 * @note alarm handler 종료시 log 출력하도록 alarm_log_thread에 pthread_cond_signal()
*/
void signal_exit(void);

/** feature : alarm_log_thread 수행관련 mutex 변수
 *  
*/
pthread_mutex_t system_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  system_loop_cond  = PTHREAD_COND_INITIALIZER;
bool            system_loop_exit = false;                     ///< true if main loop should exit

/** feature : TIMER_SIG handler 
 * @param {void} 
 * @return {int} 0
 * @todo : tick값() 출력 
*/
static void timerSig_handler(int sig, siginfo_t *si, void *uc)
{

    printf("\n/**************** TIMER_SIG handler - start ****************/\n");
    /** 
     * @note signal handler내에서는 lock 걸면 안된다 
     *  => global var(critical sec 생성하는) 관련 r/w thread를 따로 생성해서, 해당 thread에서 수행하도록!
     *  => signal handler는 최대한 짧게 작성
    */
    ++toy_timer;

    timer_t *tidptr;

    tidptr = si->si_value.sival_ptr;

    printf("\n[%s] Got signal %d : %d번째 alarm \n", currTime("%T"), sig, toy_timer);

    signal_exit();
    printf("/**************** TIMER_SIG handler - end ****************/\n\n");
}


/** feature : thread_func  
 * 
*/
void * watchdog_thread_func(void *);
void * monitor_thread_func(void *);
void * disk_service_thread_func(void *);
void * camera_service_thread_func(void *);
void * alarm_log_thread_func(void *);


/** thread_func array 
 * 
*/
void * (*thread_funcs[NUM_OF_THREADS]) (void *) = {
    watchdog_thread_func,
    monitor_thread_func,
    disk_service_thread_func,
    camera_service_thread_func,
    alarm_log_thread_func
};

/** threads_name ary
 * 
*/
char* threads_name[NUM_OF_THREADS] = {
    "watchdog_thread",
    "monitor_thread",
    "disk_service_thread",
    "camera_service_thread",
    "alarm_log_thread"
};

/** feature : main process가 생성한 모든 process monitoring 
 * @param {void} 
 * @return {int} 0
 * @todo 
*/
int system_server()
{
    struct sigaction sa;
    timer_t *tidlist;

    printf("나 system_server 프로세스!\n");
    
    /****************************************************************************************************/
    /******************* 5초 타이머 생성 + TIMER_SIG handler 등록 *******************************************/
    /****************************************************************************************************/

    //1. sig hanlder 등록(TIMER_SIG)
    memset(&sa, 0, sizeof(sigaction));

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = timerSig_handler;

    if(sigaction(TIMER_SIG, &sa, NULL) == -1)   perror("sigaction");

    //2. set + create timer
    set_create_peridicTimer(60, 0, 10, 0);
    /****************************************************************************************************/
    /******************* 5초 타이머 생성 + TIMER_SIG handler 등록 *******************************************/
    /****************************************************************************************************/
    

    /****************************************************************************************************/
    /************************************* message queue open - start *******************************************/
    /****************************************************************************************************/
    for(int i = 0; i < NUM_OF_MQ; ++i)
    {
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


    while(1) 
    {
        //printf("main thread : busy waiting\n");

        sleep(10);
        //posix_sleep_ms(5000);
    }
    

    /****************************************************************************************************/
    /****************************** posix msg queue unlink (for IPC) - start *******************************/
    /****************************************************************************************************/

    //3. unlink : message queue delete
    
    for(int i = 0; i <  NUM_OF_MQ; ++i)
    {
        if(mq_unlink(msg_queues_str[i]) == -1) perror("mq_unlink");
    }

    /****************************************************************************************************/
    /****************************** posix msg queue unlink (for IPC) - end *******************************/
    /****************************************************************************************************/

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
     * @note char *received_msg_buffer : mq_receive()로 받은 data를 저장할 buffer
    */
    //0. open mq file in parent process

    //1. receive 하기위한 setting : mq_getattr() -> received_msg_buffer 동적할당
    struct mq_attr attr;
    unsigned int prio_of_msg;
    char *received_msg_buffer;
    unsigned int current_msg_num;
    struct timespec set_timeout; 

    if(mq_getattr(mqds[0], &attr) == -1) perror("mq_getattr(watchdog_thread_func)");
    received_msg_buffer = malloc(sizeof(char) * attr.mq_msgsize);
    set_timeout.tv_sec = 500;
    set_timeout.tv_nsec = 0;

    //current_msg_num = attr.mq_curmsgs;

    //2. receive
    ssize_t numRead;           //몇 byte message 수신했는지 저징

    //while(attr.mq_curmsgs > 0)
    while(1)
    {
        //2_1. receive
        numRead = mq_receive(mqds[0], received_msg_buffer, attr.mq_msgsize, &prio_of_msg);
        //numRead = mq_timedreceive(mqds[0], received_msg_buffer, attr.mq_msgsize, &prio_of_msg, &set_timeout);
        assert(numRead != -1);

        //2_2. 받은 message 출력
        printf("(%s) Read %ld bytes; priority : %u\n\n", msg_queues_str[0], numRead, prio_of_msg);

        printf("msg_type : %d\n", ((toy_msg_t*)received_msg_buffer)->msg_type);
        printf("param1 : %d\n", ((toy_msg_t*)received_msg_buffer)->param1);
        printf("param2 : %d\n", ((toy_msg_t*)received_msg_buffer)->param2);
        //printf("param3 : %s\n", (char*)((toy_msg_t*)received_msg_buffer)->param3);

        printf("\n");
    }
/*
    printf("timeout : %s!!!\n", str);
    while(1)
    {
        sleep(5);
    }
*/

    return NULL;
}

/** feature : monitor_thread_func definition 
 * 
*/
void * monitor_thread_func(void *arg)
{  
    char *str = arg;
    printf("나 %s\n", str);

    /** feature : receive data form "/monitor_mq" (message queue를 사용한 data IPC)
     * @note mq_getattr().mq_msgsize : receive 하여 저장할 buffer size 지정 위해 사용
     * @note unsigned int prio : receive 받은 message의 우선 순위 저장
     * @note ssize_t numRead : 몇 byte message 수신했는지 저장
     * @note char *received_msg_buffer : mq_receive()로 받은 data를 저장할 buffer
    */
    //0. open mq file in parent process

    //1. receive 하기위한 setting : mq_getattr() -> received_msg_buffer 동적할당
    struct mq_attr attr;
    unsigned int prio_of_msg;
    char *received_msg_buffer;
    unsigned int current_msg_num;
    struct timespec set_timeout; 

    if(mq_getattr(mqds[1], &attr) == -1) perror("mq_getattr(monitor_thread_func)");
    received_msg_buffer = malloc(sizeof(char) * attr.mq_msgsize);
    set_timeout.tv_sec = 500;
    set_timeout.tv_nsec = 0;

    //current_msg_num = attr.mq_curmsgs;

    //2. receive
    ssize_t numRead;           //몇 byte message 수신했는지 저징

    //while(attr.mq_curmsgs > 0)
    while(1)
    {
        //2_1. receive
        numRead = mq_receive(mqds[1], received_msg_buffer, attr.mq_msgsize, &prio_of_msg);
        //numRead = mq_timedreceive(mqds[1], received_msg_buffer, attr.mq_msgsize, &prio_of_msg, &set_timeout);
        assert(numRead != -1);

        //2_2. 받은 message 출력
        printf("(%s) Read %ld bytes; priority : %u\n\n", msg_queues_str[1], numRead, prio_of_msg);

        printf("msg_type : %d\n", ((toy_msg_t*)received_msg_buffer)->msg_type);
        printf("param1 : %d\n", ((toy_msg_t*)received_msg_buffer)->param1);
        printf("param2 : %d\n", ((toy_msg_t*)received_msg_buffer)->param2);
        //printf("param3 : %s\n", (char*)((toy_msg_t*)received_msg_buffer)->param3);

        printf("\n");
    }
/*
    printf("timeout : %s!!!\n", str);
    while(1)
    {
        sleep(5);
    }
*/

    return NULL;
}

/** feature : disk_service_thread_func definition 
 * @note  10초마다 disk 사용량 출력
*/
#define POPEN_FMT "/bin/df -h ./"
#define PCMD_BIF_SIZE 1024
void * disk_service_thread_func(void *arg)
{  
    char *str = arg;
    printf("나 %s\n", str);

    /** feature : receive data form "/disk_mq" (message queue를 사용한 data IPC)
     * @note mq_getattr().mq_msgsize : receive 하여 저장할 buffer size 지정 위해 사용
     * @note unsigned int prio : receive 받은 message의 우선 순위 저장
     * @note ssize_t numRead : 몇 byte message 수신했는지 저장
     * @note char *received_msg_buffer : mq_receive()로 받은 data를 저장할 buffer
    */
    //0. open mq file in parent process

    //1. receive 하기위한 setting : mq_getattr() -> received_msg_buffer 동적할당
    struct mq_attr attr;
    unsigned int prio_of_msg;
    char *received_msg_buffer;
    unsigned int current_msg_num;
    struct timespec set_timeout; 

    if(mq_getattr(mqds[2], &attr) == -1) perror("mq_getattr(camera_service_thread_func)");
    received_msg_buffer = malloc(sizeof(char) * attr.mq_msgsize);
    set_timeout.tv_sec = 500;
    set_timeout.tv_nsec = 0;

    //current_msg_num = attr.mq_curmsgs;

    //2. receive
    ssize_t numRead;           //몇 byte message 수신했는지 저징

    //while(attr.mq_curmsgs > 0)
    while(1)
    {
        //2_1. receive
        numRead = mq_receive(mqds[2], received_msg_buffer, attr.mq_msgsize, &prio_of_msg);
        // numRead = mq_timedreceive(mqds[2], received_msg_buffer, attr.mq_msgsize, &prio_of_msg, &set_timeout);
        assert(numRead != -1);

        //2_2. 받은 message 출력
        printf("(%s) Read %ld bytes; priority : %u\n\n", msg_queues_str[2], numRead, prio_of_msg);

        printf("msg_type : %d\n", ((toy_msg_t*)received_msg_buffer)->msg_type);
        printf("param1 : %d\n", ((toy_msg_t*)received_msg_buffer)->param1);
        printf("param2 : %d\n", ((toy_msg_t*)received_msg_buffer)->param2);
        //printf("param3 : %s\n", (char*)((toy_msg_t*)received_msg_buffer)->param3);

        printf("\n");
    }
/*
    printf("timeout : %s!!!\n", str);
    while(1)
    {
        sleep(5);
    }
*/

    return NULL;
}

/** feature : camera_service_thread_func definition 
 * 
*/
void * camera_service_thread_func(void *arg)
{  
    char *str = arg;
    printf("나 %s\n", str);

    toy_camera_open();

    /** feature : receive data form "/camera_mq" (message queue를 사용한 data IPC)
     * @note mq_getattr().mq_msgsize : receive 하여 저장할 buffer size 지정 위해 사용
     * @note unsigned int prio : receive 받은 message의 우선 순위 저장
     * @note ssize_t numRead : 몇 byte message 수신했는지 저장
     * @note char *received_msg_buffer : mq_receive()로 받은 data를 저장할 buffer
    */
    //0. open mq file in parent process

    //1. receive 하기위한 setting : mq_getattr() -> received_msg_buffer 동적할당
    struct mq_attr attr;
    unsigned int prio_of_msg;
    char *received_msg_buffer;
    unsigned int current_msg_num;
    struct timespec set_timeout; 


    if(mq_getattr(mqds[3], &attr) == -1) perror("mq_getattr(camera_service_thread_func)");
    received_msg_buffer = malloc(sizeof(char) * attr.mq_msgsize);
    set_timeout.tv_sec = 500;
    set_timeout.tv_nsec = 0;
    //current_msg_num = attr.mq_curmsgs;

    //2. receive
    ssize_t numRead;           //몇 byte message 수신했는지 저징

    //while(attr.mq_curmsgs > 0)
    while(1)
    {
        //2_1. receive
        numRead = mq_receive(mqds[3], received_msg_buffer, attr.mq_msgsize, prio_of_msg);
        // numRead = mq_timedreceive(mqds[3], received_msg_buffer, attr.mq_msgsize, &prio_of_msg, &set_timeout);
        assert(numRead != -1);

        //2_2. 받은 message 출력
        printf("(%s) Read %ld bytes; priority : %u\n\n", msg_queues_str[3], numRead, prio_of_msg);

        printf("msg_type : %d\n", ((toy_msg_t*)received_msg_buffer)->msg_type);
        printf("param1 : %d\n", ((toy_msg_t*)received_msg_buffer)->param1);
        printf("param2 : %d\n", ((toy_msg_t*)received_msg_buffer)->param2);
        //printf("param3 : %s\n", (char*)((toy_msg_t*)received_msg_buffer)->param3);

        if(((toy_msg_t*)received_msg_buffer)->msg_type == 1) toy_camera_take_picture();

        printf("\n");

        //if(write(STDOUT_FILENO, received_msg_buffer, numRead) == -1) perror("write(camera_service_thread_func)");
        //write(STDOUT_FILENO, "\n", 1);
    }
/*
    printf("timeout : %s!!!\n", str);
    while(1)
    {
        sleep(5);
    }
*/
    return NULL;
}


/** feature : alarm_log_thread_func definition 
 * @note alarm 올때마다 Log 찍는 기능 따로 구현
*/
void * alarm_log_thread_func(void *arg)
{  
    char *str = arg;
    printf("나 %s\n", str);

    while(1)
    {
        pthread_mutex_lock(&system_loop_mutex);

        if(system_loop_exit == false)
        {
            pthread_cond_wait(&system_loop_cond, &system_loop_mutex);
        }
        //printf(" <== system\n");
        system_loop_exit = false;
        pthread_mutex_unlock(&system_loop_mutex);
    }

    return NULL;
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