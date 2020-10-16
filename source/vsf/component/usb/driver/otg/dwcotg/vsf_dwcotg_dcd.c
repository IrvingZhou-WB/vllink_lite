/*****************************************************************************
 *   Copyright(C)2009-2019 by VSF Team                                       *
 *                                                                           *
 *  Licensed under the Apache License, Version 2.0 (the "License");          *
 *  you may not use this file except in compliance with the License.         *
 *  You may obtain a copy of the License at                                  *
 *                                                                           *
 *     http://www.apache.org/licenses/LICENSE-2.0                            *
 *                                                                           *
 *  Unless required by applicable law or agreed to in writing, software      *
 *  distributed under the License is distributed on an "AS IS" BASIS,        *
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. *
 *  See the License for the specific language governing permissions and      *
 *  limitations under the License.                                           *
 *                                                                           *
 ****************************************************************************/

/*============================ INCLUDES ======================================*/

#include "component/usb/vsf_usb_cfg.h"

#if VSF_USE_USB_DEVICE == ENABLED && VSF_USE_USB_DEVICE_DCD_DWCOTG == ENABLED

#define __VSF_DWCOTG_DCD_CLASS_IMPLEMENT
#include "./vsf_dwcotg_dcd.h"

/*============================ MACROS ========================================*/

//#define DWCOTG_DEBUG
//#define DWCOTG_DEBUG_DUMP_USB_ON
//#define DWCOTG_DEBUG_DUMP_FUNC_CALL
//#define DWCOTG_DEBUG_DUMP_DATA

/*============================ MACROFIED FUNCTIONS ===========================*/
/*============================ TYPES =========================================*/

enum __usb_evt_t {
    ______________,

    __USB_ON_START,
        __USB_ON_SETUP,
        __USB_ON_MMIS,
        __USB_ON_IN,
        __USB_ON_OUT,
        __USB_ON_STATUS,
        __USB_ON_ENUMDNE,
        __USB_ON_USBSUSP,
        __USB_ON_WKUINTP,
        __USB_ON_SOF,
        __USB_ON_RST,
        __USB_ON_IRQ,

        __USB_ON_IEPINT,
        __USB_ON_DIEPINT_INEPNE,
        __USB_ON_DIEPINT_TXFE,

        __USB_ON_OEPINT,
        __USB_ON_DOEPINT_B2BSTUPP,

        __USB_ON_RXFLVL,
        __USB_ON_RXSTAT_SETUP_UPDT,
        __USB_ON_RXSTAT_DATA_UPDT,

        __USB_ON_ISR_IN_TRANSFER,
        __USB_ON_ISR_OUT_TRANSFER,
    __USB_ON_END,

    __FUNC_START,
        __FUNC_RESET,
        __FUNC_INIT,
        __FUNC_CONNECT,
        __FUNC_DISCONNECT,
        __FUNC_STATUS_STAGE,
        __FUNC_EP_ADD,
        __FUNC_EP_WRITE,
        __FUNC_EP_READ,
        __FUNC_EP_OUT_TRANSFER,
        __FUNC_EP_IN_TRANSFER,
        __FUNC_EP_TRANSFER_SEND,
        __FUNC_EP_TRANSFER_RECV,

        __FUNC_EP_WRITE_ENABLE_FIFO_EMPTY,
        __FUNC_EP_WRITE_DISABLE_FIFO_EMPTY,
    __FUNC_END,

};

/*============================ PROTOTYPES ====================================*/

extern uint_fast16_t vsf_dwcotg_dcd_get_fifo_size(uint_fast8_t ep, usb_ep_type_t type, uint_fast16_t size);

/*============================ GLOBAL VARIABLES ==============================*/

#ifdef DWCOTG_DEBUG
ROOT enum __usb_evt_t evt_buf[1024 * 2];
uint16_t evt_index = 0;
#endif

/*============================ LOCAL VARIABLES ===============================*/
/*============================ IMPLEMENTATION ================================*/

#ifdef DWCOTG_DEBUG
static void debug_add(uint32_t data)
{
    if (evt_index < dimof(evt_buf)) {
        evt_buf[evt_index++] = (enum __usb_evt_t)data;
    }
}
#else
#   define debug_add(data)
#endif

#ifdef DWCOTG_DEBUG_DUMP_DATA
static void debug_add_data(uint32_t data)
{
    debug_add(data);
}
#else
#   define debug_add_data(data)
#endif

#ifdef DWCOTG_DEBUG_DUMP_DATA
static void debug_add_evt(uint32_t evt)
{
#ifndef DWCOTG_DEBUG_DUMP_USB_ON
    if (evt < __USB_ON_END) return;
#endif

#ifndef DWCOTG_DEBUG_DUMP_FUNC_CALL
    if (evt > __USB_ON_END) return;
#endif

    debug_add_data(evt);
}
#else
#   define debug_add_evt(evt)
#endif

#ifndef WEAK_VSF_DWCOTG_DCD_GET_FIFO_SIZE
WEAK(vsf_dwcotg_dcd_get_fifo_size)
uint_fast16_t vsf_dwcotg_dcd_get_fifo_size(uint_fast8_t ep, usb_ep_type_t type, uint_fast16_t size)
{
    return size;
}
#endif

static void __vk_dwcotg_dcd_init_regs(vk_dwcotg_dcd_t *dwcotg_dcd, void *regbase, uint_fast8_t ep_num)
{
    dwcotg_dcd->reg.global_regs = regbase;
    dwcotg_dcd->reg.dev.global_regs = (void *)((uint8_t *)regbase + 0x800);
    dwcotg_dcd->reg.dev.ep.in_regs = (void *)((uint8_t *)regbase + 0x900);
    dwcotg_dcd->reg.dev.ep.out_regs = (void *)((uint8_t *)regbase + 0xB00);

    for (uint_fast8_t i = 0; i < ep_num; i++) {
        dwcotg_dcd->reg.dfifo[i] = (void *)((uint8_t *)regbase + (i + 1) * 0x1000);
    }
}

static void __vk_dwcotg_dcd_flush_txfifo(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t fifo_num)
{
    dwcotg_dcd->reg.global_regs->grstctl = (fifo_num << 6U) | USB_OTG_GRSTCTL_TXFFLSH;
    while (dwcotg_dcd->reg.global_regs->grstctl & USB_OTG_GRSTCTL_TXFFLSH);
}

static void __vk_dwcotg_dcd_flush_rxfifo(vk_dwcotg_dcd_t *dwcotg_dcd)
{
    dwcotg_dcd->reg.global_regs->grstctl = USB_OTG_GRSTCTL_RXFFLSH;
    while (dwcotg_dcd->reg.global_regs->grstctl & USB_OTG_GRSTCTL_RXFFLSH);
}

vsf_err_t vk_dwcotg_dcd_init(vk_dwcotg_dcd_t *dwcotg_dcd, usb_dc_cfg_t *cfg)
{
    VSF_USB_ASSERT((dwcotg_dcd != NULL) && (cfg != NULL));
    VSF_USB_ASSERT((dwcotg_dcd->param != NULL) && (dwcotg_dcd->param->op != NULL));

    debug_add_evt(__FUNC_INIT);

    const vk_dwcotg_dcd_param_t *param = dwcotg_dcd->param;
    struct dwcotg_core_global_regs_t *global_regs;
    struct dwcotg_dev_global_regs_t *dev_global_regs;
    vk_dwcotg_dc_ip_info_t info;
    param->op->GetInfo(&info.use_as__usb_dc_ip_info_t);

    __vk_dwcotg_dcd_init_regs(dwcotg_dcd, info.regbase, info.ep_num);
    global_regs = dwcotg_dcd->reg.global_regs;
    dev_global_regs = dwcotg_dcd->reg.dev.global_regs;

    dwcotg_dcd->callback.evt_handler = cfg->evt_handler;
    dwcotg_dcd->callback.param = cfg->param;

    {
        usb_dc_ip_cfg_t ip_cfg = {
            .priority       = cfg->priority,
            .irq_handler    = (usb_ip_irq_handler_t)vk_dwcotg_dcd_irq,
            .param          = dwcotg_dcd,
        };
        dwcotg_dcd->param->op->Init(&ip_cfg);
    }

    vk_dwcotg_phy_init(&dwcotg_dcd->use_as__vk_dwcotg_t,
                        &param->use_as__vk_dwcotg_param_t,
                        &info.use_as__vk_dwcotg_hw_info_t);

    global_regs->gahbcfg |= USB_OTG_GAHBCFG_TXFELVL;
    if (dwcotg_dcd->dma_en) {
        global_regs->gahbcfg |= USB_OTG_GAHBCFG_HBSTLEN_0 | USB_OTG_GAHBCFG_DMAEN;
    }

    // set device mode
    global_regs->gusbcfg &= ~USB_OTG_GUSBCFG_FHMOD;
    global_regs->gusbcfg |= USB_OTG_GUSBCFG_FDMOD;

    // config 80% periodic frame interval to default
    if (param->speed == USB_DC_SPEED_HIGH) {
        dev_global_regs->dcfg = USB_OTG_DCFG_NZLSOHSK;
    } else if (param->ulpi_en || param->utmi_en) {
        dev_global_regs->dcfg = USB_OTG_DCFG_NZLSOHSK | USB_OTG_DCFG_DSPD_0;
    } else {
        // set full speed PHY
        dev_global_regs->dcfg = USB_OTG_DCFG_NZLSOHSK | USB_OTG_DCFG_DSPD_0 | USB_OTG_DCFG_DSPD_1;
    }

    // disconnect
    dev_global_regs->dctl |= USB_OTG_DCTL_SDIS;

    dev_global_regs->doepmsk = USB_OTG_DOEPMSK_XFRCM | USB_OTG_DOEPMSK_STUPM;
    dev_global_regs->diepmsk = USB_OTG_DIEPMSK_XFRCM;
    dev_global_regs->daint = 0xffffffff;
    dev_global_regs->daintmsk = 0;

    global_regs->gintsts = 0xbfffffff;
    global_regs->gotgint = 0xffffffff;

    global_regs->gintmsk = USB_OTG_GINTMSK_USBRST | USB_OTG_GINTMSK_ENUMDNEM |
            USB_OTG_GINTMSK_IEPINT | USB_OTG_GINTMSK_OEPINT |
            USB_OTG_GINTMSK_IISOIXFRM | USB_OTG_GINTMSK_PXFRM_IISOOXFRM |
            USB_OTG_GINTMSK_RXFLVLM;

    global_regs->gahbcfg |= USB_OTG_GAHBCFG_GINT;
    return VSF_ERR_NONE;
}

void vk_dwcotg_dcd_fini(vk_dwcotg_dcd_t *dwcotg_dcd)
{
    dwcotg_dcd->param->op->Fini();
}

void vk_dwcotg_dcd_reset(vk_dwcotg_dcd_t *dwcotg_dcd, usb_dc_cfg_t *cfg)
{
    debug_add_evt(__FUNC_RESET);

    struct dwcotg_dev_global_regs_t *dev_global_regs = dwcotg_dcd->reg.dev.global_regs;
    vk_dwcotg_dc_ip_info_t info;

    dwcotg_dcd->param->op->GetInfo(&info.use_as__usb_dc_ip_info_t);
    dwcotg_dcd->buffer_word_pos = info.buffer_word_size;
    dwcotg_dcd->ep_num = info.ep_num >> 1;
    dwcotg_dcd->dma_en = info.dma_en;
    dwcotg_dcd->ctrl_transfer_state = DWCOTG_SETUP_STAGE;

    for (uint_fast8_t i = 0; i < dwcotg_dcd->ep_num; i++) {
        dwcotg_dcd->reg.dev.ep.out_regs[i].doepctl |= USB_OTG_DOEPCTL_SNAK;
    }
    dev_global_regs->dcfg &= ~USB_OTG_DCFG_DAD;
    memset(dwcotg_dcd->out_buf, 0, sizeof(dwcotg_dcd->out_buf));
}

void vk_dwcotg_dcd_connect(vk_dwcotg_dcd_t *dwcotg_dcd)
{
    debug_add_evt(__FUNC_CONNECT);
    dwcotg_dcd->reg.dev.global_regs->dctl &= ~USB_OTG_DCTL_SDIS;
}

void vk_dwcotg_dcd_disconnect(vk_dwcotg_dcd_t *dwcotg_dcd)
{
    debug_add_evt(__FUNC_DISCONNECT);
    dwcotg_dcd->reg.dev.global_regs->dctl |= USB_OTG_DCTL_SDIS;
}

void vk_dwcotg_dcd_wakeup(vk_dwcotg_dcd_t *dwcotg_dcd)
{
}

void vk_dwcotg_dcd_set_address(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t addr)
{

}

uint_fast8_t vk_dwcotg_dcd_get_address(vk_dwcotg_dcd_t *dwcotg_dcd)
{
    return (dwcotg_dcd->reg.dev.global_regs->dcfg & USB_OTG_DCFG_DAD) >> 4;
}

uint_fast16_t vk_dwcotg_dcd_get_frame_number(vk_dwcotg_dcd_t *dwcotg_dcd)
{
    return (dwcotg_dcd->reg.dev.global_regs->dsts & USB_OTG_DSTS_FNSOF) >> 8;
}

extern uint_fast8_t vk_dwcotg_dcd_get_mframe_number(vk_dwcotg_dcd_t *dwcotg_dcd)
{
    return 0;
}

void vk_dwcotg_dcd_get_setup(vk_dwcotg_dcd_t *dwcotg_dcd, uint8_t *buffer)
{
    memcpy(buffer, &dwcotg_dcd->setup, sizeof(dwcotg_dcd->setup));
}

void vk_dwcotg_dcd_status_stage(vk_dwcotg_dcd_t *dwcotg_dcd, bool is_in)
{
    debug_add_evt(__FUNC_STATUS_STAGE);

    dwcotg_dcd->ctrl_transfer_state = DWCOTG_STATUS_STAGE;
    if (is_in) {
        struct dwcotg_dev_in_ep_regs_t *in_regs = &dwcotg_dcd->reg.dev.ep.in_regs[0];

        in_regs->dieptsiz = (0x1 << 19) | 0;
        in_regs->diepctl |= USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_CNAK;
    } else {
        struct dwcotg_dev_out_ep_regs_t *out_regs = &dwcotg_dcd->reg.dev.ep.out_regs[0];
        out_regs->doeptsiz &= ~(USB_OTG_DOEPTSIZ_XFRSIZ | USB_OTG_DOEPTSIZ_PKTCNT);
        out_regs->doeptsiz |= (0x1 << 19) | 0;
        out_regs->doepctl |= USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_CNAK;
    }
}

uint_fast8_t vk_dwcotg_dcd_ep_get_feature(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep, uint_fast8_t feature)
{
    return 0;
}

static volatile uint32_t * __vk_dwcotg_dcd_get_ep_ctrl(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep)
{
    uint_fast8_t is_in = ep & 0x80;
    ep &= 0x0F;
    return is_in ? &dwcotg_dcd->reg.dev.ep.in_regs[ep].diepctl : &dwcotg_dcd->reg.dev.ep.out_regs[ep].doepctl;
}

vsf_err_t vk_dwcotg_dcd_ep_add(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep, usb_ep_type_t type, uint_fast16_t size)
{
    volatile uint32_t *ep_ctrl = __vk_dwcotg_dcd_get_ep_ctrl(dwcotg_dcd, ep);
    uint_fast8_t is_in = ep & 0x80;

    debug_add_evt(__FUNC_EP_ADD);

    ep &= 0x0F;
#if VSF_DWCOTG_DCD_CFG_FAKE_EP == ENABLED
    if (ep >= dwcotg_dcd->ep_num) {
        return VSF_ERR_NONE;
    }
#else
    VSF_USB_ASSERT(ep < dwcotg_dcd->ep_num);
#endif
    
    *ep_ctrl &= ~USB_OTG_DIEPCTL_MPSIZ;
    if (0 == ep) {
        *ep_ctrl |= USB_OTG_DIEPCTL_USBAEP;
        switch (size) {
        case 64:                    break;
        case 32:    *ep_ctrl |= 1;  break;
        case 16:    *ep_ctrl |= 2;  break;
        case 8:     *ep_ctrl |= 3;  break;
        default:
            VSF_USB_ASSERT(false);
            return VSF_ERR_NOT_SUPPORT;
        }
    } else {
        *ep_ctrl &= ~USB_OTG_DIEPCTL_EPTYP;

        switch (type) {
        case USB_EP_TYPE_CONTROL:
            VSF_USB_ASSERT(false);
            break;
        case USB_EP_TYPE_INTERRUPT:
            *ep_ctrl |= (0x3ul << 18) | USB_OTG_DIEPCTL_USBAEP;
            break;
        case USB_EP_TYPE_BULK:
            *ep_ctrl |= (0x2ul << 18) | USB_OTG_DIEPCTL_USBAEP;
            break;
        case USB_EP_TYPE_ISO:
            *ep_ctrl |= (0x1ul << 18) | USB_OTG_DIEPCTL_USBAEP;
            break;
        }
        *ep_ctrl |= size;
        if (is_in) {
            *ep_ctrl |= ep << 22;
        }
    }
    if (is_in) {
        size = (size + 3) & ~3;
        size = vsf_dwcotg_dcd_get_fifo_size(ep | 0x80, type, size);
        size >>= 2;
        dwcotg_dcd->buffer_word_pos -= size;

        if (!ep) {
            dwcotg_dcd->reg.global_regs->gnptxfsiz = (size << 16) | dwcotg_dcd->buffer_word_pos;
        } else {
            dwcotg_dcd->reg.global_regs->dtxfsiz[ep - 1] = (size << 16) | dwcotg_dcd->buffer_word_pos;
        }
        dwcotg_dcd->reg.global_regs->grxfsiz &= ~USB_OTG_GRXFSIZ_RXFD;
        dwcotg_dcd->reg.global_regs->grxfsiz |= dwcotg_dcd->buffer_word_pos;
        // flush FIFO to validate fifo settings
        __vk_dwcotg_dcd_flush_txfifo(dwcotg_dcd, 0x10);
        __vk_dwcotg_dcd_flush_rxfifo(dwcotg_dcd);
    }
    dwcotg_dcd->reg.dev.global_regs->daintmsk |= (1 << (is_in ? 0 : 16)) << ep;
    return VSF_ERR_NONE;
}

uint_fast16_t vk_dwcotg_dcd_ep_get_size(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep)
{
    volatile uint32_t *ep_ctrl = __vk_dwcotg_dcd_get_ep_ctrl(dwcotg_dcd, ep);

    ep &= 0x0F;
    VSF_USB_ASSERT(ep < dwcotg_dcd->ep_num);

    if (0 == ep) {
        switch (*ep_ctrl & USB_OTG_DIEPCTL_MPSIZ) {
        case 0:     return 64;
        case 1:     return 32;
        case 2:     return 16;
        case 3:     return 8;
        }
    } else {
        return *ep_ctrl & USB_OTG_DIEPCTL_MPSIZ;
    }
    return 0;
}

vsf_err_t vk_dwcotg_dcd_ep_set_stall(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep)
{
    volatile uint32_t *ep_ctrl = __vk_dwcotg_dcd_get_ep_ctrl(dwcotg_dcd, ep);

    ep &= 0x0F;
    VSF_USB_ASSERT(ep < dwcotg_dcd->ep_num);

    *ep_ctrl |= USB_OTG_DIEPCTL_STALL;
    return VSF_ERR_NONE;
}

bool vk_dwcotg_dcd_ep_is_stalled(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep)
{
    volatile uint32_t *ep_ctrl = __vk_dwcotg_dcd_get_ep_ctrl(dwcotg_dcd, ep);

    ep &= 0x0F;
    VSF_USB_ASSERT(ep < dwcotg_dcd->ep_num);

    return !!(*ep_ctrl & USB_OTG_DIEPCTL_STALL);
}

vsf_err_t vk_dwcotg_dcd_ep_clear_stall(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep)
{
    volatile uint32_t *ep_ctrl = __vk_dwcotg_dcd_get_ep_ctrl(dwcotg_dcd, ep);

    ep &= 0x0F;
    VSF_USB_ASSERT(ep < dwcotg_dcd->ep_num);

    *ep_ctrl &= ~USB_OTG_DIEPCTL_STALL;
    return VSF_ERR_NONE;
}

vsf_err_t vk_dwcotg_dcd_ep_transaction_read_buffer(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep, uint8_t *buffer, uint_fast16_t size)
{
    return VSF_ERR_NONE;
}

vsf_err_t vk_dwcotg_dcd_ep_transaction_enable_out(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep, uint8_t *buffer)
{
    VSF_USB_ASSERT(!(ep & 0x80));
    VSF_USB_ASSERT(ep < dwcotg_dcd->ep_num);
    
    uint_fast8_t ep_idx = ep;
    uint_fast16_t ep_size = vk_dwcotg_dcd_ep_get_size(dwcotg_dcd, ep);
    struct dwcotg_dev_out_ep_regs_t *out_regs = &dwcotg_dcd->reg.dev.ep.out_regs[ep_idx];
    
    dwcotg_dcd->out_buf[ep_idx] = buffer;
    out_regs->doeptsiz &= ~(USB_OTG_DOEPTSIZ_XFRSIZ | USB_OTG_DOEPTSIZ_PKTCNT);
    out_regs->doeptsiz |= (0x1 << 19) | ep_size;
    out_regs->doepctl |= USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_CNAK;
    return VSF_ERR_NONE;
}

vsf_err_t vk_dwcotg_dcd_ep_transaction_set_data_size(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep, uint_fast16_t size)
{
    VSF_USB_ASSERT(ep & 0x80);
    
    uint_fast8_t ep_idx = ep & 0x0F;
    VSF_USB_ASSERT(ep < dwcotg_dcd->ep_num);
    
    struct dwcotg_dev_in_ep_regs_t *in_regs = &dwcotg_dcd->reg.dev.ep.in_regs[ep_idx];

    if (!size) {
        in_regs->dieptsiz = 0x1ul << 19;
    }
    in_regs->diepctl |= USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_CNAK;
    
    return VSF_ERR_NONE;
}

vsf_err_t vk_dwcotg_dcd_ep_transaction_write_buffer(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep, uint8_t *buffer, uint_fast16_t size)
{
    VSF_USB_ASSERT(ep & 0x80);
    
    uint32_t data;
    uint_fast8_t ep_idx = ep & 0x0F; 
    VSF_USB_ASSERT(ep < dwcotg_dcd->ep_num);

    struct dwcotg_dev_in_ep_regs_t *in_regs = &dwcotg_dcd->reg.dev.ep.in_regs[ep_idx];

    in_regs->dieptsiz = (0x1ul << 19) | size;

    for (uint_fast16_t i = 0; i < size; i += 4, buffer += 4) {
#ifndef UNALIGNED
        data = get_unaligned_cpu32(buffer);
#else
        data = *(uint32_t UNALIGNED *)buffer;
#endif
        *dwcotg_dcd->reg.dfifo[ep_idx] = data;
    }
    dwcotg_dcd->reg.dev.global_regs->dtknqr4_fifoemptymsk |= 1 << ep_idx;
    return VSF_ERR_NONE;
}

uint_fast32_t vk_dwcotg_dcd_ep_get_data_size(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep)
{
    VSF_USB_ASSERT(!(ep & 0x80));
    VSF_USB_ASSERT(ep < dwcotg_dcd->ep_num);
    
    uint_fast8_t ep_idx = ep;
    return dwcotg_dcd->out_size[ep_idx];
}

vsf_err_t vk_dwcotg_dcd_ep_transfer_recv(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep, uint8_t *buffer, uint_fast32_t size)
{
    VSF_USB_ASSERT(false);
    return VSF_ERR_NOT_SUPPORT;
}

vsf_err_t vk_dwcotg_dcd_ep_transfer_send(vk_dwcotg_dcd_t *dwcotg_dcd, uint_fast8_t ep, uint8_t *buffer, uint_fast32_t size, bool zlp)
{
    VSF_USB_ASSERT(false);
    return VSF_ERR_NOT_SUPPORT;
}

static void __vk_dwcotg_dcd_notify(vk_dwcotg_dcd_t *dwcotg_dcd, usb_evt_t evt, uint_fast8_t value)
{
    if (dwcotg_dcd->callback.evt_handler != NULL) {
        dwcotg_dcd->callback.evt_handler(dwcotg_dcd->callback.param, evt, value);
    }
}

void vk_dwcotg_dcd_irq(vk_dwcotg_dcd_t *dwcotg_dcd)
{
    struct dwcotg_core_global_regs_t *global_regs = dwcotg_dcd->reg.global_regs;
    struct dwcotg_dev_global_regs_t *dev_global_regs = dwcotg_dcd->reg.dev.global_regs;
    struct dwcotg_dev_in_ep_regs_t *in_regs = dwcotg_dcd->reg.dev.ep.in_regs;
    struct dwcotg_dev_out_ep_regs_t *out_regs = dwcotg_dcd->reg.dev.ep.out_regs;
    uint_fast32_t intsts = global_regs->gintmsk | USB_OTG_GINTSTS_CMOD;

    intsts &= global_regs->gintsts;

    debug_add_evt(__USB_ON_IRQ);
    debug_add_evt(intsts);

    VSF_USB_ASSERT(!(intsts & USB_OTG_GINTSTS_CMOD));

    if (intsts & USB_OTG_GINTSTS_MMIS) {
        debug_add_evt(__USB_ON_MMIS);
        VSF_USB_ASSERT(false);
        global_regs->gintsts = USB_OTG_GINTSTS_MMIS;
    }
    if (intsts & USB_OTG_GINTSTS_USBRST) {
        debug_add_evt(__USB_ON_RST);
        __vk_dwcotg_dcd_notify(dwcotg_dcd, USB_ON_RESET, 0);
        global_regs->gintsts = USB_OTG_GINTSTS_USBRST;
    }
    if (intsts & USB_OTG_GINTSTS_ENUMDNE) {
        debug_add_evt(__USB_ON_ENUMDNE);
        uint8_t speed = (dev_global_regs->dsts & USB_OTG_DSTS_ENUMSPD) >> 1;
        global_regs->gusbcfg &= ~USB_OTG_GUSBCFG_TRDT;
        global_regs->gusbcfg |= ((0/* USB_SPEED_HIGH*/ == speed) ? 0x09U : 0x05U) << 10;
        dev_global_regs->dctl |= USB_OTG_DCTL_CGINAK;
        global_regs->gintsts = USB_OTG_GINTSTS_ENUMDNE;
    }
    if (intsts & USB_OTG_GINTSTS_USBSUSP) {
        debug_add_evt(__USB_ON_USBSUSP);
        __vk_dwcotg_dcd_notify(dwcotg_dcd, USB_ON_SUSPEND, 0);
        global_regs->gintsts = USB_OTG_GINTSTS_USBSUSP;
    }
    if (intsts & USB_OTG_GINTSTS_WKUINT) {
        debug_add_evt(__USB_ON_WKUINTP);
        __vk_dwcotg_dcd_notify(dwcotg_dcd, USB_ON_RESUME, 0);
        global_regs->gintsts = USB_OTG_GINTSTS_WKUINT;
    }
    if (intsts & USB_OTG_GINTSTS_SOF) {
        debug_add_evt(__USB_ON_SOF);
        __vk_dwcotg_dcd_notify(dwcotg_dcd, USB_ON_SOF, 0);
        global_regs->gintsts = USB_OTG_GINTSTS_SOF;
    }

    if (intsts & USB_OTG_GINTSTS_IEPINT) {
        debug_add_evt(__USB_ON_IEPINT);
        uint_fast8_t ep_idx = 0;
        uint_fast32_t ep_int = dev_global_regs->daint;
        ep_int = (ep_int & dev_global_regs->daintmsk) & 0xffff;

        while (ep_int) {
            if (ep_int & 0x1) {
                uint_fast32_t int_status = in_regs[ep_idx].diepint;
                debug_add_data(0xFF000000 + (ep_idx << 16) + int_status);

                uint_fast32_t int_msak = dev_global_regs->diepmsk | USB_OTG_DIEPINT_INEPNE | USB_OTG_DIEPINT_NAK;
                int_status &= (int_msak | USB_OTG_DIEPINT_TXFE);

                if (int_status & USB_OTG_DIEPINT_XFRC) {
                    if ((ep_idx == 0) && (dwcotg_dcd->ctrl_transfer_state == DWCOTG_STATUS_STAGE)) {
                        dwcotg_dcd->ctrl_transfer_state = DWCOTG_SETUP_STAGE;
                        debug_add_evt(USB_ON_STATUS);
                        __vk_dwcotg_dcd_notify(dwcotg_dcd, USB_ON_STATUS, 0);
                    } else {
                        debug_add_evt(__USB_ON_IN);
                        __vk_dwcotg_dcd_notify(dwcotg_dcd, USB_ON_IN, ep_idx);
                    }
                    in_regs[ep_idx].diepint = USB_OTG_DIEPINT_XFRC;
                }
                if (int_status & USB_OTG_DIEPINT_EPDISD) {
                    in_regs[ep_idx].diepint = USB_OTG_DIEPINT_EPDISD;
                }
                if (int_status & USB_OTG_DIEPINT_TOC) {
                    in_regs[ep_idx].diepint = USB_OTG_DIEPINT_TOC;
                }
                if (int_status & USB_OTG_DIEPINT_INEPNE) {
                    in_regs[ep_idx].diepint = USB_OTG_DIEPINT_INEPNE;
                }
                if (int_status & USB_OTG_DIEPINT_TXFE) {
                    debug_add_evt(__USB_ON_DIEPINT_TXFE);
                    in_regs[ep_idx].diepint = USB_OTG_DIEPINT_TXFE;
                }
            }
            ep_int >>= 1;
            ep_idx++;
        }
    }

    if (intsts & USB_OTG_GINTSTS_OEPINT) {
        debug_add_evt(__USB_ON_OEPINT);
        uint_fast8_t ep_idx = 0;
        uint_fast32_t ep_int = dev_global_regs->daint;
        ep_int = (ep_int & dev_global_regs->daintmsk) >> 16;

        while (ep_int) {
            if (ep_int & 0x1) {
                uint_fast32_t int_status = out_regs[ep_idx].doepint;
                debug_add_data(0xFF000000 + (ep_idx << 16) + int_status);

                int_status &= dev_global_regs->doepmsk | USB_OTG_DOEPINT_STSPHSERCVD;

                // transfer complete interrupt
                if (int_status & USB_OTG_DOEPINT_XFRC) {
                    if ((ep_idx == 0) && (dwcotg_dcd->ctrl_transfer_state == DWCOTG_STATUS_STAGE)) {
                        dwcotg_dcd->ctrl_transfer_state = DWCOTG_SETUP_STAGE;
                        debug_add_evt(__USB_ON_STATUS);
                        __vk_dwcotg_dcd_notify(dwcotg_dcd, USB_ON_STATUS, 0);
                    } else if (((ep_idx == 0) && dwcotg_dcd->ctrl_transfer_state == DWCOTG_DATA_STAGE) || (ep_idx > 0)) {
                        debug_add_evt(__USB_ON_OUT);
                        __vk_dwcotg_dcd_notify(dwcotg_dcd, USB_ON_OUT, ep_idx);
                    }
                    out_regs[ep_idx].doepint = USB_OTG_DOEPINT_XFRC;
                }
                // endpoint disable interrupt
                if (int_status & USB_OTG_DOEPINT_EPDISD) {
                    out_regs[ep_idx].doepint = USB_OTG_DOEPINT_EPDISD;
                }
                // setup phase finished interrupt (just for control endpoints)
                if (int_status & USB_OTG_DOEPINT_STUP) {
                    // need update address immediately
                    if (    ((((uint32_t *)dwcotg_dcd->setup)[0] & 0xFF00FFFF) == 0x00000500)
                        &&  (((uint32_t *)dwcotg_dcd->setup)[1] == 0x0)) {
                        VSF_USB_ASSERT(!vk_dwcotg_dcd_get_address(dwcotg_dcd));
                        dev_global_regs->dcfg |= (dwcotg_dcd->setup[2] & 0x7F) << 4;
                    }

                    debug_add_evt(__USB_ON_SETUP);
                    dwcotg_dcd->ctrl_transfer_state = DWCOTG_DATA_STAGE;
                    __vk_dwcotg_dcd_notify(dwcotg_dcd, USB_ON_SETUP, 0);
                    out_regs[ep_idx].doepint = USB_OTG_DOEPINT_STUP;
                }
                // back to back setup packets received
                if (int_status & USB_OTG_DOEPINT_B2BSTUP) {
                    debug_add_evt(__USB_ON_DOEPINT_B2BSTUPP);
                    out_regs[ep_idx].doepint = USB_OTG_DOEPINT_B2BSTUP;
                }
                if (int_status & USB_OTG_DOEPINT_STSPHSERCVD) {
                    out_regs[ep_idx].doepint = USB_OTG_DOEPINT_STSPHSERCVD;
                }
            }
            ep_int >>= 1;
            ep_idx++;
        }
    }

    if (intsts & USB_OTG_GINTSTS_RXFLVL) {
        uint_fast8_t ep_idx, pid;
        uint_fast16_t size;
        uint_fast32_t rx_status;

        global_regs->gintmsk &= ~USB_OTG_GINTMSK_RXFLVLM;
        rx_status = global_regs->grxstsp;

        debug_add_evt(__USB_ON_RXFLVL);
        debug_add_evt(0x80000000 + ((rx_status & USB_OTG_GRXSTSP_PKTSTS) >> 17));

        ep_idx = rx_status & USB_OTG_GRXSTSP_EPNUM;
        size = (rx_status & USB_OTG_GRXSTSP_BCNT) >> 4;
        pid = (rx_status & USB_OTG_GRXSTSP_DPID) >> 15;

        switch ((rx_status & USB_OTG_GRXSTSP_PKTSTS) >> 17) {
        case 1:
        case 3:
        case 4:
            break;
        case 6: //RXSTAT_SETUP_UPDT:
            if (!ep_idx && (8 == size) && (0/*DPID_DATA0*/ == pid)) {
                // In some versions of dwcotg, We can't replace dfifo[0] with grxstsp[0]
                ((uint32_t *)dwcotg_dcd->setup)[0] = *dwcotg_dcd->use_as__vk_dwcotg_t.reg.dfifo[0];
                ((uint32_t *)dwcotg_dcd->setup)[1] = *dwcotg_dcd->use_as__vk_dwcotg_t.reg.dfifo[0];

                debug_add_evt(__USB_ON_RXSTAT_SETUP_UPDT);
                debug_add_data(((uint32_t *)dwcotg_dcd->setup)[0]);
                debug_add_data(((uint32_t *)dwcotg_dcd->setup)[1]);
            }
            break;
        case 2: { //RXSTAT_DATA_UPDT: 
                uint32_t data;
                uint8_t *buffer = dwcotg_dcd->out_buf[ep_idx];
                debug_add_evt(__USB_ON_RXSTAT_DATA_UPDT);
                debug_add_evt(0x80000000 + (ep_idx << 16) + size);

                if (buffer) {
                    for (uint_fast16_t i = 0; i < size; i += 4, buffer += 4) {
                        data = *dwcotg_dcd->reg.dfifo[0];
                        #ifndef UNALIGNED
                        put_unaligned_cpu32(data, buffer);
                        #else
                        *(uint32_t UNALIGNED *)buffer = data;
                        #endif
                    }
                    dwcotg_dcd->out_size[ep_idx] = size;
                } else {
                    for (uint_fast16_t i = 0; i < size; i += 4, buffer += 4) {
                        data = *dwcotg_dcd->reg.dfifo[0];
                    }
                    dwcotg_dcd->out_size[ep_idx] = 0;
                }
            }
            break;
        //case RXSTAT_GOUT_NAK:
        //case RXSTAT_SETUP_COMP:
        default:
            VSF_HAL_ASSERT(false);
            break;
        }

        global_regs->gintmsk |= USB_OTG_GINTMSK_RXFLVLM;
    }
}

#endif
