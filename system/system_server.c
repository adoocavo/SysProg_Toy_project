#include <stdio.h>
#include <sys/prctl.h>     //?? 왜 사용??
#include <sys/unistd.h>    //for getpid() syscall
#include <sys/types.h>     //for getpid() syscall
#include <sys/wait.h>      //for waitpid() syscall  
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
//#include <bits/sigevent-consts.h>    //for SIGEV_SIGNAL
//#include <bits/sigaction.h>

#include "system_server.h"
#include "./../ui/gui.h"
#include "./../ui/input.h"
#include "./../web_server/web_server.h"
#include "./../project_libs/time/currTime.h"

#define TIMER_SIG SIGRTMAX      //POSIX RTS 사용


/** feature : main process가 생성한 모든 process monitoring 
 * @param {long} initial_sec, initial_usec, interval_sec, interval_usec
 * @return {void} 
 * @todo  posix timer set + create (by using timer_create() and timer_create())
*/
void set_create_peridicTimer(long initial_sec, long initial_usec, long interval_sec, long interval_usec)
{
    timer_t *tidlist;

    //1. sigevent struct 설정 for timer_create() + timer_create()
    struct sigevent   sev;    
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = TIMER_SIG;

    if(timer_create(CLOCK_REALTIME, &sev, &tidlist[0]) == -1)  perror("timer_create");


    //2. itimerspec struct 설정 for timer_settime() + timer_settime()
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
//static int toy_timer = 0;  
/*
int posix_sleep_ms(unsigned int timeout_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / MILLISEC_PER_SECOND;
    sleep_time.tv_nsec = (timeout_ms % MILLISEC_PER_SECOND) * (NANOSEC_PER_USEC * USEC_PER_MILLISEC);

    return nanosleep(&sleep_time, NULL);
}
*/

/** feature : TIMER_SIG handler 
 * @param {void} 
 * @return {int} 0
 * @todo : tick값() 출력 
*/
static void timerSig_handler(int sig, siginfo_t *si, void *uc)
{

    timer_t *tidptr;

    tidptr = si->si_value.sival_ptr;

    /* UNSAFE: This handler uses non-async-signal-safe functions
       (printf(); see Section 21.1.2) */

    printf("[%s] Got signal %d\n", currTime("%T"), sig);
    printf("    *sival_ptr         = %ld\n", (long) *tidptr);
    printf("    timer_getoverrun() = %d\n", timer_getoverrun(*tidptr));

}



/** feature : main process가 생성한 모든 process monitoring 
 * @param {void} 
 * @return {int} 0
 * @todo 
*/
int system_server()
{
//    struct itimerspec ts;
    struct sigaction  sa;
//    struct sigevent   sev;
    timer_t *tidlist;

    printf("나 system_server 프로세스!\n");
    
    /* 5초 타이머를 만들어 봅시다. */

    //1. sig hanlder 등록(TIMER_SIG)
    memset(&sa, 0, sizeof(sigaction));

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = timerSig_handler;

    if(sigaction(TIMER_SIG, &sa, NULL) == -1)   perror("sigaction");

    //2. set + create timer
    set_create_peridicTimer(5, 0, 5, 0);

    printf("system init done.  waiting...");

    while (1) 
    {
        sleep(5);
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

        //exit(EXIT_SUCCESS);                    
    }

    else if(systemPid == -1) perror("fork");
    
    // parent process   
    else                                    
    {
        int childStatus;                   /** local var : system server process의 terminating 상태 저장*/
        int child_wPid;                    /** local var : system server 기다린 이후, return되는 child Pid 저장*/

        printf("parent process!,  PID : %d\n", getpid());
        
        //1. caller에서 새로 생성한 자식 process wait 처리
        return systemPid;

        /*2. callee에서  새로 생성한 자식 process wait 처리 -> sighandler 등록 필요??
        //1. system server process 정상 종료 확인 전까지 wait
        child_wPid = waitpid(systemPid, &childStatus, 0);
       
        //1-1. waitpid return 확인
        if(child_wPid == -1)     
        {
            perror("error in waitpid()\n");
        }

        else    
        {
           //1-2. child terminated status 확인
           if(WIFEXITED(childStatus)) 
           {
                printf("child proces(PID : %d) terminated with exit status %d\n", child_wPid, WEXITSTATUS(childStatus));
           }

           else 
           {
                printf("child proces(PID : %d) terminated abnormally\n", child_wPid);
           }
        }
        */

    }

    return 0;
}




