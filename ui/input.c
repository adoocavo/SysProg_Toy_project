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
#define TOY_BUFFSIZE 1024

/***************************************************************************************/
/******************************** mutex lock + cond var - start ********************************/
/***************************************************************************************/

//global var -> critical sec 형성 / race condition 발생
static char global_message[TOY_BUFFSIZE];     
//static char global_message_for_ITC[TOY_BUFFSIZE];     


/** global var : terminal 출력 가능 여부 확인
 * @note  0 : Terminal에 output 가능, 1 : Terminal에 output write 불기
*/
int TOY_prompt_operation_check = 0;           
//int terminal_operation_check = 0;

//mutex var
static pthread_mutex_t global_message_mutex = PTHREAD_MUTEX_INITIALIZER;                 
//pthread_mutex_t TOY_prompt_mutex = PTHREAD_MUTEX_INITIALIZER; 
//pthread_mutex_t terminal_operation_mutex = PTHREAD_MUTEX_INITIALIZER; 

//cond var
static pthread_cond_t global_message_cond = PTHREAD_COND_INITIALIZER;
//pthread_cond_t TOY_prompt_cond = PTHREAD_COND_INITIALIZER;
//pthread_cond_t terminal_operation_cond;





/***************************************************************************************/
/******************************** mutex lock + cond var - end ********************************/
/***************************************************************************************/


/***********************************************************************************/
/******************************** SIGSEGV : handler - start ********************************/
/***********************************************************************************/
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
/***********************************************************************************/
/******************************** SIGSEGV : handler - end ********************************/
/***********************************************************************************/


/****************************************************************************************************/
/******************************** command thread, sensor thread func - start ********************************/
/****************************************************************************************************/

//1. 함수 선언
/* feature : sensor thread
 *  
 */
void *sensor_thread(void*);             //mutex


/* feature : command thread 입력받은 cmd의 실행 함수 선언 
 * 
 */
int toy_send(char* *);
int toy_mutex(char* *);                 //mutex
int toy_shell(char* *);
int toy_exit(char* *);

/** featute : TOY> prompt에서 입력 받아서 실행 가능한 'cmd 명/cmd 함수 명' char 포인터 / 함수 포인터 배열로 선언 
 * 
*/
char *builtin_str[] = {
    "send",
    "mu",
    "sh",
    "exit"
};
int (*builtin_func[]) (char **) = {
    &toy_send,
    &toy_mutex,
    &toy_shell,
    &toy_exit
};
int toy_num_builtins(void);


/** feature : command_thread 실행에 포함되는 funcs
 *  @note command_thread() -> toy_loop() -> toy_read_line() -> toy_split_line() -> toy_execute()
*/ 
void* command_thread(void *);
void toy_loop(void);                        //mutex
char* toy_read_line(void);
char** toy_split_line(char *);         
int toy_execute(char* *);


//2. 함수 정의 
/* feature : sensor thread
 *  
 */
void *sensor_thread(void* arg)            //mutex
{
    char *s = arg;
    printf("%s", s);

    int i = 0;
    while (1) 
    {   
        i = 0;

        /******************************** pthread_mutex_lock + cond var - start ********************************/
        if(pthread_mutex_lock(&global_message_mutex)) perror("pthread_mutex_lock : sensor_thread(global_message_mutex)");
        //if(pthread_mutex_lock(&TOY_prompt_mutex)) perror("pthread_mutex_lock : sensor_thread(TOY_prompt_mutex)");


        //if(global_message[0] == NULL || TOY_prompt_operation_check)
        if(global_message[0] == NULL)
        {
            //printf("waiting for input from toy_mutex(TOY> mu)......\n");
            if(pthread_cond_wait(&global_message_cond, &global_message_mutex))perror("pthread_cond_wait : sensor_thread(global_message_mutex)");
            //if(pthread_cond_wait(&TOY_prompt_cond, &TOY_prompt_mutex)) perror("pthread_cond_wait : sensor_thread(TOY_prompt_mutex)");

            TOY_prompt_operation_check = 1;
        }

        while (global_message[i] != NULL)
        {
            printf("%c", global_message[i]);
            fflush(stdout);
            //posix_sleep_ms(500);
            sleep(1);
            i++;
        }
        TOY_prompt_operation_check = 0;

        memset(global_message, NULL, sizeof(global_message));

        if(pthread_mutex_unlock(&global_message_mutex)) perror("pthread_mutex_unlock : sensor_thread(global_message_mutex)");
        //if(pthread_mutex_unlock(&TOY_prompt_mutex)) perror("pthread_mutex_unlock : sensor_thread(TOY_prompt_mutex)");

        /******************************** pthread_mutex_lock + cond var - end ********************************/

        printf("\n");

        //posix_sleep_ms(5000);
        sleep(5);
    }

    return 0;
}

/** feature : TOY> prompt로 실행 가능한 명령어의 개수 return 
 * 
*/
int toy_num_builtins()
{
    return sizeof(builtin_str) / sizeof(char *);
}

/** feature : toy_send
 *  @note 'TOY> send' 입력시 실행동작 정의
 *  @note  TOY> send <string>
*/
int toy_send(char **args)
{
    printf("send message: %s\n", args[1]);

    return 1;
}

/** feature : toy_mutex
 *  @note 'TOY> mu' 입력시 실행동작 정의
 *  @note  TOY> mu <string>
*/
int toy_mutex(char **args)                //mutex
{
    if (args[1] == NULL) 
    {
        return 1;
    }

    printf("save message: %s\n", args[1]);
    
    /******************************** pthread_mutex_lock - start ********************************/
    if(pthread_mutex_lock(&global_message_mutex)) perror("pthread_mutex_lock : toy_mutex");

    // 여기서 뮤텍스
    strcpy(global_message, args[1]);

    //printf("global_message : %s\n", global_message);

    if(pthread_mutex_unlock(&global_message_mutex)) perror("pthread_mutex_unlock : toy_mutex");
    if(pthread_cond_signal(&global_message_cond))  perror("pthread_cond_signal : toy_mutex");
    /******************************** pthread_mutex_lock - end ********************************/

    sleep(3);
    return 1;
}

/** feature : toy_exit
 *  @note 'TOY> exit' 입력시 실행동작 정의
*/
int toy_exit(char **args)
{
    return 0;
}

/** feature : toy_shell
 *  @note 'TOY> sh' 입력시 실행동작 정의
*/
int toy_shell(char **args)
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid == 0) 
    {
        if (execvp(args[0], args) == -1)
        {
            perror("toy");
        }
        exit(EXIT_FAILURE);
    } 
   
    else if (pid < 0) 
    {
        perror("toy");
    }
   
    else
    {
        do
        {
            waitpid(pid, &status, WUNTRACED);
        } 
        while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}


/** feature : toy_execute 
 * @note 입력받은 명령어에 대한 함수 실행 
 * @note args[0]에 실행할 명령어 저장되어있음 => builtin_str[i]과 비교 후, 함수 포인터 배열(LookUp ary)사용해서 call 
*/
int toy_execute(char **args)
{
    int i;

    if (args[0] == NULL)
    {
        return 1;
    }

    for (i = 0; i < toy_num_builtins(); i++)
    {
        if (strcmp(args[0], builtin_str[i]) == 0) 
        {
            return (*builtin_func[i])(args);
        }
    }

    return 1;
}

/** feature : toy_read_line 
 * @note TOY> prompt로부터 line 단위 입력 받기 -> line에 저장 
*/
char* toy_read_line(void)
{
    char *line = NULL;
    ssize_t bufsize = 0;

    if (getline(&line, &bufsize, stdin) == -1) 
    {
        if (feof(stdin))
        {
            exit(EXIT_SUCCESS);
        } 
        else
        {
            perror(": getline\n");
            exit(EXIT_FAILURE);
        }
    }
    return line;
}

/** feature : toy_split_line 
 * @note char* *tokens = malloc(bufsize * sizeof(char *) =>  char* tokens[bufsize] => char* type의 data를 bufsize개수만큼~ 
 * @note 입력받은 문자열 split -> tokens 배열에 저장
*/
char** toy_split_line(char *line)
{
    int bufsize = TOY_TOK_BUFSIZE, position = 0;
    char* *tokens = (char*)malloc(bufsize * sizeof(char *));
    char *token, **tokens_backup;

    if (!tokens) 
    {
        fprintf(stderr, "toy: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOY_TOK_DELIM);
    while (token != NULL) 
    {
        tokens[position] = token;
        position++;

        if (position >= bufsize) 
        {
            bufsize += TOY_TOK_BUFSIZE;
            tokens_backup = tokens;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            
            if (!tokens)
            {
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


/** feature : toy_loop 
 * @note toy_read_line() -> toy_split_line() -> toy_execute() 호출
*/
void toy_loop(void)                       //mutex
{
    char *line;
    char* *args;
    int status;

   // if(pthread_mutex_lock(&global_message_mutex)) perror("pthread_mutex_lock : toy_loop");
    do 
    {
        // 여기는 그냥 중간에 "TOY>"가 출력되는거 보기 싫어서.. 뮤텍스
        if(pthread_mutex_lock(&global_message_mutex)) perror("pthread_mutex_lock : toy_loop(global_message_mutex)");
        //if(pthread_mutex_lock(&TOY_prompt_mutex)) perror("pthread_mutex_lock : toy_loop(TOY_prompt_mutex)");
        
        while(TOY_prompt_operation_check)
        {
            sleep(1);
        }
        
        /** 수정필요 
         * @note : 깔끔하게 순서 맞춰서 terminal 출력 
        */
        //TOY_prompt_operation_check = 1;
        printf("TOY>");
        //TOY_prompt_operation_check = 0;

        if(pthread_mutex_unlock(&global_message_mutex)) perror("pthread_mutex_unlock : toy_loop(global_message_mutex)");
        //if(pthread_mutex_unlock(&TOY_prompt_mutex)) perror("pthread_mutex_unlock : toy_loop(TOY_prompt_mutex)");

        line = toy_read_line();
        args = toy_split_line(line);
        status = toy_execute(args);

        free(line);
        free(args);
    } 
    while (status);

    //if(pthread_mutex_unlock(&global_message_mutex)) perror("pthread_mutex_unlock : toy_loop"); //=> 이갓때문에 deadlock

}

/** feature : command_thread
 * @note toy_loop() 호출
*/
void* command_thread(void* arg)
{
    char *s = arg;

    printf("%s", s);

    toy_loop();

    return 0;
}
/****************************************************************************************************/
/******************************** command thread, sensor thread func - end ********************************/
/****************************************************************************************************/



/****************************************************************************************************/
/******************** Inter threads communication(by using message queue) - start *******************/
/****************************************************************************************************/
#define MAX 30
#define NUMTHREAD 3 /* number of threads */

char buffer[TOY_BUFFSIZE];               //message queue => critical sec
int read_count = 0, write_count = 0;
int buflen;

pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
int producer_count = 0, consumer_count = 0;

void *toy_consumer(int *id)
{
    pthread_mutex_lock(&count_mutex);
    while (consumer_count < MAX) 
    {
        pthread_cond_wait(&empty, &count_mutex);
        
        // 큐에서 하나 꺼낸다.
        printf("                           소비자[%d]: %c\n", *id, buffer[read_count]);
        read_count = (read_count + 1) % TOY_BUFFSIZE;
        fflush(stdout);
        consumer_count++;
    }
    pthread_mutex_unlock(&count_mutex);
}

void *toy_producer(int *id)
{
    //printf("\n");
    while (producer_count < MAX) 
    {
        pthread_mutex_lock(&count_mutex);
        strcpy(buffer, "");
        buffer[write_count] = global_message[write_count % buflen];

        // 큐에 추가한다.
        printf("%d - 생산자[%d]: %c \n", producer_count, *id, buffer[write_count]);
        fflush(stdout);
        write_count = (write_count + 1) % TOY_BUFFSIZE;
        producer_count++;
        pthread_cond_signal(&empty);
        pthread_mutex_unlock(&count_mutex);

        sleep(rand() % 3);
    }
}
/****************************************************************************************************/
/******************** Inter threads communication(by using message queue) - end *********************/
/****************************************************************************************************/


/** feature : input process operation
 * @note SIGSEGV handler 등록, command thread/sensor thread 생성
*/
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
    /************************************ input process 내에서 생성/실행 thread 선택 - start *****************/
    /****************************************************************************************************/
    //pthread_mutex_lock(&terminal_operation_mutex);
    //terminal_operation_check = 1;

    //sigsuspend();
    kill(getpid(), SIGSTOP);
    sleep(3);
    int menu;

    //if(pthread_mutex_lock(&global_message_mutex)) perror("pthread_mutex_lock : input_server(global_message_mutex)");
    printf("intput operation mode(0 : cmd/sensoe th, 1 : ITC) : ");
    //if(pthread_mutex_unlock(&global_message_mutex)) perror("pthread_mutex_lock : input_server(global_message_mutex)");

    scanf(" %d", &menu);

    /** 수정필요 
     * @note : 아딴 방식으로 input mode를 구분할지... + 반복 입력( 깔끔하게 순서 맞춰서)
    */
    if(!menu)
    {    
        /****************************************************************************************************/
        /************************************ command thread, sensor thread 생성 - start *****************************/
        /****************************************************************************************************/
        
        //1. pthread_t, pthread_attr_t 선언
        pthread_t sensorTh_tid, commandTh_tid;
        pthread_attr_t sensorTh_attr, commandTh_attr;
        int retcode;

        //2. attr 초기화 + detach 설정
        if(pthread_attr_init(&sensorTh_attr)) perror("pthread_attr_init : sensorTh");
        if(pthread_attr_init(&commandTh_attr)) perror("pthread_attr_init : commandTh");

        if(pthread_attr_setdetachstate(&sensorTh_attr, PTHREAD_CREATE_DETACHED)) perror("pthread_attr_setdetachstate : sensorTh");
        if(pthread_attr_setdetachstate(&commandTh_attr, PTHREAD_CREATE_DETACHED)) perror("pthread_attr_setdetachstate : commandTh");

        //3. thread 생성
        if((retcode = pthread_create(&sensorTh_tid, &sensorTh_attr, sensor_thread, (void *)"sensor_thread\n"))) perror("pthread_create : sensorTh");
        assert(retcode == 0);

        if((retcode = pthread_create(&commandTh_tid, &commandTh_attr, command_thread, (void *)"command_thread\n"))) perror("pthread_create : commandTh");
        assert(retcode == 0);

        /****************************************************************************************************/
        /************************************ command thread, sensor thread 생성 - end*****************************/
        /****************************************************************************************************/
    }

    else if(menu == 1)
    {
        /****************************************************************************************************/
        /************************************ producer/consumer(for ITC) - start *****************************/
        /****************************************************************************************************/
        
        //1. pthread_t 선언, pthread_attr_t 설정
        pthread_t thread_producer_consumer[NUMTHREAD];
        pthread_attr_t thread_producer_consumer_attrs[NUMTHREAD];
        int thread_id[NUMTHREAD] = {0, 1, 2};

        for(int i = 0; i < NUMTHREAD; ++i)
        {
            if(pthread_attr_init(&thread_producer_consumer_attrs[i])) perror("pthread_attr_init");
            if(pthread_attr_setdetachstate(&thread_producer_consumer_attrs[i], PTHREAD_CREATE_DETACHED)) perror("pthread_attr_setdetachstate");
        }

        //2. mutex lock
        pthread_mutex_lock(&global_message_mutex);

        strcpy(global_message, "hello world!");
        buflen = strlen(global_message);
        
        pthread_mutex_unlock(&global_message_mutex);
        
        //sleep(10);
        //3. pthread 생성 : consumer 1, producer 2
        pthread_create(&thread_producer_consumer[0], NULL, (void *)toy_consumer, &thread_id[0]);
        pthread_create(&thread_producer_consumer[1], NULL, (void *)toy_producer, &thread_id[1]);
        pthread_create(&thread_producer_consumer[2], NULL, (void *)toy_producer, &thread_id[2]);

        /****************************************************************************************************/
        /************************************ producer/consumer(for ITC) - end *****************************/
        /****************************************************************************************************/
    }
    else{;}
    
    /****************************************************************************************************/
    /************************************ input process 내에서 생성/실행 thread 선택 - end *******************/
    /****************************************************************************************************/

    //terminal_operation_check = 0;
    //pthread_mutex_unlock(&terminal_operation_mutex);



    while (1) 
    {
        sleep(1);
    }

    return 0;
}

/** feature : input server process 생성(by fork())
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
