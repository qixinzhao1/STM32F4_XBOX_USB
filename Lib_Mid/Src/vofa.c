#include "vofa.h"

#include <float.h>
#include <string.h>

_Static_assert(sizeof(float) == 4U, "JustFloat requires float32");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128, "JustFloat requires IEEE binary32");

/* @brief 按明确的小端字节序输出一个 float32。
 * @param c_value 输入值。
 * @param p_data 四字节目标；内部调用前已检查帧容量。
 * @return 0 成功，负值失败。 */
static vofa_err_t _write_float_le(float c_value, uint8_t *p_data)
{
    uint32_t bits;
    if (p_data == NULL) {
        return VOFA_ERR_NULL;
    }
    /* memcpy 避免指针强转违反严格别名规则。 */
    memcpy(&bits, &c_value, sizeof(bits));
    p_data[0] = (uint8_t)bits;
    p_data[1] = (uint8_t)(bits >> 8U);
    p_data[2] = (uint8_t)(bits >> 16U);
    p_data[3] = (uint8_t)(bits >> 24U);
    return VOFA_ERR_OK;
}

vofa_err_t Vofa_Pack(const xbox_state_t *p_state, uint8_t c_valid,
                    uint8_t *p_frame, size_t c_capacity)
{
    float channels[VOFA_CHANNEL_COUNT] = {0.0F};
    if ((p_state == NULL) || (p_frame == NULL)) {
        return VOFA_ERR_NULL;
    }
    if (c_capacity < VOFA_FRAME_SIZE) {
        return VOFA_ERR_CAPACITY;
    }
    if (c_valid != 0U) {
        channels[0] = (float)p_state->left_x;
        channels[1] = (float)p_state->left_y;
        channels[2] = (float)p_state->right_x;
        channels[3] = (float)p_state->right_y;
        channels[4] = (float)p_state->left_trigger;
        channels[5] = (float)p_state->right_trigger;
        channels[6] = (float)p_state->buttons;
        channels[7] = 1.0F;
    }
    /* 无效帧从零初始化数组生成，避免泄露断线前状态。 */
    for (size_t i = 0U; i < VOFA_CHANNEL_COUNT; ++i) {
        if (_write_float_le(channels[i], &p_frame[4U * i]) != VOFA_ERR_OK) {
            return VOFA_ERR_NULL;
        }
    }
    p_frame[32] = 0U;
    p_frame[33] = 0U;
    p_frame[34] = 0x80U;
    p_frame[35] = 0x7FU;
    return VOFA_ERR_OK;
}
