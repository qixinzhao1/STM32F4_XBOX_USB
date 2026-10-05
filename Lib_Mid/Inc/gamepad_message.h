#ifndef GAMEPAD_MESSAGE_H
#define GAMEPAD_MESSAGE_H

#include <stdint.h>

#define GAMEPAD_USB_PAYLOAD_SIZE 64U

/* USB 原始数据与连接事件；事件不代表已经通过 Xbox 输入报告解析。 */
typedef enum gamepad_event_e {
    GAMEPAD_EVENT_INPUT = 1,
    GAMEPAD_EVENT_LINK = 2
} gamepad_event_t;

/* 四个元数据字段加 64 字节原始载荷，与生成队列的 80 字节元素对应。 */
typedef struct gamepad_message_s {
    uint32_t event;                    /* gamepad_event_t 对应的事件编号。 */
    uint32_t session;                  /* 连接代号；最低位表示 USB 类已就绪。 */
    uint32_t timestamp_ms;             /* Host 接收或连接变化时间。 */
    uint32_t length;                   /* 有效载荷长度，不超过 64 字节。 */
    uint8_t data[GAMEPAD_USB_PAYLOAD_SIZE]; /* 原始报告，未使用尾部清零。 */
} gamepad_message_t;

_Static_assert(sizeof(gamepad_message_t) == 80U, "CubeMX queue item must be 80 bytes");

#endif
