#ifndef __PROJ_CFG_LVL2_H__
#define __PROJ_CFG_LVL2_H__

#define VSF_SYSTIMER_FREQ                   (CHIP_AHB_FREQ_HZ)

#if defined(BRD_CFG_VLLINKLITE_GD32E103)
#   define FIRMWARE_AREA_ADDR                           0x08003000
#   define FIRMWARE_AREA_SIZE_MAX                       (128 * 1024 - 12 * 1024)
#   define FIRMWARE_SP_ADDR			                    (0x20000000 + 4)
#   define FIRMWARE_SP_SIZE_MAX		                    (32 * 1024 - 4)
#   define PROJ_CFG_GD32E10X_HSI48M_USB_PLL_120M
#   define PROJ_CFG_CORE_INIT_TINY
#   if (VSF_USE_USB_DEVICE == ENABLED) || (VSF_USE_USB_HOST == ENABLED)
#       define VSF_DWCOTG_DCD_CFG_FAKE_EP               DISABLED
#       define USRAPP_CFG_USBD_SPEED                    USB_SPEED_FULL
#       define APP_CFG_USBD_SPEED                       USB_DC_SPEED_FULL
#   endif
#elif defined(BRD_CFG_VLLINKLITE_GD32F350)
#   define FIRMWARE_AREA_ADDR                           0x08003000
#   define FIRMWARE_AREA_SIZE_MAX                       (64 * 1024 - 12 * 1024)
#   define FIRMWARE_SP_ADDR			                    (0x20000000 + 4)
#   define FIRMWARE_SP_SIZE_MAX		                    (8 * 1024 - 4)
#   define PROJ_CFG_GD32F3X0_HSI48M_USB_PLL_108M
#   define PROJ_CFG_CORE_INIT_TINY
#   if (VSF_USE_USB_DEVICE == ENABLED) || (VSF_USE_USB_HOST == ENABLED)
#       define VSF_DWCOTG_DCD_CFG_FAKE_EP               DISABLED
#       define USRAPP_CFG_USBD_SPEED                    USB_SPEED_FULL
#       define APP_CFG_USBD_SPEED                       USB_DC_SPEED_FULL
#   endif
#endif

#endif // __PROJ_CFG_LVL2_H__
