#include "usb_host.h"
#include "project_config.h"
#include "usb_otg.h"
#include "usbh_core.h"
#include "usbh_ctlreq.h"
#include "usbh_ioreq.h"
#include "usbh_pipes.h"
#include "xbox.h"

#include <string.h>

#define USB_HOST_PIPE_COUNT 8U
#define USB_HOST_PIPE_NONE 0xFFU
#define USB_HOST_URB_WAIT_MS 100U

/* 单实例接收上下文；只有 gamepad 线程推进状态机。 */
typedef struct xbox_host_s {
    uint8_t in_pipe;       /* 已分配的 IN 通道，0xFF 表示未分配。 */
    uint8_t out_pipe;      /* OUT 通道，仅保留传输能力，不发送震动。 */
    uint8_t in_endpoint;  /* 枚举获得的输入端点。 */
    uint8_t interval_ms;  /* 全速端点轮询间隔。 */
    uint16_t packet_size; /* 输入缓冲接收上限。 */
    uint8_t pending;      /* 一份 IN URB 正在等待完成。 */
    uint8_t clearing;     /* 正在线程中清除端点 STALL。 */
    uint32_t submitted;   /* 本份 URB 提交时的 SOF 毫秒计数。 */
    uint32_t session;     /* URB 对应连接，阻止迟到完成复活旧状态。 */
    uint8_t data[GAMEPAD_USB_PAYLOAD_SIZE]; /* FIFO 接收缓冲，非 USB DMA。 */
} xbox_host_t;

volatile usb_host_diag_t g_v_usb_host_diag;
static USBH_HandleTypeDef s_host;
static xbox_host_t s_xbox;
static osMessageQueueId_t s_queue;
static osEventFlagsId_t s_events;
static volatile uint32_t s_v_session;
static volatile uint32_t s_v_recover;
static uint8_t s_initialized;
static uint8_t s_abort_reported;
static uint8_t s_ll_ready;

/* @brief 校验此 BSP 唯一 Host 句柄。
 * @param p_host SDK 句柄，允许错误调用传入 NULL 后返回失败。
 * @return 0 表示已绑定控制器，负值表示空指针或实例错误。 */
static usb_host_err_t _host_validate(const USBH_HandleTypeDef *p_host)
{
    /* SDK 的固定 ABI 由适配层做空值及实例检查。 */
    if (p_host == NULL) {
        return USB_HOST_ERR_NULL;
    }
    if ((p_host != &s_host) || (p_host->pData != &hhcd_USB_OTG_FS)) {
        return USB_HOST_ERR_HAL;
    }
    return USB_HOST_ERR_OK;
}

/* @brief 发布原始消息，不解析报告，不阻塞等待队列空间。
 * @param p_message 已填充消息，不能为空。
 * @return 0 成功，负值失败。 */
static usb_host_err_t _publish(const gamepad_message_t *p_message)
{
    if (p_message == NULL) {
        return USB_HOST_ERR_NULL;
    }
    if ((s_queue == NULL) || (s_events == NULL)) {
        return USB_HOST_ERR_RESOURCE;
    }
    if (osMessageQueuePut(s_queue, p_message, 0U, 0U) != osOK) {
        uint32_t irq_mask = __get_PRIMASK();
        __disable_irq();
        /* 队列溢出切换代号；积压中的旧输入不能继续当成最新输入。 */
        s_v_session += 2U;
        g_v_usb_host_diag.session = s_v_session;
        g_v_usb_host_diag.queue_drops++;
        g_v_usb_host_diag.last_error = USB_HOST_ERR_QUEUE;
        __set_PRIMASK(irq_mask);
        (void)osEventFlagsSet(s_events, PROJECT_EVENT_WORK);
        return USB_HOST_ERR_QUEUE;
    }
    (void)osEventFlagsSet(s_events, PROJECT_EVENT_WORK);
    return USB_HOST_ERR_OK;
}

/* @brief 使连接失效；短临界段先更新原子代号，再发布状态通知。
 * @param c_error 故障原因，正常连接变化可以为 0。
 * @return 无；断开通知失败仍由当前代号阻止旧消息重放。 */
static void _invalidate(usb_host_err_t c_error)
{
    gamepad_message_t message = {0};
    uint32_t irq_mask = __get_PRIMASK();
    __disable_irq();
    s_v_session = (s_v_session + 2U) & ~UINT32_C(1);
    g_v_usb_host_diag.session = s_v_session;
    g_v_usb_host_diag.last_error = c_error;
    __set_PRIMASK(irq_mask);
    /* 若队列满，调用方仍能直接读取已更新的连接代号。 */
    message.event = GAMEPAD_EVENT_LINK;
    message.session = s_v_session;
    message.timestamp_ms = HAL_GetTick();
    (void)_publish(&message);
}

/* @brief 已配置且接口匹配后开启连接标志；尚未表示输入报告有效。
 * @return 无，输入有效性由 Mid 解析结果决定。 */
static void _activate(void)
{
    gamepad_message_t message = {0};
    uint32_t irq_mask = __get_PRIMASK();
    __disable_irq();
    s_v_session |= 1U;
    g_v_usb_host_diag.session = s_v_session;
    __set_PRIMASK(irq_mask);
    message.event = GAMEPAD_EVENT_LINK;
    message.session = s_v_session;
    message.timestamp_ms = HAL_GetTick();
    (void)_publish(&message);
}

/* @brief 暂停当前输入有效性，保留已配置 USB 类以完成 STALL 恢复。
 * @param c_error 错误原因。
 * @return 无；下一份新连接代号的有效报告才恢复输入。 */
static void _retire_input(usb_host_err_t c_error)
{
    gamepad_message_t message = {0};
    uint32_t irq_mask = __get_PRIMASK();
    __disable_irq();
    /* 保留低位的类就绪状态，让线程仍可完成端点 ClearFeature。 */
    s_v_session += 2U;
    g_v_usb_host_diag.session = s_v_session;
    g_v_usb_host_diag.last_error = c_error;
    __set_PRIMASK(irq_mask);
    message.event = GAMEPAD_EVENT_LINK;
    message.session = s_v_session;
    message.timestamp_ms = HAL_GetTick();
    (void)_publish(&message);
}

/* @brief SOF 只维护 Host 计时，不执行协议解析或串口发送。
 * @param p_hcd 已注册 HCD；为空或实例错误则忽略。
 * @return 无，HAL 固定回调 ABI。 */
static void _on_sof(HCD_HandleTypeDef *p_hcd)
{
    /* 这里只更新计时，接收和报告解析都留在线程中。 */
    if ((p_hcd != NULL) && (p_hcd == &hhcd_USB_OTG_FS) && (_host_validate(&s_host) == USB_HOST_ERR_OK)) {
        USBH_LL_IncTimer(&s_host);
    }
}

/* @brief 根端口连接通知。
 * @param p_hcd HCD 句柄，不能为空。
 * @return 无，HAL 固定回调 ABI。 */
static void _on_connect(HCD_HandleTypeDef *p_hcd)
{
    /* 进入枚举前先退休上一连接的输入。 */
    if ((p_hcd == NULL) || (p_hcd != &hhcd_USB_OTG_FS) || (_host_validate(&s_host) != USB_HOST_ERR_OK)) {
        return;
    }
    g_v_usb_host_diag.connects++;
    s_abort_reported = 0U;
    _invalidate(USB_HOST_ERR_OK);
    (void)USBH_LL_Connect(&s_host);
}

/* @brief 根端口断开通知，先失效再通知 Core。
 * @param p_hcd HCD 句柄，不能为空。
 * @return 无，HAL 固定回调 ABI。 */
static void _on_disconnect(HCD_HandleTypeDef *p_hcd)
{
    /* 立即更新代号，不依赖较低优先级线程先处理队列。 */
    if ((p_hcd == NULL) || (p_hcd != &hhcd_USB_OTG_FS) || (_host_validate(&s_host) != USB_HOST_ERR_OK)) {
        return;
    }
    g_v_usb_host_diag.disconnects++;
    _invalidate(USB_HOST_ERR_OK);
    (void)USBH_LL_Disconnect(&s_host);
}

/* @brief USB 端口使能通知。
 * @param p_hcd HCD 句柄，不能为空。
 * @return 无，HAL 固定回调 ABI。 */
static void _on_port_enabled(HCD_HandleTypeDef *p_hcd)
{
    /* 端口使能只推进枚举，不提前把输入标为有效。 */
    if ((p_hcd != NULL) && (p_hcd == &hhcd_USB_OTG_FS) && (_host_validate(&s_host) == USB_HOST_ERR_OK)) {
        USBH_LL_PortEnabled(&s_host);
    }
}

/* @brief USB 端口关闭通知；无有效类时不重复产生失效事件。
 * @param p_hcd HCD 句柄，不能为空。
 * @return 无，HAL 固定回调 ABI。 */
static void _on_port_disabled(HCD_HandleTypeDef *p_hcd)
{
    /* 根端口关闭后，旧输入不能在类回调中继续发布。 */
    if ((p_hcd == NULL) || (p_hcd != &hhcd_USB_OTG_FS) || (_host_validate(&s_host) != USB_HOST_ERR_OK)) {
        return;
    }
    if ((s_v_session & 1U) != 0U) {
        _invalidate(USB_HOST_ERR_OK);
    }
    USBH_LL_PortDisabled(&s_host);
}

/* @brief Core 用户事件只协调 BSP 状态，不调用 App 业务入口。
 * @param p_host Core 句柄，不能为空。
 * @param c_event Core 事件。
 * @return 无，SDK 固定回调 ABI。 */
static void _user_event(USBH_HandleTypeDef *p_host, uint8_t c_event)
{
    if ((_host_validate(p_host) != USB_HOST_ERR_OK)) {
        return;
    }
    /* 断线由 HCD 回调抢先失效；这里保留不可恢复错误诊断。 */
    if (c_event == HOST_USER_UNRECOVERED_ERROR) {
        _invalidate(USB_HOST_ERR_DEVICE);
    }
}

/* @brief 关闭并释放 Xbox 占用通道。
 * @param p_host Core 句柄，不能为空。
 * @return SDK 要求的正值 ABI 状态；项目接口不导出此类型。 */
static USBH_StatusTypeDef _class_deinit(USBH_HandleTypeDef *p_host)
{
    if ((_host_validate(p_host) != USB_HOST_ERR_OK)) {
        return USBH_FAIL;
    }
    /* 先取消接收，避免释放后的完成事件继续发布旧数据。 */
    s_xbox.pending = 0U;
    if (s_xbox.in_pipe < USB_HOST_PIPE_COUNT) {
        (void)USBH_ClosePipe(p_host, s_xbox.in_pipe);
        (void)USBH_FreePipe(p_host, s_xbox.in_pipe);
    }
    if (s_xbox.out_pipe < USB_HOST_PIPE_COUNT) {
        (void)USBH_ClosePipe(p_host, s_xbox.out_pipe);
        (void)USBH_FreePipe(p_host, s_xbox.out_pipe);
    }
    s_xbox.in_pipe = USB_HOST_PIPE_NONE;
    s_xbox.out_pipe = USB_HOST_PIPE_NONE;
    if ((s_v_session & 1U) != 0U) {
        _invalidate(USB_HOST_ERR_OK);
    }
    return USBH_OK;
}

/* @brief 按真实接口和端点配置 Xbox 类，不使用 HID Boot 请求。
 * @param p_host 已枚举的 Core 句柄，不能为空。
 * @return SDK 固定 ABI 状态。 */
static USBH_StatusTypeDef _class_init(USBH_HandleTypeDef *p_host)
{
    USBH_InterfaceDescTypeDef *p_interface;
    uint8_t directions = 0U;
    if ((_host_validate(p_host) != USB_HOST_ERR_OK)) {
        return USBH_FAIL;
    }
    memset(&s_xbox, 0, sizeof(s_xbox));
    s_xbox.in_pipe = USB_HOST_PIPE_NONE;
    s_xbox.out_pipe = USB_HOST_PIPE_NONE;
    g_v_usb_host_diag.vendor = p_host->device.DevDesc.idVendor;
    g_v_usb_host_diag.product = p_host->device.DevDesc.idProduct;
    g_v_usb_host_diag.declared_power_ma = (uint16_t)((uint16_t)p_host->device.CfgDesc.bMaxPower * 2U);
    p_interface = &p_host->device.CfgDesc.Itf_Desc[0];
    if ((p_host->device.DevDesc.idVendor != PROJECT_USB_VID) ||
        (p_host->device.DevDesc.idProduct != PROJECT_USB_PID) ||
        (p_host->device.speed != USBH_SPEED_FULL) ||
        (p_host->device.CfgDesc.bNumInterfaces != 1U) ||
        (p_interface->bInterfaceClass != 0xFFU) ||
        (p_interface->bInterfaceSubClass != 0x5DU) ||
        (p_interface->bInterfaceProtocol != 0x01U) ||
        (p_interface->bAlternateSetting != 0U) || (p_interface->bNumEndpoints != 2U)) {
        _invalidate(USB_HOST_ERR_DEVICE);
        return USBH_FAIL;
    }
    (void)USBH_SelectInterface(p_host, 0U);
    /* 分配前验证方向唯一，错误描述符不能覆盖已分配通道而造成泄漏。 */
    for (uint8_t i = 0U; i < 2U; ++i) {
        USBH_EpDescTypeDef *p_endpoint = &p_interface->Ep_Desc[i];
        uint8_t direction = ((p_endpoint->bEndpointAddress & 0x80U) != 0U) ? 1U : 2U;
        if ((directions & direction) != 0U) {
            _invalidate(USB_HOST_ERR_DEVICE);
            return USBH_FAIL;
        }
        if ((direction == 1U) && (p_endpoint->wMaxPacketSize < XBOX_INPUT_REPORT_SIZE)) {
            _invalidate(USB_HOST_ERR_DEVICE);
            return USBH_FAIL;
        }
        directions |= direction;
    }
    if (directions != 3U) {
        _invalidate(USB_HOST_ERR_DEVICE);
        return USBH_FAIL;
    }
    /* 数据端点必须来自实际描述符，不能把 Windows 的 IG_01 当 USB HID。 */
    for (uint8_t i = 0U; i < 2U; ++i) {
        USBH_EpDescTypeDef *p_endpoint = &p_interface->Ep_Desc[i];
        uint8_t pipe;
        if (((p_endpoint->bmAttributes & 3U) != USBH_EP_INTERRUPT) ||
            ((p_endpoint->bEndpointAddress & 0x0FU) == 0U) ||
            (p_endpoint->wMaxPacketSize == 0U) ||
            (p_endpoint->wMaxPacketSize > GAMEPAD_USB_PAYLOAD_SIZE) ||
            (p_endpoint->bInterval == 0U)) {
            (void)_class_deinit(p_host);
            _invalidate(USB_HOST_ERR_DEVICE);
            return USBH_FAIL;
        }
        pipe = USBH_AllocPipe(p_host, p_endpoint->bEndpointAddress);
        if (pipe >= USB_HOST_PIPE_COUNT) {
            (void)_class_deinit(p_host);
            _invalidate(USB_HOST_ERR_PIPE);
            return USBH_FAIL;
        }
        if (USBH_LL_OpenPipe(p_host, pipe, p_endpoint->bEndpointAddress,
                            p_host->device.address, p_host->device.speed,
                            USBH_EP_INTERRUPT, p_endpoint->wMaxPacketSize) != USBH_OK) {
            (void)USBH_FreePipe(p_host, pipe);
            (void)_class_deinit(p_host);
            _invalidate(USB_HOST_ERR_PIPE);
            return USBH_FAIL;
        }
        (void)USBH_LL_SetToggle(p_host, pipe, 0U);
        if ((p_endpoint->bEndpointAddress & 0x80U) != 0U) {
            s_xbox.in_pipe = pipe;
            s_xbox.in_endpoint = p_endpoint->bEndpointAddress;
            s_xbox.packet_size = p_endpoint->wMaxPacketSize;
            s_xbox.interval_ms = p_endpoint->bInterval;
            g_v_usb_host_diag.in_endpoint = p_endpoint->bEndpointAddress;
            g_v_usb_host_diag.max_packet = p_endpoint->wMaxPacketSize;
            g_v_usb_host_diag.interval_ms = p_endpoint->bInterval;
        } else {
            s_xbox.out_pipe = pipe;
            g_v_usb_host_diag.out_endpoint = p_endpoint->bEndpointAddress;
        }
    }
    if ((s_xbox.in_pipe >= USB_HOST_PIPE_COUNT) || (s_xbox.out_pipe >= USB_HOST_PIPE_COUNT)) {
        (void)_class_deinit(p_host);
        _invalidate(USB_HOST_ERR_PIPE);
        return USBH_FAIL;
    }
    p_host->pActiveClass->pData = &s_xbox;
    return USBH_OK;
}

/* @brief 进入类运行阶段；当前兼容接收器先使用 SET_CONFIGURATION 后接收。
 * @param p_host Core 句柄，不能为空。
 * @return SDK 固定 ABI 状态。 */
static USBH_StatusTypeDef _class_requests(USBH_HandleTypeDef *p_host)
{
    if ((_host_validate(p_host) != USB_HOST_ERR_OK)) {
        return USBH_FAIL;
    }
    /* 不发 SetProtocol/SetIdle、震动或未经验证的厂商初始化命令。 */
    s_xbox.submitted = p_host->Timer - s_xbox.interval_ms;
    _activate();
    return USBH_OK;
}

/* @brief 接收故障退出当前代号，请求在类回调外重新枚举。
 * @return 无，重启由线程的 Usb_host_Process 执行。 */
static void _request_recovery(void)
{
    g_v_usb_host_diag.transfer_errors++;
    _invalidate(USB_HOST_ERR_TRANSFER);
    s_xbox.pending = 0U;
    s_v_recover = 1U;
}

/* @brief 非阻塞轮询 IN URB，发布原始报告；NAK 不被当成无线断线。
 * @param p_host Core 句柄，不能为空。
 * @return SDK 固定 ABI 状态。 */
static USBH_StatusTypeDef _class_process(USBH_HandleTypeDef *p_host)
{
    if ((_host_validate(p_host) != USB_HOST_ERR_OK) || (s_xbox.in_pipe >= USB_HOST_PIPE_COUNT)) {
        return USBH_FAIL;
    }
    if ((p_host->device.is_connected == 0U) || ((s_v_session & 1U) == 0U)) {
        return USBH_BUSY;
    }
    if (s_xbox.clearing != 0U) {
        USBH_StatusTypeDef ret = USBH_ClrFeature(p_host, s_xbox.in_endpoint);
        if (ret == USBH_OK) {
            s_xbox.clearing = 0U;
            (void)USBH_LL_SetToggle(p_host, s_xbox.in_pipe, 0U);
        } else if (ret != USBH_BUSY) {
            _request_recovery();
        }
        return USBH_BUSY;
    }
    if (s_xbox.pending != 0U) {
        USBH_URBStateTypeDef state = USBH_LL_GetURBState(p_host, s_xbox.in_pipe);
        uint32_t elapsed = p_host->Timer - s_xbox.submitted;
        if (state == USBH_URB_DONE) {
            uint32_t length = USBH_LL_GetLastXferSize(p_host, s_xbox.in_pipe);
            s_xbox.pending = 0U;
            if ((length > 0U) && (length <= GAMEPAD_USB_PAYLOAD_SIZE) &&
                (s_xbox.session == s_v_session) && ((s_v_session & 1U) != 0U)) {
                gamepad_message_t message = {0};
                message.event = GAMEPAD_EVENT_INPUT;
                message.session = s_xbox.session;
                message.timestamp_ms = HAL_GetTick();
                message.length = length;
                memcpy(message.data, s_xbox.data, length);
                g_v_usb_host_diag.received++;
                g_v_usb_host_diag.last_length = length;
                for (uint32_t i = 0U; i < GAMEPAD_USB_PAYLOAD_SIZE; ++i) {
                    g_v_usb_host_diag.last_data[i] = message.data[i];
                }
                (void)_publish(&message);
            } else if (length > GAMEPAD_USB_PAYLOAD_SIZE) {
                _request_recovery();
            }
        } else if (state == USBH_URB_STALL) {
            s_xbox.pending = 0U;
            s_xbox.clearing = 1U;
            g_v_usb_host_diag.transfer_errors++;
            _retire_input(USB_HOST_ERR_TRANSFER);
        } else if (state == USBH_URB_ERROR) {
            _request_recovery();
        } else if ((state == USBH_URB_NOTREADY) || (state == USBH_URB_NAK_WAIT) ||
                   (state == USBH_URB_NYET)) {
            if (elapsed >= s_xbox.interval_ms) {
                s_xbox.pending = 0U;
            }
        } else if (elapsed > USB_HOST_URB_WAIT_MS) {
            _request_recovery();
        }
    }
    if ((s_xbox.pending == 0U) && (s_xbox.clearing == 0U) && (s_v_recover == 0U) &&
        ((p_host->Timer - s_xbox.submitted) >= s_xbox.interval_ms)) {
        /* 64 字节是接收上限；有效 20 字节输入由 Mid 在任务中解析。 */
        s_xbox.session = s_v_session;
        if (USBH_InterruptReceiveData(p_host, s_xbox.data,
                                     (uint8_t)s_xbox.packet_size, s_xbox.in_pipe) != USBH_OK) {
            _request_recovery();
        } else {
            s_xbox.pending = 1U;
            s_xbox.submitted = p_host->Timer;
        }
    }
    return USBH_BUSY;
}

/* @brief 类 SOF 回调不操作接收状态；状态推进只由工作线程执行。
 * @param p_host Core 句柄，不能为空。
 * @return SDK 固定 ABI 状态。 */
static USBH_StatusTypeDef _class_sof(USBH_HandleTypeDef *p_host)
{
    /* Core 在 HCD ISR 中调用这里，禁止解析/排队业务或等待。 */
    return (_host_validate(p_host) == USB_HOST_ERR_OK) ? USBH_OK : USBH_FAIL;
}

static USBH_ClassTypeDef s_xbox_class = {
    "Xbox360", 0xFFU, _class_init, _class_deinit,
    _class_requests, _class_process, _class_sof, NULL
};

usb_host_err_t Usb_host_Init(osMessageQueueId_t c_queue, osEventFlagsId_t c_events)
{
    if ((c_queue == NULL) || (c_events == NULL)) {
        return USB_HOST_ERR_NULL;
    }
    if (osMessageQueueGetMsgSize(c_queue) != sizeof(gamepad_message_t)) {
        return USB_HOST_ERR_RESOURCE;
    }
    s_queue = c_queue;
    s_events = c_events;
    s_xbox.in_pipe = USB_HOST_PIPE_NONE;
    s_xbox.out_pipe = USB_HOST_PIPE_NONE;
    /* 使用生成的 HCD 句柄，Core 的 OS 模式关闭，不重复创建对象。 */
    if ((USBH_Init(&s_host, _user_event, 0U) != USBH_OK) || (s_ll_ready == 0U) ||
        (USBH_RegisterClass(&s_host, &s_xbox_class) != USBH_OK) ||
        (USBH_Start(&s_host) != USBH_OK)) {
        return USB_HOST_ERR_HAL;
    }
    s_initialized = 1U;
    return USB_HOST_ERR_OK;
}

usb_host_err_t Usb_host_Process(void)
{
    if (s_initialized == 0U) {
        return USB_HOST_ERR_RESOURCE;
    }
    /* Host Core 的延时不会阻塞优先级更高的发送任务。 */
    (void)USBH_Process(&s_host);
    g_v_usb_host_diag.host_state = (uint32_t)s_host.gState;
    if ((s_host.gState == HOST_ABORT_STATE) && (s_abort_reported == 0U)) {
        s_abort_reported = 1U;
        _invalidate(USB_HOST_ERR_DEVICE);
    }
    if (s_v_recover != 0U) {
        s_v_recover = 0U;
        (void)USBH_ReEnumerate(&s_host);
        return USB_HOST_ERR_TRANSFER;
    }
    return USB_HOST_ERR_OK;
}

uint32_t Usb_host_GetSession(void)
{
    /* 32 位对齐读取，不依赖排队状态事件的处理时机。 */
    return s_v_session;
}

/* 下列函数保留 ST Host Core 固定 ABI，只在 BSP 内封装 HAL。 */
USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef *p_host)
{
    s_ll_ready = 0U;
    if ((p_host == NULL) || (p_host != &s_host)) {
        return USBH_FAIL;
    }
    p_host->pData = &hhcd_USB_OTG_FS;
    hhcd_USB_OTG_FS.pData = p_host;
    /* 不覆盖 PA9/PA10，不重跑生成的 HCD/USART 初始化。 */
    if ((HAL_HCD_RegisterCallback(&hhcd_USB_OTG_FS, HAL_HCD_SOF_CB_ID, _on_sof) != HAL_OK) ||
        (HAL_HCD_RegisterCallback(&hhcd_USB_OTG_FS, HAL_HCD_CONNECT_CB_ID, _on_connect) != HAL_OK) ||
        (HAL_HCD_RegisterCallback(&hhcd_USB_OTG_FS, HAL_HCD_DISCONNECT_CB_ID, _on_disconnect) != HAL_OK) ||
        (HAL_HCD_RegisterCallback(&hhcd_USB_OTG_FS, HAL_HCD_PORT_ENABLED_CB_ID, _on_port_enabled) != HAL_OK) ||
        (HAL_HCD_RegisterCallback(&hhcd_USB_OTG_FS, HAL_HCD_PORT_DISABLED_CB_ID, _on_port_disabled) != HAL_OK)) {
        return USBH_FAIL;
    }
    USBH_LL_SetTimer(p_host, HAL_HCD_GetCurrentFrame(&hhcd_USB_OTG_FS));
    s_ll_ready = 1U;
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef *p_host)
{
    /* 停止 BSP 使用，不反初始化生成层拥有的硬件配置。 */
    return ((_host_validate(p_host) == USB_HOST_ERR_OK) && (HAL_HCD_Stop(&hhcd_USB_OTG_FS) == HAL_OK)) ? USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef *p_host)
{
    /* 生成的控制器已经初始化；这里仅打开运行所需的硬件中断。 */
    return ((_host_validate(p_host) == USB_HOST_ERR_OK) && (HAL_HCD_Start(&hhcd_USB_OTG_FS) == HAL_OK)) ? USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef *p_host)
{
    /* 保留 GPIO/时钟配置，让 Core 可以在断线后重新启动。 */
    return ((_host_validate(p_host) == USB_HOST_ERR_OK) && (HAL_HCD_Stop(&hhcd_USB_OTG_FS) == HAL_OK)) ? USBH_OK : USBH_FAIL;
}

USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef *p_host)
{
    if ((_host_validate(p_host) != USB_HOST_ERR_OK)) {
        return USBH_SPEED_FULL;
    }
    /* HAL 与 USBH 的速度枚举均为 High=0、Full=1、Low=2。 */
    return (USBH_SpeedTypeDef)HAL_HCD_GetCurrentSpeed(&hhcd_USB_OTG_FS);
}

USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef *p_host)
{
    /* 根端口复位仅在 Host 枚举线程中执行。 */
    return ((_host_validate(p_host) == USB_HOST_ERR_OK) && (HAL_HCD_ResetPort(&hhcd_USB_OTG_FS) == HAL_OK)) ? USBH_OK : USBH_FAIL;
}

uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *p_host, uint8_t pipe)
{
    /* 使用真实完成长度，不能把配置的最大包长视为报告长度。 */
    if ((_host_validate(p_host) != USB_HOST_ERR_OK) || (pipe >= USB_HOST_PIPE_COUNT)) {
        return 0U;
    }
    return HAL_HCD_HC_GetXferCount(&hhcd_USB_OTG_FS, pipe);
}

USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef *p_host, uint8_t pipe,
                                   uint8_t epnum, uint8_t address, uint8_t speed,
                                   uint8_t ep_type, uint16_t packet_size)
{
    if ((_host_validate(p_host) != USB_HOST_ERR_OK) || (pipe >= USB_HOST_PIPE_COUNT)) {
        return USBH_FAIL;
    }
    /* 端点、设备地址和包长由枚举层提供，不使用硬编码通道 0。 */
    return (HAL_HCD_HC_Init(&hhcd_USB_OTG_FS, pipe, epnum, address,
                           speed, ep_type, packet_size) == HAL_OK) ? USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef *p_host, uint8_t pipe)
{
    /* 关闭有限的硬件 Host channel，再由 Core 释放其逻辑槽。 */
    if ((_host_validate(p_host) != USB_HOST_ERR_OK) || (pipe >= USB_HOST_PIPE_COUNT)) {
        return USBH_FAIL;
    }
    return (HAL_HCD_HC_Halt(&hhcd_USB_OTG_FS, pipe) == HAL_OK) ? USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef *p_host, uint8_t pipe,
                                    uint8_t direction, uint8_t ep_type, uint8_t token,
                                    uint8_t *p_buffer, uint16_t length, uint8_t do_ping)
{
    if ((_host_validate(p_host) != USB_HOST_ERR_OK) || (pipe >= USB_HOST_PIPE_COUNT) ||
        ((p_buffer == NULL) && (length != 0U))) {
        return USBH_FAIL;
    }
    /* USB 的零长度状态阶段允许空缓冲区，遵循 SDK ABI。 */
    return (HAL_HCD_HC_SubmitRequest(&hhcd_USB_OTG_FS, pipe, direction, ep_type,
                                    token, p_buffer, length, do_ping) == HAL_OK) ? USBH_OK : USBH_FAIL;
}

USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *p_host, uint8_t pipe)
{
    /* 读取 IRQ 更新的完成状态，不在这里等待或解析报告。 */
    if ((_host_validate(p_host) != USB_HOST_ERR_OK) || (pipe >= USB_HOST_PIPE_COUNT)) {
        return USBH_URB_ERROR;
    }
    return (USBH_URBStateTypeDef)HAL_HCD_HC_GetURBState(&hhcd_USB_OTG_FS, pipe);
}

USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *p_host, uint8_t state)
{
    if ((_host_validate(p_host) != USB_HOST_ERR_OK)) {
        return USBH_FAIL;
    }
    /* 原板没有 GPIO VBUS 开关；受保护的外部 5V 支路必须已供电。 */
    (void)state;
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *p_host, uint8_t pipe, uint8_t toggle)
{
    /* 分别维护 IN/OUT 的 DATA0/DATA1，STALL 清除后从 DATA0 恢复。 */
    if ((_host_validate(p_host) != USB_HOST_ERR_OK) || (pipe >= USB_HOST_PIPE_COUNT)) {
        return USBH_FAIL;
    }
    if (hhcd_USB_OTG_FS.hc[pipe].ep_is_in != 0U) {
        hhcd_USB_OTG_FS.hc[pipe].toggle_in = toggle;
    } else {
        hhcd_USB_OTG_FS.hc[pipe].toggle_out = toggle;
    }
    return USBH_OK;
}

uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef *p_host, uint8_t pipe)
{
    /* 获取当前方向的 toggle；非法通道不能访问 hc 数组。 */
    if ((_host_validate(p_host) != USB_HOST_ERR_OK) || (pipe >= USB_HOST_PIPE_COUNT)) {
        return 0U;
    }
    return (hhcd_USB_OTG_FS.hc[pipe].ep_is_in != 0U) ?
           hhcd_USB_OTG_FS.hc[pipe].toggle_in : hhcd_USB_OTG_FS.hc[pipe].toggle_out;
}

void USBH_Delay(uint32_t delay_ms)
{
    /* 已运行 RTOS 后让出 CPU；初始化早期才使用 HAL 时基。 */
    if (delay_ms == 0U) {
        return;
    }
    if (osKernelGetState() == osKernelRunning) {
        (void)osDelay(delay_ms);
    } else {
        HAL_Delay(delay_ms);
    }
}
