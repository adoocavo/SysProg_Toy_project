#include <stdio.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <errno.h>
#include <pthread.h>

#include "./system/system_server.h"
#include "./ui/gui.h"
#include "./ui/input.h"
#include "./web_server/web_server.h"
#include "./project_libs/time/currTime.h"

#define TOY_BUFFSIZE 1024


/**
 * @note num of forked process
*/
#define N 4;       


/** global var : for terminal_operation_check in input.c
 *  @note  0 : terminal에서 입력 가능, 1 : terminal에서 입력 불가
*/
//extern int terminal_operation_check;
//extern pthread_mutex_t terminal_operation_mutex;
//extern pthread_cond_t terminal_operation_cond;


/**
 * @note num of forked process
*/
static volatile int childProcNum;

/**************** SIGCHLD : non-blocking handler ****************/
static void sigchld_handler(int sig)
{
    printf("%s handler: Caught SIGCHLD\n", currTime("%T"));

    int status, savedErrno;
    pid_t id;

    savedErrno = errno;


    /**
     * @note waitpid : child status 뱐화 감지 -> resource 반환
    */
    while((id = waitpid(-1, &status, WNOHANG)) > 0) 
    {
        printf("%s handler: Reaped child %ld - \n", currTime("%T"), (long) id);
        --childProcNum;
    }

    /**
     * @note waitpid()함수 실행 error 확인 -> waitpid error msg 출력
    */
    if(id == -1 && errno != ECHILD) perror("waitpid");

    printf("%s handler: returning\n", currTime("%T"));

    errno = savedErrno;


}
/**************** SIGCHLD : non-blocking handler ****************/



int main()
{
	pid_t spid, gpid, ipid, wpid;
    childProcNum = N;
    int sigCnt;
    int chld_status;

    /**
     * @note sa, blockMask, emptyMask : signal handler 속성 변경 변수
    */
    sigset_t blockMask, emptyMask;
    struct sigaction sa;

    /**
     * @note installing signal hanlder
    */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigchld_handler;
    if(sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    
    printf("메인 함수입니다.\n");

    //1. fork() : system server process 
    printf("\n시스템 서버를 생성합니다.\n");
    spid = create_system_server();
    
    //2. fork() : web server process 
    printf("\n웹 서버를 생성합니다.\n");
    wpid = create_web_server();
    
    //3. fork() : input process 
    printf("\n입력 프로세스를 생성합니다.\n");
    ipid = create_input();
 
    //4. fork() : gui process 
    printf("\nGUI를 생성합니다.\n");
    gpid = create_gui();


    //sleep(5);            // => 생성한 모든 proc의 시작 출력이 끝난 후로 변경해야함 
    //kill(ipid, SIGCONT);

    /**
     * @note wait for SIGCHLD until all children are dead 
    */
    while(childProcNum > 0)
    {
        sleep(1);
    }

    return 0;
}
