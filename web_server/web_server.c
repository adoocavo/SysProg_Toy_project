#include <stdio.h>
#include <sys/prctl.h>     //?? 왜 사용??
#include <sys/unistd.h>    //for getpid() syscall
#include <sys/types.h>     //for getpid() syscall
#include <sys/wait.h>      //for waitpid() syscall  

#include "./../system/system_server.h"
#include "./../ui/gui.h"
#include "./../ui/input.h"
#include "web_server.h"



/** feature : filebrowser 실행
 * @param {void} 
 * @return {int} 0
 * @todo  execl() return value 처리??
*/
int web_server()
{
    printf("web_server process start!!\n");

    //execl() syscall -> filebrowser 실행
    printf("filebrowser start!!\n");
    if(execl("/usr/local/bin/filebrowser", "filebrowser", "-p", "8282", (char *)NULL))
    {
        perror("execl");
        //printf("execfailed\n");
    }

    while(1)
    {
        sleep(1);            
    }
    
    return 0; 

}

/** feature : Web server process 생성 
 * @param {void} 
 * @return {int} 0
 * @todo 해당 함수 내부에서 parent process의 흐름이 멈추지 않고, child process 종료를 확인하도롣 구현 (by sig handler)
*/
int create_web_server()
{
    pid_t webPid;

    /**
     * @note fork -> child : Web server process 수행 
     *              parent : Web server process 생성 후 wait
    */ 
    // child process
    printf("여기서 Web Server 프로세스를 생성합니다.\n");
    
    //sleep(3);

    if((webPid = fork()) == 0)           
    {
        // printf("child(Web Server) process!,  PID : %d\n", getpid());
        
        ////Web server process 동작 시작  
        web_server();  

        //exit(EXIT_SUCCESS);         
    }

    else if(webPid == -1) perror("fork");

    // parent process   
    else                                    
    {
        int childStatus;                   /** local var : Web server process의 terminating 상태 저장*/
        int child_wPid;                    /** local var : Web server 기다린 이후, return되는 child Pid 저장*/

        // printf("parent process!,  PID : %d\n", getpid());
        
        sleep(3);

        // 1. caller에서 새로 생성한 자식 process wait 처리
        return webPid;

        /* 2. callee에서  새로 생성한 자식 process wait 처리 -> sighandler 등록 필요??
        //1. Web server process 정상 종료 확인 전까지 wait
        child_wPid = waitpid(webPid, &childStatus, 0);
       
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
