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

#include "./../hal/camera_HAL.h"
#include "system_server.h"
#include "./../ui/gui.h"
#include "./../ui/input.h"
#include "./../web_server/web_server.h"
#include "./../project_libs/time/currTime.h"

#define TIMER_SIG SIGRTMAX      //POSIX RTS 사용
#define NUM_OF_THREADS 5

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

/** 
 * 
*/
void signal_exit(void);

/**
 * 
*/
pthread_mutex_t system_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  system_loop_cond  = PTHREAD_COND_INITIALIZER;
bool            system_loop_exit = false;    ///< true if main loop should exit

/** feature : TIMER_SIG handler 
 * @param {void} 
 * @return {int} 0
 * @todo : tick값() 출력 
*/
static void timerSig_handler(int sig, siginfo_t *si, void *uc)
{
    
    /** 
     * @note signal handler내에서는 lock 걸면 안된다 
     *  => global var(critical sec 생성하는) 관련 r/w thread를 따로 생성해서, 해당 thread에서 수행하도록!
     *  => signal handler는 최대한 짧게 작성
    */
    ++toy_timer;

    timer_t *tidptr;

    tidptr = si->si_value.sival_ptr;

    printf("[%s] Got signal %d : %d번째 alarm \n", currTime("%T"), sig, toy_timer);

    signal_exit();
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
    "alarm_log_thread_func"
};

/** feature : main process가 생성한 모든 process monitoring 
 * @param {void} 
 * @return {int} 0
 * @todo 
*/
int system_server()
{
    struct sigaction  sa;
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
    set_create_peridicTimer(10, 0, 10, 0);
    /****************************************************************************************************/
    /******************* 5초 타이머 생성 + TIMER_SIG handler 등록 *******************************************/
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
        if(pthread_create(&tids[i], &attrs[i], thread_funcs[i], (void *)threads_name[i])) perror("pthread_create");
    }
    /****************************************************************************************************/
    /******************* watchdog, monitor, disk_serviced, camera_service threads 생성 *******************/
    /****************************************************************************************************/

    printf("system init done.  waiting...");
/*
    // 여기에 구현하세요... 여기서 cond wait로 대기한다. 10초 후 알람이 울리면 <== system 출력
    while(1)
    {
        pthread_mutex_lock(&system_loop_mutex);
        if(system_loop_exit == false)
        {
            pthread_cond_wait(&system_loop_cond, &system_loop_mutex);
        }
        printf(" <== system\n");
        system_loop_exit = false;
        pthread_mutex_unlock(&system_loop_mutex);
    }
*/    
    /*
    // 1초 마다 wake-up 
    while (system_loop_exit == false) 
    {
        sleep(1);
    }
    */
    while(1) 
    {
        //pthread_mutex_lock(&system_loop_mutex);
        printf("main thread : busy waiting\n");
        //pthread_mutex_lock(&system_loop_mutex);

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
        printf("child(system_server) process!,  PID : %d\n", getpid());
        
        
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

        printf("parent process!,  PID : %d\n", getpid());
        
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

    while(1)
    {
//        posix_sleep_ms(5000);
        sleep(5);
    }

    return NULL;
}

/** feature : monitor_thread_func definition 
 * 
*/
void * monitor_thread_func(void *arg)
{  
    char *str = arg;
    printf("나 %s\n", str);

    while(1)
    {
//        posix_sleep_ms(5000);
        sleep(5);
    }

    return NULL;
}

/** feature : disk_service_thread_func definition 
 * 
*/
void * disk_service_thread_func(void *arg)
{  
    char *str = arg;
    printf("나 %s\n", str);

    while(1)
    {
//        posix_sleep_ms(5000);
        sleep(5);
    }

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
    toy_camera_take_picture();


    while(1)
    {
//        posix_sleep_ms(5000);
        sleep(5);
    }

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
        printf(" <== system\n");
        system_loop_exit = false;
        pthread_mutex_unlock(&system_loop_mutex);
    }

    return NULL;
}

/** feature : camera_service_thread_func definition 
 * 
*/
void signal_exit(void)
{
    /* 여기에 구현하세요..  종료 메시지를 보내도록.. */
    pthread_mutex_lock(&system_loop_mutex);
    system_loop_exit = true;
    pthread_mutex_unlock(&system_loop_mutex);
    pthread_cond_signal(&system_loop_cond);
    
}
