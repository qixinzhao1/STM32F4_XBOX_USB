#ifndef UART_TRANSPORT_H
#define UART_TRANSPORT_H

#include "cmsis_os2.h"
#include "vofa.h"

#include <stddef.h>
#include <stdint.h>

/* UART DMA 传输错误。 */
typedef enum uart_transport_err_e {
    UART_TRANSPORT_ERR_OK = 0,
    UART_TRANSPORT_ERR_NULL = -1,
    UART_TRANSPORT_ERR_RESOURCE = -2,
    UART_TRANSPORT_ERR_HAL = -3,
    UART_TRANSPORT_ERR_TIMEOUT = -4,
    UART_TRANSPORT_ERR_LENGTH = -5
} uart_transport_err_t;

/* @brief 绑定已有线程锁和完成信号量，注册 UART 回调。
 * @param c_mutex 生成层创建的互斥量，不能为空。
 * @param c_done 生成层创建且初始耗尽的二值信号量，不能为空。
 * @return 0 成功，负值失败。 */
uart_transport_err_t Uart_transport_Init(osMutexId_t c_mutex, osSemaphoreId_t c_done);

/* @brief 在线程中复制并 DMA 发送一帧，等待真实 UART TC 完成。
 * @param p_frame JustFloat 帧，不能为空。
 * @param c_length 只接受 36 字节。
 * @return 0 成功，负值失败；ISR 不调用此函数。 */
uart_transport_err_t Uart_transport_Send(const uint8_t *p_frame, size_t c_length);

#endif
