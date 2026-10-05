#ifndef VOFA_H
#define VOFA_H

#include "xbox.h"

#include <stddef.h>
#include <stdint.h>

#define VOFA_CHANNEL_COUNT 8U
#define VOFA_FRAME_SIZE 36U

/* JustFloat 帧打包错误。 */
typedef enum vofa_err_e {
    VOFA_ERR_OK = 0,
    VOFA_ERR_NULL = -1,
    VOFA_ERR_CAPACITY = -2
} vofa_err_t;

/* @brief 打包六个原始轴、按键位图、valid 以及 JustFloat 帧尾。
 * @param p_state 已解析的原始状态，不能为空。
 * @param c_valid 非零表示有效；无效时全部八个通道输出 0。
 * @param p_frame 目标字节缓冲区，不能为空。
 * @param c_capacity 目标容量，至少 36 字节。
 * @return 0 成功，负值失败。 */
vofa_err_t Vofa_Pack(const xbox_state_t *p_state, uint8_t c_valid,
                    uint8_t *p_frame, size_t c_capacity);

#endif
