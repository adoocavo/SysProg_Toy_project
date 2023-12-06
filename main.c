#include <stdio.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <errno.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include "./system/system_server.h"
#include "./ui/gui.h"
#include "./ui/input.h"
#include "./web_server/web_server.h"
#include "./project_libs/time/currTime.h"
#include "./project_libs/toy_message.h"



/**
 * @note num of forked process
*/
#define N 4;       

/**
 * @note num of forked process
*/
static volatile int childProcNum;

/**************** SIGCHLD : non-blocking handler ****************/
static void sigchld_handler(int sig)
{   
    printf("/**************** SIGCHLD handler - start ****************/\n");
    printf("[%s] handler: Caught SIGCHLD(%d)\n", currTime("%T"), sig);

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
    printf("/**************** SIGCHLD handler - end ****************/\n");
}
/**************** SIGCHLD : non-blocking handler ****************/



/***************************************************************************************/
/******************************** message queue괸련 선언  - start ********************************/
/***************************************************************************************/


// /** feature : system_server proc 내에서 생성되는 threads들의 msg queue mqd저장
//  * 
// */
// #define MQ_NUM_MSG 10                //각 posix msg queue에 저장될 수 있는 msg 개수
// #define MQ_MSG_SIZE 2048             //각 msg의 size(default : 8KB)
// #define NUM_OF_MQ 4                  //생성할 message queue file 개수
// #define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) 
/*
static struct mq_attr watchdog_mq_attr;
static struct mq_attr monitor_mq_attr;
static struct mq_attr disk_mq_attr;
static struct mq_attr camera_mq_attr;
*/


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
/******************************** message queue괸련 선언  - end ********************************/
/***************************************************************************************/

/**************** SIGINT : for unlink ****************/
static void sigint_handler(int sig)
{
    // 1. unlink : message queue delete
    for(int i = 0; i <  NUM_OF_MQ; ++i)
    {
        if(mq_unlink(msg_queues_str[i]) == -1) perror("mq_unlink");
    }

    //2.
    signal(SIGINT, SIG_DFL);

    //3. 
    printf("종료하려면 'Ctrl+C'를 누르세요!!\n");
}
/**************** SIGINT : for unlink  ****************/

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
    signal(SIGINT, sigint_handler);
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigchld_handler;
    if(sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    
    printf("메인 함수입니다.\n");


    /****************************************************************************************************/
    /****************************** posix msg queue create (for IPC) - start *******************************/
    /****************************************************************************************************/

    //1. struct mq_attr / flags :  생성할 message queue의 attr / flag 설정, 
    /** feature : struct mq_attr / flags :  생성할 message queue의 attr / flag 설정, 
     * @note flags : ??? (attr.flag와 차이? 둘 중 하나만 설정?)
     * @note mq_attr :
     * @note attr.mq_maxmsg  : 생성되는 posix msg queue에 저장될 수 있는 msg 개수
     * @note attr.mq_msgsize : 생성되는 posix msg queue에 저장되는 msg의 size(default : 8KB)
     * @note attr.flag : ???(mq_open의 param인 flags와 차이? 둘 중 하나만 설정?)
     * @note  mode_t perms : 생성되는 msg queue file의 접근 권한 설정
    */
    mode_t perms = FILE_MODE;
    //int flags = O_RDWR | O_CREAT | O_EXCL;
    int flags = O_RDWR | O_CREAT;

    struct mq_attr attr;                        
    attr.mq_maxmsg = MQ_NUM_MSG;
    // attr.mq_msgsize = sizeof(toy_msg_t);
    //attr.mq_msgsize = sizeof(MQ_MSG_SIZE);
    attr.mq_msgsize = MQ_MSG_SIZE;

    //2. open : message queue create
    for(int i = 0; i <  NUM_OF_MQ; ++i)
    {
        // if(mqds[i] = mq_open(msg_queues_str[i], flags, perms, &attr) == -1) perror("mq_open");
        mqds[i] = mq_open(msg_queues_str[i], flags, perms, &attr);
        assert(mqds[i] != -1);

    }

    //if((prompt_perm_mqd = mq_open(prompt_perm_filename, flags, perms, &attr)) == -1) perror("mq_open");

    /****************************************************************************************************/
    /****************************** posix msg queue create (for IPC) - end *******************************/
    /****************************************************************************************************/

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
