#ifndef _TOY_MESSAGE_H_
#define _TOY_MESSAGE_H_

#include <unistd.h>

/** feature : system_server proc 내에서 생성되는 threads들의 msg queue mqd저장
 * 
*/
#define MQ_NUM_MSG 10                //각 posix msg queue에 저장될 수 있는 msg 개수
#define MQ_MSG_SIZE 2048             //각 msg의 size(default : 8KB)
#define NUM_OF_MQ 4                  //생성할 message queue file 개수
#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) 

typedef struct
{
    unsigned int msg_type;
    unsigned int param1;
    unsigned int param2;
    void *param3;
} toy_msg_t;



#endif /* _TOY_MESSAGE_H_ */