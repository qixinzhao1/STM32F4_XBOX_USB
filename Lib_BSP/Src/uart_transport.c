#include "uart_transport.h"
#include "project_config.h"
#include "usart.h"

#include <string.h>

static osMutexId_t s_tx_mutex;
static osSemaphoreId_t s_tx_done;
static uint8_t s_tx_buffer[VOFA_FRAME_SIZE];
static volatile uint32_t s_v_tx_error;

/* @brief UART TC 完成通知；不在 ISR 操作互斥量。
 * @param p_uart HAL 提供的句柄；为空或非 USART1 时忽略。
 * @return 无，HAL 回调 ABI。 */
static void _tx_complete(UART_HandleTypeDef *p_uart)
{
    if ((p_uart == NULL) || (p_uart != &huart1) || (s_tx_done == NULL)) {
        return;
    }
    /* 只唤醒拥有 DMA 缓冲区的线程，不继续执行业务。 */
    (void)osSemaphoreRelease(s_tx_done);
}

/* @brief UART 故障通知，先记录再唤醒等待线程。
 * @param p_uart HAL 句柄；为空或非 USART1 时忽略。
 * @return 无，HAL 回调 ABI。 */
static void _tx_error(UART_HandleTypeDef *p_uart)
{
    if ((p_uart == NULL) || (p_uart != &huart1) || (s_tx_done == NULL)) {
        return;
    }
    s_v_tx_error = 1U;
    (void)osSemaphoreRelease(s_tx_done);
}

uart_transport_err_t Uart_transport_Init(osMutexId_t c_mutex, osSemaphoreId_t c_done)
{
    if ((c_mutex == NULL) || (c_done == NULL)) {
        return UART_TRANSPORT_ERR_NULL;
    }
    /* 资源由 Lib_OS 注入；使用生成的 USART1 配置，不重复初始化。 */
    s_tx_mutex = c_mutex;
    s_tx_done = c_done;
    if ((HAL_UART_RegisterCallback(&huart1, HAL_UART_TX_COMPLETE_CB_ID, _tx_complete) != HAL_OK) ||
        (HAL_UART_RegisterCallback(&huart1, HAL_UART_ERROR_CB_ID, _tx_error) != HAL_OK)) {
        return UART_TRANSPORT_ERR_HAL;
    }
    return UART_TRANSPORT_ERR_OK;
}

uart_transport_err_t Uart_transport_Send(const uint8_t *p_frame, size_t c_length)
{
    uart_transport_err_t err = UART_TRANSPORT_ERR_OK;
    if (p_frame == NULL) {
        return UART_TRANSPORT_ERR_NULL;
    }
    if (c_length != VOFA_FRAME_SIZE) {
        return UART_TRANSPORT_ERR_LENGTH;
    }
    if ((s_tx_mutex == NULL) || (s_tx_done == NULL) ||
        (osMutexAcquire(s_tx_mutex, PROJECT_UART_WAIT_TICKS) != osOK)) {
        return UART_TRANSPORT_ERR_RESOURCE;
    }
    /* 丢弃旧通知，缓冲区由当前线程持有到 UART 最后一个停止位发完。 */
    while (osSemaphoreAcquire(s_tx_done, 0U) == osOK) {
    }
    memcpy(s_tx_buffer, p_frame, sizeof(s_tx_buffer));
    s_v_tx_error = 0U;
    if (HAL_UART_Transmit_DMA(&huart1, s_tx_buffer, VOFA_FRAME_SIZE) != HAL_OK) {
        err = UART_TRANSPORT_ERR_HAL;
    } else if (osSemaphoreAcquire(s_tx_done, PROJECT_UART_WAIT_TICKS) != osOK) {
        (void)HAL_UART_AbortTransmit(&huart1);
        err = UART_TRANSPORT_ERR_TIMEOUT;
    } else if (s_v_tx_error != 0U) {
        (void)HAL_UART_AbortTransmit(&huart1);
        err = UART_TRANSPORT_ERR_HAL;
    }
    (void)osMutexRelease(s_tx_mutex);
    return err;
}
