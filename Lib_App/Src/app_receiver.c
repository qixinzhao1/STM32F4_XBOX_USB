#include "app_receiver.h"
#include "gamepad_stream.h"
#include "uart_transport.h"
#include "usb_host.h"
#include "vofa.h"

#define APP_USB_PROCESS_TICKS UINT32_C(1)

volatile app_diag_t g_v_app_diag;
static app_resources_t s_resources;

/* @brief 从当前连接快照打包并完成一帧发送。
 * @param p_stream 输入数据流，不能为空。
 * @return 0 成功，负值失败，同时记录业务诊断。 */
static app_receiver_err_t _send_stream(gamepad_stream_t *p_stream)
{
    uint8_t frame[VOFA_FRAME_SIZE];
    uart_transport_err_t err;
    if (p_stream == NULL) {
        g_v_app_diag.last_error = APP_RECEIVER_ERR_NULL;
        return APP_RECEIVER_ERR_NULL;
    }
    /* 排队期间可能断线，发送前再核对当前连接代号。 */
    (void)Gamepad_stream_Sync(p_stream, Usb_host_GetSession());
    if (Vofa_Pack(&p_stream->state, p_stream->valid, frame, sizeof(frame)) != VOFA_ERR_OK) {
        g_v_app_diag.last_error = APP_RECEIVER_ERR_INIT;
        return APP_RECEIVER_ERR_INIT;
    }
    err = Uart_transport_Send(frame, sizeof(frame));
    if (err != UART_TRANSPORT_ERR_OK) {
        g_v_app_diag.uart_errors++;
        g_v_app_diag.last_error = err;
        return APP_RECEIVER_ERR_SEND;
    }
    g_v_app_diag.input_valid = p_stream->valid;
    if (p_stream->valid != 0U) {
        g_v_app_diag.valid_frames++;
    } else {
        g_v_app_diag.invalid_frames++;
    }
    return APP_RECEIVER_ERR_OK;
}

app_receiver_err_t App_receiver_Init(const app_resources_t *p_resources)
{
    if ((p_resources == NULL) || (p_resources->queue == NULL) ||
        (p_resources->uart_mutex == NULL) || (p_resources->uart_done == NULL) ||
        (p_resources->events == NULL)) {
        return APP_RECEIVER_ERR_NULL;
    }
    /* 只绑定，OS 对象不在业务层创建或调整优先级。 */
    s_resources = *p_resources;
    if ((Uart_transport_Init(s_resources.uart_mutex, s_resources.uart_done) != UART_TRANSPORT_ERR_OK) ||
        (Usb_host_Init(s_resources.queue, s_resources.events) != USB_HOST_ERR_OK)) {
        return APP_RECEIVER_ERR_INIT;
    }
    return APP_RECEIVER_ERR_OK;
}

void App_gamepad_entry(void *p_argument)
{
    (void)p_argument;
    /* 保持 Core 状态推进；USB 端点自身的 2ms 轮询不由 UART 定时替代。 */
    for (;;) {
        (void)Usb_host_Process();
        (void)osDelay(APP_USB_PROCESS_TICKS);
    }
}

void App_transmit_entry(void *p_argument)
{
    gamepad_stream_t stream = {0};
    (void)p_argument;
    for (;;) {
        uint32_t flags = osEventFlagsWait(s_resources.events,
                                         GAMEPAD_NOTIFY_WORK | GAMEPAD_NOTIFY_LINK_TICK,
                                         osFlagsWaitAny, osWaitForever);
        gamepad_message_t message;
        uint8_t sent = 0U;
        if ((flags & osFlagsError) != 0U) {
            g_v_app_diag.last_error = APP_RECEIVER_ERR_EVENT;
            (void)osDelay(1U);
            continue;
        }
        (void)Gamepad_stream_Sync(&stream, Usb_host_GetSession());
        /* WORK 是可合并通知；采样逐份保存在队列中并逐份发送。 */
        while (osMessageQueueGet(s_resources.queue, &message, NULL, 0U) == osOK) {
            uint32_t current = Usb_host_GetSession();
            (void)Gamepad_stream_Sync(&stream, current);
            if (message.event == GAMEPAD_EVENT_INPUT) {
                if (Gamepad_stream_Input(&stream, &message, current) == GAMEPAD_STREAM_ERR_OK) {
                    (void)_send_stream(&stream);
                    sent = 1U;
                } else {
                    g_v_app_diag.rejected++;
                }
            }
        }
        (void)Gamepad_stream_Sync(&stream, Usb_host_GetSession());
        g_v_app_diag.input_valid = stream.valid;
        /* 在线时节拍不重复旧值；无输入/失联才输出零值状态。 */
        if ((stream.valid == 0U) && (sent == 0U)) {
            (void)_send_stream(&stream);
        }
    }
}

app_receiver_err_t App_receiver_NotifyTick(void)
{
    if (s_resources.events == NULL) {
        return APP_RECEIVER_ERR_INIT;
    }
    /* 定时器服务任务不等待锁、USB 枚举或 DMA 完成。 */
    if ((osEventFlagsSet(s_resources.events, GAMEPAD_NOTIFY_LINK_TICK) & osFlagsError) != 0U) {
        return APP_RECEIVER_ERR_EVENT;
    }
    return APP_RECEIVER_ERR_OK;
}
