#include "xbox.h"

#include <string.h>

/* @brief 读取小端有符号 16 位值，避免未对齐访问和实现相关的截断。
 * @param p_data 两字节输入；内部调用前已经验证整份报告。
 * @param p_value 输出值，不能为空。
 * @return 0 成功，负值失败。 */
static xbox_err_t _read_i16_le(const uint8_t *p_data, int16_t *p_value)
{
    uint16_t raw;
    int32_t value;
    if ((p_data == NULL) || (p_value == NULL)) {
        return XBOX_ERR_NULL;
    }
    /* 用较宽类型显式转换补码，兼容纯 C 主机测试。 */
    raw = (uint16_t)((uint16_t)p_data[0] | ((uint16_t)p_data[1] << 8U));
    value = (raw <= UINT16_C(32767)) ? (int32_t)raw : (int32_t)raw - INT32_C(65536);
    *p_value = (int16_t)value;
    return XBOX_ERR_OK;
}

xbox_err_t Xbox_Reset(xbox_state_t *p_state)
{
    if (p_state == NULL) {
        return XBOX_ERR_NULL;
    }
    /* 无效状态不携带上一次摇杆或按键值。 */
    memset(p_state, 0, sizeof(*p_state));
    return XBOX_ERR_OK;
}

xbox_err_t Xbox_Parse(const uint8_t *p_data, size_t c_length, xbox_state_t *p_state)
{
    xbox_state_t state = {0};
    if ((p_data == NULL) || (p_state == NULL)) {
        return XBOX_ERR_NULL;
    }
    if ((c_length < XBOX_INPUT_REPORT_SIZE) || (c_length > XBOX_INPUT_TRANSFER_MAX)) {
        return XBOX_ERR_LENGTH;
    }
    if ((p_data[0] != 0U) || (p_data[1] != XBOX_INPUT_REPORT_SIZE)) {
        return XBOX_ERR_REPORT;
    }
    /* 头部声明 20B 标准区；32B 实际传输的后段为不解析的厂商扩展。 */
    state.buttons = (uint16_t)((uint16_t)p_data[2] | ((uint16_t)p_data[3] << 8U));
    state.left_trigger = p_data[4];
    state.right_trigger = p_data[5];
    if ((_read_i16_le(&p_data[6], &state.left_x) != XBOX_ERR_OK) ||
        (_read_i16_le(&p_data[8], &state.left_y) != XBOX_ERR_OK) ||
        (_read_i16_le(&p_data[10], &state.right_x) != XBOX_ERR_OK) ||
        (_read_i16_le(&p_data[12], &state.right_y) != XBOX_ERR_OK)) {
        return XBOX_ERR_NULL;
    }
    *p_state = state;
    return XBOX_ERR_OK;
}
