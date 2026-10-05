#include "gamepad_stream.h"

gamepad_stream_err_t Gamepad_stream_Sync(gamepad_stream_t *p_stream, uint32_t c_session)
{
    if (p_stream == NULL) {
        return GAMEPAD_STREAM_ERR_NULL;
    }
    /* 新连接必须等待新报告，不能沿用上一连接的有效标志。 */
    if ((p_stream->session != c_session) || ((c_session & 1U) == 0U)) {
        (void)Xbox_Reset(&p_stream->state);
        p_stream->valid = 0U;
    }
    p_stream->session = c_session;
    return GAMEPAD_STREAM_ERR_OK;
}

gamepad_stream_err_t Gamepad_stream_Input(gamepad_stream_t *p_stream,
                                        const gamepad_message_t *p_message,
                                        uint32_t c_current_session)
{
    if ((p_stream == NULL) || (p_message == NULL)) {
        return GAMEPAD_STREAM_ERR_NULL;
    }
    /* 即使断线事件在队列中排得较后，当前代号也能拒绝旧报告。 */
    (void)Gamepad_stream_Sync(p_stream, c_current_session);
    if (((c_current_session & 1U) == 0U) || (p_message->session != c_current_session)) {
        return GAMEPAD_STREAM_ERR_SESSION;
    }
    if ((p_message->event != GAMEPAD_EVENT_INPUT) ||
        (p_message->length > GAMEPAD_USB_PAYLOAD_SIZE) ||
        (Xbox_Parse(p_message->data, p_message->length, &p_stream->state) != XBOX_ERR_OK)) {
        return GAMEPAD_STREAM_ERR_REPORT;
    }
    p_stream->valid = 1U;
    return GAMEPAD_STREAM_ERR_OK;
}
