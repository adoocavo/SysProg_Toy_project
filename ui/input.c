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
#include <mqueue.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>    /*for potability*/
#include <sys/shm.h> 
#include <bits/shmlba.h>  /* */
#include <time.h>
#include <stdlib.h>

#include "./../system/system_server.h"
#include "gui.h"
#include "input.h"
#include "./../web_server/web_server.h"
#include "./../project_libs/toy_message.h"
#include "./../project_libs/shared_memory.h"

#define TOY_TOK_BUFSIZE 64
#define TOY_TOK_DELIM " \t\r\n\a"
#define TOY_BUFFSIZE 1024
#define NUM_OF_MQ 4             //open할 message queue file 개수


/***************************************************************************************/
/******************************** mutex lock + cond var - start ********************************/
/***************************************************************************************/

//global var -> critical sec 형성 / race condition 발생
static char global_message[TOY_BUFFSIZE];     

/** global var : terminal 출력 가능 여부 확인
 * @note  0 : Terminal에 output 가능, 1 : Terminal에 output write 불기
*/
// int TOY_prompt_operation_check = 0;           

//mutex var
static pthread_mutex_t global_message_mutex = PTHREAD_MUTEX_INITIALIZER;                 

//cond var
static pthread_cond_t global_message_cond = PTHREAD_COND_INITIALIZER;

/***************************************************************************************/
/******************************** mutex lock + cond var - end ********************************/
/***************************************************************************************/


/***************************************************************************************/
/******************************** message queue괸련 선언  - start ********************************/
/***************************************************************************************/

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
/******************************** message queue괸련 선언 - end ********************************/
/***************************************************************************************/


/***************************************************************************************/
/********************************  SV shm 괸련 선언 - start ********************************/
/***************************************************************************************/

static shm_sensor_t *the_sensor_info = NULL; 


/***************************************************************************************/
/******************************** SV shm 괸련 선언 - end ********************************/
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

/** feature : segfault_handler
 * @note SIGSEGV signal을 받은 proc의 stackFrame 출력 후 종료
*/
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
int toy_message_queue(char* *);


/** featute : TOY> prompt에서 입력 받아서 실행 가능한 'cmd 명/cmd 함수 명' char 포인터 / 함수 포인터 배열로 선언 
 * @note mq <""> <"send message">
*/
char *builtin_str[] = {
    "send",
    "mu",
    "sh",
    "exit",
    "mq"
};
int (*builtin_func[]) (char **) = {
    &toy_send,
    &toy_mutex,
    &toy_shell,
    &toy_exit,
    &toy_message_queue

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
    printf("나 %s", s);

    /** 0. shmget() / mq_send() 위한 var 선언 
     * @note shm_retcode, mq_retcode
     * @note sensor_shm_key : shmget()으로부터 생성된 key value 저장
     * @note toy_msg_t msg_to_monitor : mqueue로 전송
    */
    int shm_retcode;
    int sensor_shm_key;
    int mq_retcode;
    toy_msg_t msg_to_monitor;

    enum def_shm_key shm_key = SHM_KEY_SENSOR;
    

    while(1)
    {
        sleep(5);

        /***************************************************************************************/
        /******************************** SV shm 생성 + sensor_info 저장 - start ********************************/
        /***************************************************************************************/

        /** 1. shm segment 생성-1(shmget()) 
         * @note shmget() 를 사용하여 shm 생성 + key값 얻기
         * @return 생성된 shm segment's kev value(seg id) 
        */
        sensor_shm_key = shmget(shm_key, sizeof(shm_sensor_t), IPC_CREAT | SHMGET_FLAGS);
        assert(sensor_shm_key != -1);

        /** 2. shm segment 생성-2(shmgat()) 
         * @note shmgat() 를 사용하여 address에 shm을 attach
         * @note +) 프로세스가 종료될 때 자동으로 공유 메모리는 detach 된다.
         * @note 2번째 argument NULL : kernel이 적절한(사용하지 않은) 주소를 붙임
         * @return attached shm's address
        */
        the_sensor_info = (shm_sensor_t *)shmat(sensor_shm_key, NULL, SHMAT_FLAGS_RW);
        assert((void *)the_sensor_info != (void *)-1); 

        /** 3. shm segment에 data 저장
         * @note the_sensor_info 사용하여 저장
         * 
        */ 
        the_sensor_info->temp = rand() % 126 - 40;       //-40 ~ 85
        the_sensor_info->press = rand() % 101;           //mV : 0 ~ 100
        the_sensor_info->humidity = rand() % 21 - 30;    //RH : 30 ~ 50

        /***************************************************************************************/
        /******************************** SV shm 생성 + sensor_info 저장 - start ********************************/
        /***************************************************************************************/


        /***************************************************************************************/
        /******************************** message queue 전송(send) - start ********************************/
        /***************************************************************************************/
        //0. open 

        //1. send : 생성된 shm segment's kev value(seg id)를 전송
        msg_to_monitor.msg_type = 1;
        msg_to_monitor.param1 = sensor_shm_key;
        msg_to_monitor.param2 = 0;
        msg_to_monitor.param3 = NULL;


        mq_retcode = mq_send(mqds[1], (char *)&msg_to_monitor, sizeof(msg_to_monitor), 0);


        /***************************************************************************************/
        /******************************** message queue 전송(send) - end ********************************/
        /***************************************************************************************/

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

/** feature : toy_message_queue
 *  @note send message to camera_serice thread in system_server.c 
 *  @note "/camera_mq"에 write
 *  @note mq camera <"send message">
*/
int toy_message_queue(char* *args)
{
    int mq_retcode;
    toy_msg_t msg;

    if (args[1] == NULL || args[2] == NULL) 
    {
        return 1;
    }

    if (!strcmp(args[1], "camera")) 
    {
        msg.msg_type = atoi(args[2]);
        msg.param1 = 0;
        msg.param2 = 0;

//      msg.param3 = args[3];     //메세지 전달 추가 => 포인터를 전달하는거라 error발생

        mq_retcode = mq_send(mqds[3], (char *)&msg, sizeof(msg), 0);       //mqds[3] : "/camera_mq"
        assert(mq_retcode == 0);
    }

    //mqretcode = mq_receive(prompt_perm_mqd, );

    return 1;
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

    sleep(5);
    do 
    {
        // 여기는 그냥 중간에 "TOY>"가 출력되는거 보기 싫어서.. 뮤텍스
        if(pthread_mutex_lock(&global_message_mutex)) perror("pthread_mutex_lock : toy_loop(global_message_mutex)");

        sleep(1);

        /** 수정필요 
         * @note 깔끔하게 순서 맞춰서 terminal 출력 (현재는 sleep(5)로 대체)
         * @note 다른 threads/proecss가 terminal의 출력작업 종료 후 "TOY>"띄우기
        */
        printf("TOY>");

        if(pthread_mutex_unlock(&global_message_mutex)) perror("pthread_mutex_unlock : toy_loop(global_message_mutex)");

        line = toy_read_line();
        args = toy_split_line(line);
        status = toy_execute(args);

        free(line);
        free(args);
    } 
    while (status);
}

/** feature : command_thread
 * @note toy_loop() 호출
*/
void* command_thread(void* arg)
{
    char *s = arg;

    printf("나 %s", s);

    toy_loop();

    return 0;
}

/****************************************************************************************************/
/******************************** command thread, sensor thread func - end ********************************/
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
    /************************************ message queue open - start *****************/
    /****************************************************************************************************/
    
    //message queue open    
    for(int i = 0; i < NUM_OF_MQ; ++i)
    {
        mqds[i] = mq_open(msg_queues_str[i], O_RDWR);
        assert(mqds[i] != -1);
    }

    //prompt_perm_mqd = mq_open(prompt_perm_filename, O_RDONLY);


    /****************************************************************************************************/
    /************************************ message queue open - start *****************************/
    /****************************************************************************************************/


    /****************************************************************************************************/
    /************************************ input process 내에서 생성/실행 thread 선택 - start *****************/
    /****************************************************************************************************/
   
    //pthread_mutex_lock(&terminal_operation_mutex);
    //terminal_operation_check = 1;

    //sigsuspend();
    //kill(getpid(), SIGSTOP);
    //sleep(3);

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
    srand(time(NULL));

    /**
     * @note fork -> child : input server process 수행 
     *              parent : input server process 생성 후 wait
    */ 
    // child process
    printf("여기서 input 프로세스를 생성합니다.\n");   
    if((inputPid = fork()) == 0)           
    {
        //printf("child(input) process!,  PID : %d\n", getpid());
        
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

        // printf("parent process!,  PID : %d\n", getpid());
        
        sleep(3);

        return inputPid;

    }

    return 0;
}
