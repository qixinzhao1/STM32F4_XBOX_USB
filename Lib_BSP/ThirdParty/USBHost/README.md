# STM32Cube USB Host Core

来源：本机 `STM32Cube_FW_F4_V1.28.3/Middlewares/ST/STM32_USB_Host_Library/Core`。

复制四个 Core `.c` 和五个 `.h` 原文件，保留 ST 原始版权及附带许可/发布说明。未复制默认HID类、示例BSP或RTOS对象创建适配。

配置和低层适配位于项目自定义 `Lib_BSP/Inc/usbh_conf.h`、`Lib_BSP/Src/usb_host.c`；`USBH_USE_OS=0`，由生成的应用任务推进。第三方源码不按项目命名规范重写。
