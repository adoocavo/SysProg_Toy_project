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
#include <assert.h>
#include <pthread.h>

#include "./../system/system_server.h"
#include "gui.h"
#include "input.h"
#include "./../web_server/web_server.h"

#define TOY_TOK_BUFSIZE 64
#define TOY_TOK_DELIM " \t\r\n\a"


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



/******************************** command thread, sensor thread func ********************************/

/* feature : sensor thread
 *  
 */
void *sensor_thread(void* arg)
{
    char *s = arg;

    printf("%s", s);

    while (1) {
        //posix_sleep_ms(5000);
        sleep(5);
    }

    return 0;
}

/* feature : command thread
 * 
 */
int toy_send(char **args);
int toy_shell(char **args);
int toy_exit(char **args);

char *builtin_str[] = {
    "send",
    "sh",
    "exit"
};

int (*builtin_func[]) (char **) = {
    &toy_send,
    &toy_shell,
    &toy_exit
};

int toy_num_builtins()
{
    return sizeof(builtin_str) / sizeof(char *);
}

int toy_send(char **args)
{
    printf("send message: %s\n", args[1]);

    return 1;
}

int toy_exit(char **args)
{
    return 0;
}

int toy_shell(char **args)
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid == 0) {
        if (execvp(args[0], args) == -1) {
            perror("toy");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("toy");
    } else
{
        do
        {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

int toy_execute(char **args)
{
    int i;

    if (args[0] == NULL) {
        return 1;
    }

    for (i = 0; i < toy_num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }

    return 1;
}

char *toy_read_line(void)
{
    char *line = NULL;
    ssize_t bufsize = 0;

    if (getline(&line, &bufsize, stdin) == -1) {
        if (feof(stdin)) {
            exit(EXIT_SUCCESS);
        } else {
            perror(": getline\n");
            exit(EXIT_FAILURE);
        }
    }
    return line;
}

char **toy_split_line(char *line)
{
    int bufsize = TOY_TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token, **tokens_backup;

    if (!tokens) {
        fprintf(stderr, "toy: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOY_TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize += TOY_TOK_BUFSIZE;
            tokens_backup = tokens;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                free(tokens_backup);
                fprintf(stderr, "toy: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOY_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

void toy_loop(void)
{
    char *line;
    char **args;
    int status;

    do {
        printf("TOY>");
        line = toy_read_line();
        args = toy_split_line(line);
        status = toy_execute(args);

        free(line);
        free(args);
    } while (status);
}

void *command_thread(void* arg)
{
    char *s = arg;

    printf("%s", s);

    toy_loop();

    return 0;
}

/******************************** command thread, sensor thread func ********************************/






int input_server()
{
    printf("나 input 프로세스!\n");


    /****************************************************************************************************/
    /************************************ SIGSEGV handler 등록 *******************************************/
    /****************************************************************************************************/
    /**
     * @note struct sigaction field
     *  1. sa_mask : 시그널 핸들러가 동작 중 블록되는 시그널 집합
     *  2. sa_flags : 시그널이 처리되는 방식 설정(flag option 설정)
     *  3_1. sa_sigaction : 확장된 signal handler func 지정
     *  3_2. sa_handler : 일반 signal handler func 지정
     * => 3_1, 3_2 중 하나만 사용
     *  
    */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sigaction));      //sigaction struct sa 초기화 
    sigemptyset(&sa.sa_mask);               //sigaction mask set 초기화

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
    /****************************************************************************************************/
    /************************************ SIGSEGV handler 등록 *******************************************/
    /****************************************************************************************************/


    /****************************************************************************************************/
    /************************************ command thread, sensor thread 생성 *****************************/
    /****************************************************************************************************/
    
    //1. pthread_t, pthread_attr_t 선언
    pthread_t sensorTh_tid, commandTh_tid;
    pthread_attr_t sensorTh_attr, commandTh_attr;

    //2. attr 초기화 + detach 설정
    if(pthread_attr_init(&sensorTh_attr)) perror("pthread_attr_init : sensorTh");
    if(pthread_attr_init(&commandTh_attr)) perror("pthread_attr_init : commandTh");

    if(pthread_attr_setdetachstate(&sensorTh_attr, PTHREAD_CREATE_DETACHED)) perror("pthread_attr_setdetachstate : sensorTh");
    if(pthread_attr_setdetachstate(&commandTh_attr, PTHREAD_CREATE_DETACHED)) perror("pthread_attr_setdetachstate : commandTh");

    //3. thread 생성
    if(pthread_create(&sensorTh_tid, &sensorTh_attr, sensor_thread, (void *)"sensor_thread")) perror("pthread_create : sensorTh");
    if(pthread_create(&commandTh_tid, &commandTh_attr, command_thread, (void *)"command_thread")) perror("pthread_create : commandTh");
    /****************************************************************************************************/
    /************************************ command thread, sensor thread 생성 *****************************/
    /****************************************************************************************************/


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

    }

    else if(inputPid == -1) perror("fork");

    // parent process   
    else                                    
    {
        int childStatus;                   /** local var : input server process의 terminating 상태 저장*/
        int child_wPid;                    /** local var : input server 기다린 이후, return되는 child Pid 저장*/

        printf("parent process!,  PID : %d\n", getpid());
        
        sleep(3);

       
        return inputPid;

    }

    return 0;
}
