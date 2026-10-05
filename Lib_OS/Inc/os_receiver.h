#ifndef OS_RECEIVER_H
#define OS_RECEIVER_H

#include <stdint.h>

/* OS 绑定和系统故障错误。 */
typedef enum os_receiver_err_e {
    OS_RECEIVER_ERR_OK = 0,
    OS_RECEIVER_ERR_RESOURCE = -1,
    OS_RECEIVER_ERR_LAYOUT = -2,
    OS_RECEIVER_ERR_INIT = -3,
    OS_RECEIVER_ERR_TIMER = -4,
    OS_RECEIVER_ERR_MALLOC = -5,
    OS_RECEIVER_ERR_STACK = -6
} os_receiver_err_t;

extern volatile int32_t g_v_os_fault;

/* @brief 覆盖生成的启动弱入口，绑定对象后发布持久 READY 位。
 * @param p_argument 标准 OS 参数，允许为 NULL。
 * @return 无，完成后删除自身任务。 */
void Os_startup_entry(void *p_argument);

/* @brief 覆盖生成的 gamepad 弱入口；就绪后仅调用 App 线程主体。
 * @param p_argument 标准 OS 参数，允许为 NULL。
 * @return 无，业务返回时退出任务。 */
void Os_gamepad_entry(void *p_argument);

/* @brief 覆盖生成的 transmit 弱入口。
 * @param p_argument 标准 OS 参数，允许为 NULL。
 * @return 无，业务返回时退出任务。 */
void Os_transmit_entry(void *p_argument);

/* @brief 覆盖生成的定时器弱回调，提交 App 的非阻塞节拍通知。
 * @param p_argument 标准 OS 参数，允许为 NULL。
 * @return 无，不执行阻塞工作。 */
void Os_link_tick_callback(void *p_argument);

#endif
