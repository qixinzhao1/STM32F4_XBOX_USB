#ifndef GAMEPAD_STREAM_H
#define GAMEPAD_STREAM_H

#include "gamepad_message.h"
#include "xbox.h"

/* 数据流错误；连接代号失配不允许重放旧采样。 */
typedef enum gamepad_stream_err_e {
    GAMEPAD_STREAM_ERR_OK = 0,
    GAMEPAD_STREAM_ERR_NULL = -1,
    GAMEPAD_STREAM_ERR_SESSION = -2,
    GAMEPAD_STREAM_ERR_REPORT = -3
} gamepad_stream_err_t;

/* 协议层输入快照；连接代号由调用者传入，不依赖具体硬件或 OS。 */
typedef struct gamepad_stream_s {
    xbox_state_t state; /* 最新完整输入，失效后清零。 */
    uint32_t session;  /* 与输入配对的连接代号。 */
    uint8_t valid;     /* 只有当前连接的新报告可以置 1。 */
} gamepad_stream_t;

/* @brief 初始化/同步当前连接；连接变化后使输入无效并清零。
 * @param p_stream 流对象，不能为空。
 * @param c_session 当前连接代号，最低位表示类已就绪。
 * @return 0 成功，负值失败。 */
gamepad_stream_err_t Gamepad_stream_Sync(gamepad_stream_t *p_stream, uint32_t c_session);

/* @brief 接受当前连接的一份有效输入；错误报告不覆盖当前正确状态。
 * @param p_stream 流对象，不能为空。
 * @param p_message 原始消息，不能为空。
 * @param c_current_session 当前连接代号，不能使用消息自身代号代替。
 * @return 0 表示可发送新采样，负值表示应丢弃。 */
gamepad_stream_err_t Gamepad_stream_Input(gamepad_stream_t *p_stream,
                                        const gamepad_message_t *p_message,
                                        uint32_t c_current_session);

#endif
