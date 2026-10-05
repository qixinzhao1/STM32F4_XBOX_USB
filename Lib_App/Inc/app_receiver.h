#ifndef APP_RECEIVER_H
#define APP_RECEIVER_H

#include "cmsis_os2.h"

#include <stdint.h>

/* App 业务接入错误。 */
typedef enum app_receiver_err_e {
    APP_RECEIVER_ERR_OK = 0,
    APP_RECEIVER_ERR_NULL = -1,
    APP_RECEIVER_ERR_INIT = -2,
    APP_RECEIVER_ERR_EVENT = -3,
    APP_RECEIVER_ERR_SEND = -4
} app_receiver_err_t;

/* 已创建资源的使用视图；App 不直接 extern 生成层全局对象。 */
typedef struct app_resources_s {
    osMessageQueueId_t queue; /* 原始输入/状态队列。 */
    osMutexId_t uart_mutex;   /* UART 线程访问锁。 */
    osSemaphoreId_t uart_done;/* DMA/UART TC 完成通知。 */
    osEventFlagsId_t events;  /* READY/WORK/TICK 事件。 */
} app_resources_t;

/* 可在调试器检查的应用诊断。 */
typedef struct app_diag_s {
    uint32_t valid_frames;   /* 完成发送的有效帧次数。 */
    uint32_t invalid_frames; /* 完成发送的零值失效帧次数。 */
    uint32_t rejected;       /* 长度、类型或旧连接报告丢弃次数。 */
    uint32_t uart_errors;    /* UART DMA 发送失败次数。 */
    int32_t last_error;      /* 最新业务/BSP 错误。 */
    uint32_t input_valid;    /* 当前输入有效状态，不代表 USB 收到新采样频率。 */
} app_diag_t;

extern volatile app_diag_t g_v_app_diag;

/* @brief 使用 Lib_OS 注入的已有对象初始化业务及 BSP。
 * @param p_resources 使用视图，所有句柄不能为空。
 * @return 0 成功，负值失败；调用方只能为 Lib_OS。 */
app_receiver_err_t App_receiver_Init(const app_resources_t *p_resources);

/* @brief Gamepad 线程主体，等待就绪后由 Lib_OS 接入。
 * @param p_argument 标准任务参数，允许为 NULL。
 * @return 无，长期运行；不直接由其他 App 调用。 */
void App_gamepad_entry(void *p_argument);

/* @brief 发送线程主体；正常按采样发送，失联按节拍发送零帧。
 * @param p_argument 标准任务参数，允许为 NULL。
 * @return 无，长期运行；接入调用方只能为 Lib_OS。 */
void App_transmit_entry(void *p_argument);

/* @brief 软件定时器只提交非阻塞节拍通知。
 * @return 0 成功，负值失败；调用方只能为 Lib_OS 定时器桥接。 */
app_receiver_err_t App_receiver_NotifyTick(void);

#endif
