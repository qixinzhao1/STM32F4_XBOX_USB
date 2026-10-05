#ifndef USB_HOST_H
#define USB_HOST_H

#include "cmsis_os2.h"
#include "gamepad_message.h"

#include <stdint.h>

/* BSP Host 接入错误；第三方 USBH ABI 的状态仅在 BSP 内部使用。 */
typedef enum usb_host_err_e {
    USB_HOST_ERR_OK = 0,
    USB_HOST_ERR_NULL = -1,
    USB_HOST_ERR_RESOURCE = -2,
    USB_HOST_ERR_HAL = -3,
    USB_HOST_ERR_DEVICE = -4,
    USB_HOST_ERR_PIPE = -5,
    USB_HOST_ERR_TRANSFER = -6,
    USB_HOST_ERR_QUEUE = -7
} usb_host_err_t;

/* 可在调试器读取的诊断；不向 JustFloat 串口插入日志。 */
typedef struct usb_host_diag_s {
    uint32_t session;           /* 当前连接代号，低位表示类就绪。 */
    uint32_t connects;          /* 根端口连接次数。 */
    uint32_t disconnects;       /* 根端口断开次数。 */
    uint32_t received;          /* 完成的非空原始 USB 传输次数。 */
    uint32_t queue_drops;       /* 生成队列满导致的丢弃次数。 */
    uint32_t transfer_errors;   /* 硬传输故障次数，不计普通 NAK。 */
    uint32_t host_state;        /* ST Host 状态机值，仅供诊断。 */
    uint32_t last_length;       /* 最新实际接收长度。 */
    int32_t last_error;         /* usb_host_err_t 对应错误。 */
    uint16_t vendor;            /* 实际设备 VID。 */
    uint16_t product;           /* 实际设备 PID。 */
    uint16_t max_packet;        /* 实际 IN 端点最大包长。 */
    uint16_t declared_power_ma; /* 实际配置声明电流，不是电流测量。 */
    uint8_t in_endpoint;        /* 枚举获得的 IN 端点。 */
    uint8_t out_endpoint;       /* 枚举获得的 OUT 端点。 */
    uint8_t interval_ms;        /* FS 中断 IN 的轮询间隔。 */
    uint8_t reserved;           /* 结构体对齐。 */
    uint8_t last_data[GAMEPAD_USB_PAYLOAD_SIZE]; /* 最新原始报告，用于联机诊断。 */
} usb_host_diag_t;

extern volatile usb_host_diag_t g_v_usb_host_diag;

/* @brief 绑定已有队列/事件并注册 HCD 回调，启动单设备 USB Host。
 * @param c_queue 生成层创建的 80 字节消息队列，不能为空。
 * @param c_events 生成层创建的事件对象，不能为空。
 * @return 0 成功，负值失败。 */
usb_host_err_t Usb_host_Init(osMessageQueueId_t c_queue, osEventFlagsId_t c_events);

/* @brief 在线程中推进枚举和接收，禁止从 ISR 或定时器回调调用。
 * @return 0 成功或正常等待，负值故障。 */
usb_host_err_t Usb_host_Process(void);

/* @brief 读取原子连接代号，用于拒绝排队中的旧报告。
 * @return 当前代号；低位为 0 时无有效 USB 类连接。 */
uint32_t Usb_host_GetSession(void);

#endif
