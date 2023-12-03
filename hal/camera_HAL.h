#ifndef _CAMERA_HAL_H_
#define _CAMERA_HAL_H_

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/*********C code에서 보여질(system/system_server.c 내의 camera_service thread에서 호출될) code *******/
#include <stdint.h>

int toy_camera_open(void);
int toy_camera_take_picture(void);
/*********C code에서 보여질(system/system_server.c 내의 camera_service thread에서 호출될) code *******/

#ifdef __cplusplus
} // extern "C"
#endif  // __cplusplus

#endif /* _CAMERA_HAL_H_ */
