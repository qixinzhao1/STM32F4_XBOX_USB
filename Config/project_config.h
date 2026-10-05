#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

#include <stdint.h>

/* 事件位和使用超时；任务、栈、队列及资源创建属性仅由 .ioc 配置。 */
#define PROJECT_EVENT_READY       UINT32_C(0x01)
#define PROJECT_EVENT_WORK        UINT32_C(0x02)
#define PROJECT_EVENT_LINK_TICK   UINT32_C(0x04)
#define PROJECT_LINK_PERIOD_TICKS UINT32_C(1)
#define PROJECT_USB_PROCESS_TICKS UINT32_C(1)
#define PROJECT_UART_WAIT_TICKS   UINT32_C(20)
#define PROJECT_USB_VID           UINT16_C(0x045E)
#define PROJECT_USB_PID           UINT16_C(0x028E)

#endif
