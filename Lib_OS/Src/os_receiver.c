#include "os_receiver.h"
#include "app_receiver.h"
#include "gamepad_message.h"
#include "cmsis_os2.h"

#define OS_RECEIVER_EVENT_READY       UINT32_C(0x01)
#define OS_RECEIVER_LINK_PERIOD_TICKS UINT32_C(1)

/* 只有此层引用生成资源，App/BSP 均从初始化参数获得句柄。 */
extern osThreadId_t g_os_startupHandle;
extern osThreadId_t g_gamepad_taskHandle;
extern osThreadId_t g_transmit_taskHandle;
extern osMessageQueueId_t g_gamepad_queueHandle;
extern osMutexId_t g_uart_tx_mutexHandle;
extern osSemaphoreId_t g_uart_tx_doneHandle;
extern osEventFlagsId_t g_system_eventsHandle;
extern osTimerId_t g_link_tickHandle;

volatile int32_t g_v_os_fault;

/* @brief 检查创建结果、消息布局和初始状态，不创建替代资源。
 * @return 0 成功，负值表示生成资源无法使用。 */
static os_receiver_err_t _bind_resources(void)
{
    if ((g_os_startupHandle == NULL) || (g_gamepad_taskHandle == NULL) ||
        (g_transmit_taskHandle == NULL) || (g_gamepad_queueHandle == NULL) ||
        (g_uart_tx_mutexHandle == NULL) || (g_uart_tx_doneHandle == NULL) ||
        (g_system_eventsHandle == NULL) || (g_link_tickHandle == NULL)) {
        return OS_RECEIVER_ERR_RESOURCE;
    }
    /* 容量以生成对象为准，只核对非空容量和协议元素布局。 */
    if ((osMessageQueueGetMsgSize(g_gamepad_queueHandle) != sizeof(gamepad_message_t)) ||
        (osMessageQueueGetCapacity(g_gamepad_queueHandle) == 0U) ||
        (osSemaphoreGetCount(g_uart_tx_doneHandle) != 0U) ||
        (osEventFlagsGet(g_system_eventsHandle) != 0U) ||
        (osKernelGetTickFreq() != 1000U)) {
        return OS_RECEIVER_ERR_LAYOUT;
    }
    return OS_RECEIVER_ERR_OK;
}

/* @brief 等待持久就绪状态，不消费 READY 位。
 * @return 0 表示业务初始化成功，负值表示等待或绑定失败。 */
static os_receiver_err_t _wait_ready(void)
{
    if (g_system_eventsHandle == NULL) {
        g_v_os_fault = OS_RECEIVER_ERR_RESOURCE;
        return OS_RECEIVER_ERR_RESOURCE;
    }
    /* 两个工作线程都要看到同一个 READY 状态。 */
    if ((osEventFlagsWait(g_system_eventsHandle, OS_RECEIVER_EVENT_READY,
                         osFlagsWaitAll | osFlagsNoClear, osWaitForever) & osFlagsError) != 0U) {
        return OS_RECEIVER_ERR_RESOURCE;
    }
    return OS_RECEIVER_ERR_OK;
}

void Os_startup_entry(void *p_argument)
{
    os_receiver_err_t err;
    app_resources_t resources;
    (void)p_argument;
    err = _bind_resources();
    if (err == OS_RECEIVER_ERR_OK) {
        resources.queue = g_gamepad_queueHandle;
        resources.uart_mutex = g_uart_tx_mutexHandle;
        resources.uart_done = g_uart_tx_doneHandle;
        resources.events = g_system_eventsHandle;
        if (App_receiver_Init(&resources) != APP_RECEIVER_ERR_OK) {
            err = OS_RECEIVER_ERR_INIT;
        } else if (osTimerStart(g_link_tickHandle, OS_RECEIVER_LINK_PERIOD_TICKS) != osOK) {
            err = OS_RECEIVER_ERR_TIMER;
        } else if ((osEventFlagsSet(g_system_eventsHandle, OS_RECEIVER_EVENT_READY) & osFlagsError) != 0U) {
            err = OS_RECEIVER_ERR_RESOURCE;
        }
    }
    /* 初始化失败不发布 READY，不改变生成优先级或临时创建资源。 */
    g_v_os_fault = err;
    osThreadExit();
}

void Os_gamepad_entry(void *p_argument)
{
    /* 参数为标准任务参数，允许为 NULL。 */
    if (_wait_ready() == OS_RECEIVER_ERR_OK) {
        App_gamepad_entry(p_argument);
    }
    osThreadExit();
}

void Os_transmit_entry(void *p_argument)
{
    /* App 业务入口只由此 OS 接管层调用。 */
    if (_wait_ready() == OS_RECEIVER_ERR_OK) {
        App_transmit_entry(p_argument);
    }
    osThreadExit();
}

void Os_link_tick_callback(void *p_argument)
{
    (void)p_argument;
    /* 短通知；定时器服务任务不等 DMA。 */
    if (App_receiver_NotifyTick() != APP_RECEIVER_ERR_OK) {
        g_v_os_fault = OS_RECEIVER_ERR_TIMER;
    }
}
