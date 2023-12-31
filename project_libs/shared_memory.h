#ifndef _SHARED_MEMORY_H
#define _SHARED_MEMORY_H

#include <sys/stat.h>
// #include <bits/shm.h>
#include <sys/shm.h>
#include "/usr/include/linux/limits.h"        /* ??? : for NAME_MAX?*/

/* Hard-coded keys for IPC objects */
enum def_shm_key                        /* Key for shared memory segment */
{
    SHM_KEY_BASE = 10,
    SHM_KEY_SENSOR = SHM_KEY_BASE,
    SHM_KEY_CMD_R_FILE,
    SHM_KEY_DISK,
    SHM_KEY_MAX
};

/*
// Permission flag for shmget.  
#define SHM_R		0400		// or S_IRUGO from <linux/stat.h> 
#define SHM_W		0200		// or S_IWUGO from <linux/stat.h> 
*/
/* set of optons and permission bits */
#define SHMGET_FLAGS_CREAT (IPC_CREAT | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)              /* 644*/  
#define SHMGET_FLAGS_EXCL (IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)   /* 644*/    

/*
//Flags for `shmat'.  
#define SHM_RDONLY	010000		// attach read-only else read-write 
#define SHM_RND		020000		// round attach address to SHMLBA()
#define SHM_REMAP	040000		// take-over region on attach 
#define SHM_EXEC	0100000		// execution access 
*/
#define SHMAT_FLAGS_R (SHM_RDONLY)       
#define SHMAT_FLAGS_RW 0
#define SHM_STR_MSG_BUF_SIZE 1024

/* Defines structure of shared memory segment for sensor thread */
typedef struct shm_sensor                                
{
    int temp;
    int press;
    int humidity;
} shm_sensor_t;


/* Defines structure of shared memory segment for transfering string data */
typedef struct shm_str_msg 
{                 
    int cnt;                    /* Number of bytes used in 'buf' */
    char buf[SHM_STR_MSG_BUF_SIZE];         /* Data being transferred */
} shm_str_msg_t;


typedef struct shm_diskInfo_msg
{
    int readByte;
    char filename[NAME_MAX];

    int totalSize_of_dir;
} shm_diskInfo_msg_t;


/* shm_id[SHM_KEY_MAX - 사용 key MACRO] : shmget() return value 저장 */
//extern int shm_id[SHM_KEY_MAX - SHM_KEY_BASE];


#endif /* _SHARED_MEMORY_H */
