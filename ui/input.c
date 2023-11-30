#include <stdio.h>
#include <sys/prctl.h>     //?? 왜 사용??
#include <sys/unistd.h>    //for getpid() syscall
#include <sys/types.h>     //for getpid() syscall
#include <sys/wait.h>      //for waitpid() syscall  
#include <execinfo.h>
#include <stdlib.h>
#include <string.h>
#include <execinfo.h>
#include <signal.h>
#include <errno.h>

#include "./../system/system_server.h"
#include "gui.h"
#include "input.h"
#include "./../web_server/web_server.h"


/******************************** SIGSEGV : handler ********************************/

typedef struct _sig_ucontext
 {
    unsigned long uc_flags;
    struct ucontext *uc_link;
    stack_t uc_stack;
    struct sigcontext uc_mcontext;
    sigset_t uc_sigmask;
} sig_ucontext_t;

void segfault_handler(int sig_num, siginfo_t * info, void * ucontext) 
{
  void * array[50];
  void * caller_address;
  char ** messages;
  int size, i;
  sig_ucontext_t * uc;

  uc = (sig_ucontext_t *) ucontext;

  /* Get the address at the time the signal was raised */
  //caller_address = (void *) uc->uc_mcontext.rip;  // RIP: x86_64 specific     arm_pc: ARM
  caller_address = (void *) uc->uc_mcontext.pc;  // RIP: x86_64 specific     arm_pc: ARM

  fprintf(stderr, "\n");

  if (sig_num == SIGSEGV)
    printf("signal %d (%s), address is %p from %p\n", sig_num, strsignal(sig_num), info->si_addr,
           (void *) caller_address);
  else
    printf("signal %d (%s)\n", sig_num, strsignal(sig_num));

  size = backtrace(array, 50);
  /* overwrite sigaction with caller's address */
  array[1] = caller_address;
  messages = backtrace_symbols(array, size);

  /* skip first stack frame (points here) */
  for (i = 1; i < size && messages != NULL; ++i) 
  {
    printf("[bt]: (%d) %s\n", i, messages[i]);
  }

  free(messages);

  exit(EXIT_FAILURE);
}

/******************************** SIGSEGV : handler ********************************/




int input_server()
{
    printf("나 input 프로세스!\n");
    struct sigaction sa;
   
    memset(&sa, 0, sizeof(sigaction));
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = segfault_handler;

    /**
    * @note installing signal hanlder
    */
    if(sigaction(SIGSEGV, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    
    while (1) 
    {
        sleep(1);
    }
    
    return 0;
}

/** feature : input server process 생성 
 * @param {void} 
 * @return {int} 0
 * @todo 해당 함수 내부에서 parent process의 흐름이 멈추지 않고, child process 종료를 확인하도롣 구현 (by sig handler)
*/
int create_input()
{
    /**
     * @note installing signal hanlder
     * @note fork() -> signal handler 등록 정보도 공유하게 됨(child-parent간)
    */
    /* => child에 등록
    struct sigaction sa;

    memset(&sa, 0, sizeof(sigaction));
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = segfault_handler;

    if(sigaction(SIGSEGV, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    */
    /* => sigaction()사용
    if(signal(SIGSEGV, segfault_handler) == SIG_ERR)
    {
        perror("signal");
        exit(EXIT_FAILURE);
    }
    */
    pid_t inputPid;                        //local var : input server process's PID
    const char *name = "input_server";     
  
    /**
     * @note fork -> child : input server process 수행 
     *              parent : input server process 생성 후 wait
    */ 
    // child process
    printf("여기서 input 프로세스를 생성합니다.\n");   
    if((inputPid = fork()) == 0)           
    {
        printf("child(input) process!,  PID : %d\n", getpid());
        
        /* 프로세스 이름 변경 */
        if (prctl(PR_SET_NAME, (unsigned long) name) < 0) perror("prctl()");
        
        //input server process 동작 시작  
        input_server();  

        //exit(EXIT_SUCCESS);                        
    }

    else if(inputPid == -1) perror("fork");

    // parent process   
    else                                    
    {
        int childStatus;                   /** local var : input server process의 terminating 상태 저장*/
        int child_wPid;                    /** local var : input server 기다린 이후, return되는 child Pid 저장*/

        printf("parent process!,  PID : %d\n", getpid());
        
        sleep(3);

        // 1. caller에서 새로 생성한 자식 process wait 처리
        return inputPid;

        /* 2. callee에서  새로 생성한 자식 process wait 처리 -> sighandler 등록 필요??
        //1. input server process 정상 종료 확인 전까지 wait
        child_wPid = waitpid(inputPid, &childStatus, 0);
       
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
