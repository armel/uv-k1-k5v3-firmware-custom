/**
  ******************************************************************************
  * @file    usb_dc_py32.c
  * @author  MCU Application Team
  * @brief   CMSIS Cortex-M4 Device Peripheral Access Layer System Source File
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

#include "usb_py32_reg.h"
#include "usbd_core.h"

#define HWREG(x) \
    (*((volatile uint32_t *)(x)))
#define HWREGH(x) \
    (*((volatile uint16_t *)(x)))
#define HWREGB(x) \
    (*((volatile uint8_t *)(x)))

#ifndef USB_BASE
#define USB_BASE (0x40005C00)
#endif

#ifndef USB_NUM_BIDIR_ENDPOINTS
#define USB_NUM_BIDIR_ENDPOINTS 8
#endif

typedef enum {
  USB_EP0_STATE_SETUP = 0x0,      /**< SETUP DATA */
  USB_EP0_STATE_IN_DATA = 0x1,    /**< IN DATA */
  USB_EP0_STATE_OUT_DATA = 0x3,   /**< OUT DATA */
  USB_EP0_STATE_IN_STATUS = 0x4,  /**< IN status */
  USB_EP0_STATE_OUT_STATUS = 0x5, /**< OUT status */
  USB_EP0_STATE_IN_ZLP = 0x6,     /**< OUT status */
  USB_EP0_STATE_STALL = 0x7,      /**< STALL status */
} ep0_state_t;

/* Endpoint state */
struct pyusb_ep_state {
  uint16_t ep_mps;    /* Endpoint max packet size */
  uint8_t ep_type;    /* Endpoint type */
  uint8_t ep_stalled; /* Endpoint stall flag */
  uint8_t ep_enable;  /* Endpoint enable */
  uint8_t *xfer_buf;  /* data buffer */
  uint32_t xfer_len;
  uint32_t actual_xfer_len;
};

/* Driver state */
struct pyusb_udc {
  volatile uint8_t dev_addr;
  volatile uint32_t fifo_size_offset;
  __attribute__((aligned(32))) struct usb_setup_packet setup;
  struct pyusb_ep_state in_ep[USB_NUM_BIDIR_ENDPOINTS];  /*!< IN endpoint parameters*/
  struct pyusb_ep_state out_ep[USB_NUM_BIDIR_ENDPOINTS]; /*!< OUT endpoint parameters */
} g_pyusb_udc;

static volatile uint8_t usb_ep0_state = USB_EP0_STATE_SETUP;
volatile bool zlp_flag = 0;

void usbd_ep0_set_zlp_flag()
{
  zlp_flag = TRUE;
}

void usbd_ep0_reset_zlp_flag()
{
  zlp_flag = FALSE;
}

/* get current active ep */
static uint8_t pyusb_get_active_ep(void)
{
  return (uint8_t)(USB->INDEX);
}

/* set the active ep */
static void pyusb_set_active_ep(uint8_t ep_index)
{
  USB->INDEX = ep_index;
}

static void pyusb_write_packet(uint8_t ep_idx, uint8_t *buffer, uint16_t len)
{
  uint8_t  *nAddr;
  uint8_t  *tmp = (uint8_t *)buffer;
  uint16_t count = len;

  nAddr = &((uint8_t *)&USB->FIFO_EP0)[ep_idx<<2];

  if(count)
  {
    while (count)
    {
      *nAddr = *tmp++;
      count--;
    }
  }
}

static void pyusb_read_packet(uint8_t ep_idx, uint8_t *buffer, uint16_t len)
{
  uint8_t *tmp = (uint8_t *)buffer;
  uint8_t *nAddr;
  uint16_t count = len;

  nAddr = &((uint8_t *)&USB->FIFO_EP0)[ep_idx<<2];

  if(count)
  {
    while (count)
    {
      *tmp++ = *nAddr;
      count--;
    }
  }
}

static uint32_t pyusb_get_fifo_size(uint16_t mps, uint16_t *used)
{
  uint32_t size;

  for (uint8_t i = USB_TXFIFOSZ_SIZE_8; i <= USB_TXFIFOSZ_SIZE_M; i++) {
    size = (i << 3);
    if (mps <= size) {
      *used = size;
      return i;
    }
  }

  *used = 0;
  return USB_TXFIFOSZ_SIZE_8;
}

__WEAK void usb_dc_low_level_init(void)
{
}

__WEAK void usb_dc_low_level_deinit(void)
{
}

int usb_dc_init(void)
{
  usb_dc_low_level_init();

  pyusb_set_active_ep(0);
  USB->ADDR = 0;

//  USB->INT_USBE = USB_INTR_RESET;
  USB->INT_USBE = USB_INTR_RESET | USB_INTR_SUSPEND | USB_INTR_RESUME;
  USB->INT_IN1E = USB_INTR_EP0;
  USB->INT_OUT1E = 0;

  return 0;
}

int usb_dc_deinit(void)
{
  return 0;
}

int usbd_set_address(const uint8_t addr)
{
  if (addr == 0)
  {
    USB->ADDR = 0;
  }
  g_pyusb_udc.dev_addr = addr;
  return 0;
}

int usbd_ep_open(const struct usbd_endpoint_cfg *ep_cfg)
{
  uint16_t used = 0;
  uint16_t fifo_size = 0;
  uint8_t ep_idx = USB_EP_GET_IDX(ep_cfg->ep_addr);
  uint8_t old_ep_idx;
  uint32_t ui32Flags = 0;
  uint16_t ui32Register = 0;

  if (ep_idx == 0) {
    g_pyusb_udc.out_ep[0].ep_mps = USB_CTRL_EP_MPS;
    g_pyusb_udc.out_ep[0].ep_type = 0x00;
    g_pyusb_udc.out_ep[0].ep_enable = true;
    g_pyusb_udc.in_ep[0].ep_mps = USB_CTRL_EP_MPS;
    g_pyusb_udc.in_ep[0].ep_type = 0x00;
    g_pyusb_udc.in_ep[0].ep_enable = true;
    return 0;
  }

  if (ep_idx > (USB_NUM_BIDIR_ENDPOINTS - 1)) {
    USB_LOG_ERR("Ep addr %d overflow\r\n", ep_cfg->ep_addr);
    return -1;
  }

  old_ep_idx = pyusb_get_active_ep();
  pyusb_set_active_ep(ep_idx);

  if (USB_EP_DIR_IS_OUT(ep_cfg->ep_addr))  /* OUT endpoint */
  {
    g_pyusb_udc.out_ep[ep_idx].ep_mps = ep_cfg->ep_mps;
    g_pyusb_udc.out_ep[ep_idx].ep_type = ep_cfg->ep_type;
    g_pyusb_udc.out_ep[ep_idx].ep_enable = true;

    /* Allow auto clearing of RxPktRdy when packet of size max packet
       has been unloaded from the FIFO. */
    if (ui32Flags & USB_EP_AUTO_CLEAR)
    {
      ui32Register = USB_OUTCSR2_AutoClear;
    }

    /* Configure the DMA mode. */
    if (ui32Flags & USB_EP_DMA_MODE_1)
    {
      ui32Register |= USB_OUTCSR2_DMAEnab | USB_OUTCSR2_DMAMode;
    } else if (ui32Flags & USB_EP_DMA_MODE_0)
    {
      ui32Register |= USB_OUTCSR2_DMAEnab;
    }

    /* Enable isochronous mode if requested. */
    if (ep_cfg->ep_type == 0x01)
    {
      ui32Register |= USB_OUTCSR2_ISO;
    }

    USB->OUT_CSR2 = ui32Register;

    /* Reset the Data toggle to zero. */
    if (USB->OUT_CSR1 & USB_OUTCSR_OPR)
    {
      USB->OUT_CSR1 = (USB_OUTCSR_FF | USB_OUTCSR_CDT);
    } else
    {
      USB->OUT_CSR1 = USB_OUTCSR_CDT;
    }

    fifo_size = pyusb_get_fifo_size(ep_cfg->ep_mps, &used);

    USB->MAX_PKT_OUT = fifo_size;

    g_pyusb_udc.fifo_size_offset += used;
  } else { /* IN endpoint */
    g_pyusb_udc.in_ep[ep_idx].ep_mps = ep_cfg->ep_mps;
    g_pyusb_udc.in_ep[ep_idx].ep_type = ep_cfg->ep_type;
    g_pyusb_udc.in_ep[ep_idx].ep_enable = true;

    USB->INT_IN1E |= (1 << ep_idx);

    /* Allow auto setting of TxPktRdy when max packet size has been loaded
       into the FIFO. */
    if (ui32Flags & USB_EP_AUTO_SET) {
      ui32Register |= USB_INCSR2_AUTOSET;
    }

    /* Configure the DMA mode. */
    if (ui32Flags & (USB_EP_DMA_MODE_0 | USB_EP_DMA_MODE_1)) {
      ui32Register |= USB_INCSR2_DMAEnab;
    }

    /* Enable isochronous mode if requested. */
    if (ep_cfg->ep_type == 0x01) {
      ui32Register |= USB_INCSR2_ISO;
    }

    ui32Register |= USB_INCSR2_MODE;

    USB->IN_CSR2 = ui32Register;

    /* Reset the Data toggle to zero. */
    if (USB->IN_CSR1 & USB_INCSR_FifoNotEmpty)
    {
      USB->IN_CSR1 = (USB_INCSR_FF | USB_INCSR_CDT);
    } else
    {
      USB->IN_CSR1 = USB_INCSR_CDT;
    }

    fifo_size = pyusb_get_fifo_size(ep_cfg->ep_mps, &used);

    USB->MAX_PKT_IN = fifo_size;

    g_pyusb_udc.fifo_size_offset += used;
  }

  pyusb_set_active_ep(old_ep_idx);

  return 0;
}

int usbd_ep_close(const uint8_t ep)
{
  return 0;
}

int usbd_ep_set_stall(const uint8_t ep)
{
  uint8_t ep_idx = USB_EP_GET_IDX(ep);
  uint8_t old_ep_idx;

  old_ep_idx = pyusb_get_active_ep();
  pyusb_set_active_ep(ep_idx);

  if (USB_EP_DIR_IS_OUT(ep))
  {
    if (ep_idx == 0x00)
    {
      usb_ep0_state = USB_EP0_STATE_STALL;

      USB->EP0_CSR |= (USB_CSR0_SENDSTALL | USB_CSR0_SVDOUTPKTRDY);
    } else
    {
      USB->OUT_CSR1 |= USB_OUTCSR_SendStall;
    }
  } else
  {
    if (ep_idx == 0x00)
    {
      usb_ep0_state = USB_EP0_STATE_STALL;

      USB->EP0_CSR |= (USB_CSR0_SENDSTALL | USB_CSR0_SVDOUTPKTRDY);
    } else
    {
      USB->IN_CSR1 |= USB_INCSR_SendStall;
    }
  }

  pyusb_set_active_ep(old_ep_idx);
  return 0;
}

int usbd_ep_clear_stall(const uint8_t ep)
{
  uint8_t ep_idx = USB_EP_GET_IDX(ep);
  uint8_t old_ep_idx;

  old_ep_idx = pyusb_get_active_ep();
  pyusb_set_active_ep(ep_idx);

  if (USB_EP_DIR_IS_OUT(ep))
  {
    if (ep_idx == 0x00)
    {
      USB->EP0_CSR &= ~USB_CSR0_SENTSTALL;
    } else
    {
      /* Clear the stall on an OUT endpoint. */
      USB->OUT_CSR1 &= ~(USB_OUTCSR_SendStall | USB_OUTCSR_SentStall);
      /* Reset the data toggle. */
      USB->OUT_CSR1 |= USB_OUTCSR_CDT;
    }
  } else
  {
    if (ep_idx == 0x00)
    {
      USB->EP0_CSR &= ~USB_CSR0_SENTSTALL;
    } else {
      /* Clear the stall on an IN endpoint. */
      USB->IN_CSR1 &= ~(USB_INCSR_SendStall | USB_INCSR_SentStall);
      /* Reset the data toggle. */
      USB->IN_CSR1 |= USB_INCSR_CDT;
    }
  }

  pyusb_set_active_ep(old_ep_idx);
  return 0;
}

int usbd_ep_is_stalled(const uint8_t ep, uint8_t *stalled)
{
  uint8_t ep_idx = USB_EP_GET_IDX(ep);
  uint8_t old_ep_idx;

  old_ep_idx = pyusb_get_active_ep();
  pyusb_set_active_ep(ep_idx);

  if (USB_EP_DIR_IS_OUT(ep))
  {
    if (ep_idx == 0x00)
    {
      *stalled = 0;
    } else
    {
      *stalled = !!(USB->OUT_CSR1 & USB_OUTCSR_SendStall);
    }
  } else
  {
    if (ep_idx == 0x00)
    {
      *stalled = 0;
    } else {
      *stalled = !!(USB->IN_CSR1 & USB_INCSR_SendStall);
    }
  }

  pyusb_set_active_ep(old_ep_idx);
  return 0;
}

int usbd_ep_start_write(const uint8_t ep, const uint8_t *data, uint32_t data_len)
{
  uint8_t ep_idx = USB_EP_GET_IDX(ep);
  uint8_t old_ep_idx;

  if (!data && data_len) {
    return -1;
  }
  if (!g_pyusb_udc.in_ep[ep_idx].ep_enable) {
    return -2;
  }

  old_ep_idx = pyusb_get_active_ep();
  pyusb_set_active_ep(ep_idx);

  if (USB->IN_CSR1 & USB_INCSR_IPR)
  {
    pyusb_set_active_ep(old_ep_idx);
    return -3;
  }

  g_pyusb_udc.in_ep[ep_idx].xfer_buf = (uint8_t *)data;
  g_pyusb_udc.in_ep[ep_idx].xfer_len = data_len;
  g_pyusb_udc.in_ep[ep_idx].actual_xfer_len = 0;

  if(ep_idx == 0)
  {
    switch(usb_ep0_state)
    {
      case USB_EP0_STATE_SETUP:
        /* Zero data request */
        if(data_len == 0)
        {
          USB->EP0_CSR = (USB_CSR0_SVDOUTPKTRDY | USB_CSR0_DATAEND);
          usb_ep0_state = USB_EP0_STATE_IN_STATUS;
        }
        /* Enter IN data stage */
        else
        {
          /* Clear OutPktRdy bit for the next setup packet receive */
          USB->EP0_CSR = USB_CSR0_SVDOUTPKTRDY;

          /* The length of the sent data is less than or equal to the maximum packet length and no additional 0 packets are required */
          if(data_len <= g_pyusb_udc.in_ep[ep_idx].ep_mps && zlp_flag == FALSE)
          {
            pyusb_write_packet(ep_idx, (uint8_t *)data, data_len);
            USB->EP0_CSR = (USB_CSR0_INPKTRDY | USB_CSR0_DATAEND);
          }
          /* If the packet length exceeds the maximum, only the data with the maximum packet length is sent */
          else
          {
            pyusb_write_packet(ep_idx, (uint8_t *)data, g_pyusb_udc.in_ep[ep_idx].ep_mps);
            USB->EP0_CSR = USB_CSR0_INPKTRDY;
          }
          usb_ep0_state = USB_EP0_STATE_IN_DATA;
        }
      break;
      case USB_EP0_STATE_IN_DATA:
        /* Zero length packet need to be sent */
        if(data_len == 0)
        {
          /* clear zlp flag */
          usbd_ep0_reset_zlp_flag();
          USB->EP0_CSR = (USB_CSR0_INPKTRDY | USB_CSR0_DATAEND);
          usb_ep0_state = USB_EP0_STATE_IN_ZLP;
        }
        /* Start sending the remain data */
        else
        {
          /* The length of the sent data is less than or equal to the maximum packet length and no additional 0 packets are required */
          if(data_len <= g_pyusb_udc.in_ep[ep_idx].ep_mps && zlp_flag == FALSE)
          {
            pyusb_write_packet(ep_idx, (uint8_t *)data, data_len);
            USB->EP0_CSR = (USB_CSR0_INPKTRDY | USB_CSR0_DATAEND);
          }
          /* If the packet length exceeds the maximum, only the data with the maximum packet length is sent */
          else
          {
            pyusb_write_packet(ep_idx, (uint8_t *)data, g_pyusb_udc.in_ep[ep_idx].ep_mps);
            USB->EP0_CSR = USB_CSR0_INPKTRDY;
          }
        }
      break;

      case USB_EP0_STATE_OUT_DATA:
        /* Received all data, enter status stage */
        USB->EP0_CSR = (USB_CSR0_SVDOUTPKTRDY | USB_CSR0_DATAEND);
        usb_ep0_state = USB_EP0_STATE_IN_STATUS;
      break;
    }
  }
  else
  {
    if(data_len != 0)
    {
      /* If the packet length exceeds the maximum, only the data with the maximum packet length is sent */
      if(data_len > g_pyusb_udc.in_ep[ep_idx].ep_mps)
      {
        pyusb_write_packet(ep_idx, (uint8_t *)data, g_pyusb_udc.in_ep[ep_idx].ep_mps);
      }
      /* The remaining data can be sent in one package */
      else
      {
        pyusb_write_packet(ep_idx, (uint8_t *)data, data_len);
      }
    }
    USB->IN_CSR1 = USB_INCSR_IPR;
  }

  pyusb_set_active_ep(old_ep_idx);
  return 0;
}

/* Drop an IN transfer the host is not taking: the pending packet is flushed
 * and the rest forgotten, so the buffer may be reused.  Call with the USB
 * interrupt masked (it shares the endpoint index register).
 * FifoNotEmpty as in usbd_ep_open: a packet may wait with InPktRdy clear.
 * actual_xfer_len cleared: if the flush raises an IN interrupt, the ISR then
 * reports 0 bytes, and the class driver queues no ZLP after the cancel. */
void usbd_ep_cancel_write(const uint8_t ep)
{
  uint8_t ep_idx = USB_EP_GET_IDX(ep);
  uint8_t old_ep_idx = pyusb_get_active_ep();

  pyusb_set_active_ep(ep_idx);
  g_pyusb_udc.in_ep[ep_idx].xfer_len = 0;
  g_pyusb_udc.in_ep[ep_idx].actual_xfer_len = 0;
  if (USB->IN_CSR1 & (USB_INCSR_IPR | USB_INCSR_FifoNotEmpty))
    USB->IN_CSR1 = USB_INCSR_FF;
  pyusb_set_active_ep(old_ep_idx);
}

int usbd_ep_start_read(const uint8_t ep, uint8_t *data, uint32_t data_len)
{
  uint8_t ep_idx = USB_EP_GET_IDX(ep);
  uint8_t old_ep_idx;

  if (!data && data_len)
  {
    return -1;
  }
  if (!g_pyusb_udc.out_ep[ep_idx].ep_enable)
  {
    return -2;
  }

  old_ep_idx = pyusb_get_active_ep();
  pyusb_set_active_ep(ep_idx);

  g_pyusb_udc.out_ep[ep_idx].xfer_buf = data;
  g_pyusb_udc.out_ep[ep_idx].xfer_len = data_len;
  g_pyusb_udc.out_ep[ep_idx].actual_xfer_len = 0;

  if(ep_idx == 0)
  {
    switch(usb_ep0_state)
    {
      case USB_EP0_STATE_SETUP:
        /* Enter data stage, start receive data */
        USB->EP0_CSR |= USB_CSR0_SVDOUTPKTRDY;
        usb_ep0_state = USB_EP0_STATE_OUT_DATA;
      break;

      case USB_EP0_STATE_IN_DATA:
      case USB_EP0_STATE_IN_ZLP:
        /* Enter status stage, nothing to do */
        usb_ep0_state = USB_EP0_STATE_OUT_STATUS;
      break;
    }
  }
  else
  {
    /* Enable ep receive interrupt before clear OutPktRdy */
    USB->INT_OUT1E |= (1 << ep_idx);

    /* Clear OutPktRdy to receive data */
    USB->OUT_CSR1 &= ~USB_OUTCSR_OPR;
  }

  pyusb_set_active_ep(old_ep_idx);
  return 0;
}

static void handle_ep0(void)
{
  uint8_t  ep0_status = USB->EP0_CSR;
  uint16_t read_count;

  if (ep0_status & USB_CSR0_SENTSTALL)
  {
    USB->EP0_CSR &= ~USB_CSR0_SENTSTALL;
    usb_ep0_state = USB_EP0_STATE_SETUP;
    return;
  }

  if (ep0_status & USB_CSR0_SETUPEND)
  {
    USB->EP0_CSR = USB_CSR0_SVDSETUPEND;
    usb_ep0_state = USB_EP0_STATE_SETUP;
  }

  if (g_pyusb_udc.dev_addr > 0)
  {
    USB->ADDR = g_pyusb_udc.dev_addr;
    g_pyusb_udc.dev_addr = 0;
  }

  switch (usb_ep0_state) {
  case USB_EP0_STATE_SETUP:
    if (ep0_status & USB_CSR0_OUTPKTRDY)
    {
      read_count = USB->EP0_COUNT;
      if (read_count != 8)
      {
        return;
      }

      pyusb_read_packet(0, (uint8_t *)&g_pyusb_udc.setup, 8);

      usbd_event_ep0_setup_complete_handler((uint8_t *)&g_pyusb_udc.setup);
    }
    break;

  case USB_EP0_STATE_IN_DATA:
    if (g_pyusb_udc.in_ep[0].xfer_len > g_pyusb_udc.in_ep[0].ep_mps) {
      g_pyusb_udc.in_ep[0].actual_xfer_len += g_pyusb_udc.in_ep[0].ep_mps;
      g_pyusb_udc.in_ep[0].xfer_len -= g_pyusb_udc.in_ep[0].ep_mps;
    } else {
      g_pyusb_udc.in_ep[0].actual_xfer_len += g_pyusb_udc.in_ep[0].xfer_len;
      g_pyusb_udc.in_ep[0].xfer_len = 0;
    }

    usbd_event_ep_in_complete_handler(0x80, g_pyusb_udc.in_ep[0].actual_xfer_len);

    break;
  case USB_EP0_STATE_OUT_DATA:
    if (ep0_status & USB_CSR0_OUTPKTRDY)
    {
      read_count = USB->EP0_COUNT;

      pyusb_read_packet(0, g_pyusb_udc.out_ep[0].xfer_buf, read_count);
      g_pyusb_udc.out_ep[0].xfer_buf += read_count;
      g_pyusb_udc.out_ep[0].actual_xfer_len += read_count;

      /* End of transfer:
         1.Current packet length less than the max packet length
         2.The total transfer size reaches the preset size of setup packet
       */
      if ((read_count < g_pyusb_udc.out_ep[0].ep_mps) || (g_pyusb_udc.out_ep[0].actual_xfer_len == g_pyusb_udc.out_ep[0].xfer_len))
      {
        usbd_event_ep_out_complete_handler(0x00, g_pyusb_udc.out_ep[0].actual_xfer_len);
      } else
      {
        USB->EP0_CSR = USB_CSR0_SVDOUTPKTRDY;
      }
    }
    break;
  case USB_EP0_STATE_IN_ZLP:
    usbd_event_ep_in_complete_handler(0x80, 0);
    break;
  case USB_EP0_STATE_IN_STATUS:
  case USB_EP0_STATE_OUT_STATUS:
    usb_ep0_state = USB_EP0_STATE_SETUP;
    break;
    
  }
}

void USBD_IRQHandler(void)
{
  uint32_t is;
  uint32_t txis;
  uint32_t rxis;
  uint8_t old_ep_idx;
  uint8_t ep_idx;
  uint16_t write_count, read_count;

  is   = USB->INT_USB;
  txis = USB->INT_IN1;
  rxis = USB->INT_OUT1;

  old_ep_idx = pyusb_get_active_ep();

  /* Receive a reset signal from the USB bus */
  if (is & USB_INTR_RESET) {
    memset(&g_pyusb_udc, 0, sizeof(struct pyusb_udc));
    g_pyusb_udc.fifo_size_offset = USB_CTRL_EP_MPS;
    usbd_event_reset_handler();
    USB->POWER |= USB_POWER_SUSPENDENB;
    USB->INT_IN1E = USB_INTR_EP0;
    USB->INT_OUT1E = 0;

    usb_ep0_state = USB_EP0_STATE_SETUP;
  }

  if (is & USB_INTR_SOF)
  {
  }

  if (is & USB_INTR_RESUME)
  {
     usbd_event_resume_handler();
  }

  if (is & USB_INTR_SUSPEND)
  {
     usbd_event_suspend_handler();
  }

  txis &= USB->INT_IN1E;
  /* Handle EP0 interrupt */
  if (txis & USB_INTR_EP0)
  {
    pyusb_set_active_ep(0);
    handle_ep0();
    txis &= ~USB_INTR_EP0;
  }

  ep_idx = 1;
  while (txis) {
    if (txis & (1 << ep_idx))
    {
      pyusb_set_active_ep(ep_idx);

      if (USB->IN_CSR1 & USB_INCSR_UnderRun)
      {
        USB->IN_CSR1 &= ~USB_INCSR_UnderRun;
      }

      if (g_pyusb_udc.in_ep[ep_idx].xfer_len > g_pyusb_udc.in_ep[ep_idx].ep_mps)
      {
        g_pyusb_udc.in_ep[ep_idx].xfer_buf += g_pyusb_udc.in_ep[ep_idx].ep_mps;
        g_pyusb_udc.in_ep[ep_idx].actual_xfer_len += g_pyusb_udc.in_ep[ep_idx].ep_mps;
        g_pyusb_udc.in_ep[ep_idx].xfer_len -= g_pyusb_udc.in_ep[ep_idx].ep_mps;
      } else
      {
        g_pyusb_udc.in_ep[ep_idx].xfer_buf += g_pyusb_udc.in_ep[ep_idx].xfer_len;
        g_pyusb_udc.in_ep[ep_idx].actual_xfer_len += g_pyusb_udc.in_ep[ep_idx].xfer_len;
        g_pyusb_udc.in_ep[ep_idx].xfer_len = 0;
      }

      if (g_pyusb_udc.in_ep[ep_idx].xfer_len == 0)
      {
        usbd_event_ep_in_complete_handler(ep_idx | 0x80, g_pyusb_udc.in_ep[ep_idx].actual_xfer_len);
      } else
      {
        write_count = MIN(g_pyusb_udc.in_ep[ep_idx].xfer_len, g_pyusb_udc.in_ep[ep_idx].ep_mps);

        pyusb_write_packet(ep_idx, g_pyusb_udc.in_ep[ep_idx].xfer_buf, write_count);

        USB->IN_CSR1 = USB_INCSR_IPR;
      }

      txis &= ~(1 << ep_idx);
    }
    ep_idx++;
  }

  rxis &= USB->INT_OUT1E;
  ep_idx = 1;
  while (rxis)
  {
    if (rxis & (1 << ep_idx))
    {
      pyusb_set_active_ep(ep_idx);
      if (USB->OUT_CSR1 & USB_OUTCSR_OPR)
      {
        read_count = USB->OUT_COUNT;

        pyusb_read_packet(ep_idx, g_pyusb_udc.out_ep[ep_idx].xfer_buf, read_count);

        g_pyusb_udc.out_ep[ep_idx].xfer_buf += read_count;
        g_pyusb_udc.out_ep[ep_idx].actual_xfer_len += read_count;
        g_pyusb_udc.out_ep[ep_idx].xfer_len -= read_count;

        if ((read_count < g_pyusb_udc.out_ep[ep_idx].ep_mps) || (g_pyusb_udc.out_ep[ep_idx].xfer_len == 0))
        {
          USB->INT_OUT1E &=  ~(1 << ep_idx);
          usbd_event_ep_out_complete_handler(ep_idx, g_pyusb_udc.out_ep[ep_idx].actual_xfer_len);
        } else
        {
          USB->OUT_CSR1 &= ~USB_OUTCSR_OPR;
        }
      }

      rxis &= ~(1 << ep_idx);
    }
    ep_idx++;
  }

  pyusb_set_active_ep(old_ep_idx);
}

void usbd_activateremotewakeup(void)
{
  USB->POWER |= USB_POWER_RESUME;
}

void usbd_deactivateremotewakeup(void)
{
  USB->POWER &= ~USB_POWER_RESUME;
}
