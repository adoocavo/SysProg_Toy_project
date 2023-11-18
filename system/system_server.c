#include <stdio.h>
#include <sys/prctl.h>     //?? 왜 사용??
#include <sys/unistd.h>    //for getpid() syscall
#include <sys/types.h>     //for getpid() syscall
#include <sys/wait.h>      //for waitpid() syscall  

#include "system_server.h"
#include "./../ui/gui.h"
#include "./../ui/input.h"
#include "./../web_server/web_server.h"


/** feature : main process가 생성한 모든 process monitoring 
 * @param {void} 
 * @return {int} 0
 * @todo 
*/
int system_server()
{
    printf("system_server process start!!\n");

    
    while (1) 
    {
        sleep(1);
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
    printf("여기서 system 프로세스를 생성합니다.\n");   
    if((systemPid = fork()) == 0)           
    {
        printf("child process!,  PID : %d\n", getpid());
        
        ////system server process 동작 시작  
        system_server();     

        exit(EXIT_SUCCESS);                    
    }

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




