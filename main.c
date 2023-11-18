#include <stdio.h>
#include <sys/wait.h>

#include "./system/system_server.h"
#include "./ui/gui.h"
#include "./ui/input.h"
#include "./web_server/web_server.h"

int main()
{
	pid_t spid, gpid, ipid, wpid;
    int status, savedErrno;

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

    //5. wait() : fork()로 생성한 process의 정상 종료 확인
    waitpid(spid, &status, 0);
    waitpid(gpid, &status, 0);
    waitpid(ipid, &status, 0);
    waitpid(wpid, &status, 0);

    return 0;
}
