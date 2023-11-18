#include <stdio.h>
#include <sys/prctl.h>     //?? 왜 사용??
#include <sys/unistd.h>    //for getpid() syscall
#include <sys/types.h>     //for getpid() syscall
#include <sys/wait.h>      //for waitpid() syscall  

#include "./../system/system_server.h"
#include "gui.h"
#include "input.h"
#include "./../web_server/web_server.h"




/** feature : google-chrome-stable 실행
 * @param {void} 
 * @return {int} 0
 * @todo  execl() return value 처리??
*/
int gui_server()
{
    printf("gui_server process start!!\n");

    //execl() syscall -> google-chrome-stable 실행
    printf("chromium-browser start!!\n");
    //execl("/usr/bin/google-chrome-stable", "google-chrome-stable", "http://localhost:8282", NULL);    
    execl("/usr/bin/chromium-browser", "chromium-browser", "http://localhost:8282", NULL);    
    return 0; 
}


/** feature : gui server process 생성 
 * @param {void} 
 * @return {int} 0
 * @todo 해당 함수 내부에서 parent process의 흐름이 멈추지 않고, child process 종료를 확인하도롣 구현 (by sig handler)
*/
int create_gui()
{
    pid_t guiPid;

    /**
     * @note fork -> child : Gui server process 수행 
     *              parent : Gui server process 생성 후 wait
    */ 
    // child process
    printf("여기서 Gui Server 프로세스를 생성합니다.\n");
    //sleep(3);
    
    if((guiPid = fork()) == 0)           
    {
        printf("child process!,  PID : %d\n", getpid());
        
        ////Gui server process 동작 시작  
        gui_server();  

        exit(EXIT_SUCCESS);                        
    }

    // parent process   
    else                                    
    {
        int childStatus;                   /** local var : Gui server process의 terminating 상태 저장*/
        int child_wPid;                    /** local var : Gui server 기다린 이후, return되는 child Pid 저장*/

        printf("parent process!,  PID : %d\n", getpid());
        
        // 1. caller에서 새로 생성한 자식 process wait 처리
        return guiPid;

        /* 2. callee에서  새로 생성한 자식 process wait 처리 -> sighandler 등록 필요??
        //1. Gui server process 정상 종료 확인 전까지 wait
        child_wPid = waitpid(guiPid, &childStatus, 0);
       
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
