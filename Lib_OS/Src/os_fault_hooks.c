#include "os_receiver.h"
#include "FreeRTOS.h"
#include "task.h"

/* @brief 记录致命故障并停机，不从损坏栈中继续调用 OS/HAL。
 * @param c_error 故障码。
 * @return 不返回。 */
static void _halt_fault(os_receiver_err_t c_error)
{
    /* 不重定义 freertos.c 中的强 hook；由链接器 --wrap 接入。 */
    g_v_os_fault = c_error;
    __asm volatile ("cpsid i" ::: "memory");
    for (;;) {
        __asm volatile ("nop");
    }
}

/* @brief 替换其他目标文件对 malloc hook 的调用。
 * @return 不返回，保留生成的原函数。 */
void __wrap_vApplicationMallocFailedHook(void)
{
    _halt_fault(OS_RECEIVER_ERR_MALLOC);
}

/* @brief 替换内核对 stack overflow hook 的调用。
 * @param task 内核 ABI 任务句柄，可能为空。
 * @param p_name 内核 ABI 名称，可能为空；此处不读取损坏堆栈内容。
 * @return 不返回。 */
void __wrap_vApplicationStackOverflowHook(TaskHandle_t task, char *p_name)
{
    /* ABI 参数不解引用，不把生成的强 hook 再定义一遍。 */
    (void)task;
    (void)p_name;
    _halt_fault(OS_RECEIVER_ERR_STACK);
}
