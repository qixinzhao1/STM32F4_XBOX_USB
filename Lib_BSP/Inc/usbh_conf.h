#ifndef USBH_CONF_H
#define USBH_CONF_H

#include "stm32f4xx_hal.h"
#include <stdlib.h>
#include <string.h>

/* 单设备 Host Core；不创建第二套任务、队列或锁。 */
#define USBH_MAX_NUM_ENDPOINTS         2U
#define USBH_MAX_NUM_INTERFACES        1U
#define USBH_MAX_NUM_CONFIGURATION     1U
#define USBH_KEEP_CFG_DESCRIPTOR       1U
#define USBH_MAX_NUM_SUPPORTED_CLASS   1U
#define USBH_MAX_SIZE_CONFIGURATION    128U
#define USBH_MAX_DATA_BUFFER           512U
#define USBH_MAX_PIPES_NBR             8U
#define USBH_DEBUG_LEVEL               0U
#define USBH_USE_OS                    0U
#define USBH_IN_NAK_PROCESS            0
#define USBH_malloc                    malloc
#define USBH_free                      free
#define USBH_memset                    memset
#define USBH_memcpy                    memcpy
#define USBH_UsrLog(...)               do { } while (0)
#define USBH_ErrLog(...)               do { } while (0)
#define USBH_DbgLog(...)               do { } while (0)

/* ST Core 在枚举字符串时请求 255B，缓冲区不能按 64B 输入端点缩小。 */
_Static_assert(USBH_MAX_DATA_BUFFER >= 255U, "Host descriptor receive buffer is too small");

#endif
