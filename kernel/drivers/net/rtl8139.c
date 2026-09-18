/*
 * DevOS — RTL8139 NIC driver
 *
 * RTL8139 register offsets (from I/O BAR):
 *   0x00-0x05  IDR0-5   MAC address
 *   0x08-0x0D  MAR0-7   Multicast register
 *   0x10       TSD0     TX Status Descriptor 0  (×4 at +0, +4, +8, +12)
 *   0x20       TSAD0    TX Start Address 0      (×4)
 *   0x30       RBSTART  RX Buffer Start Address
 *   0x3C       IMR      Interrupt Mask Register
 *   0x3E       ISR      Interrupt Status Register
 *   0x40       TCR      TX Configuration Register
 *   0x44       RCR      RX Configuration Register
 *   0x50       MPC      Missed Packet Counter
 *   0x52       9346CR   93C46 Command Register (config locking)
 *   0x37       CR       Command Register (RST, RE, TE bits)
 *   0x38       CAPR     Current Address of Packet Read
 *   0x3A       CBR      Current Buffer Address (read-only)
 *
 * TX status bits (TSD):
 *   bit13  TOK  TX OK
 *   bit14  TUN  TX FIFO Underrun
 *   bit15  TABT TX Abort
 *   bit30  OWN  DMA complete
 *
 * RX packet header (16-bit):
 *   bit0  ROK  Receive OK
 *   bit1  FAE  Frame Alignment Error
 *   bit2  CRC  CRC Error
 *
 * RX frame format in ring buffer:
 *   [u16 status][u16 length][data...][pad to DWORD]
 */
#include "rtl8139.h"
#include "netdev.h"
#include "../pci/pci.h"
#include "../../arch/x86_64/irq.h"
#include "../../arch/x86_64/serial.h"
#include "../../mm/heap.h"
#include "../../lib/string.h"

#define RTL_VENDOR  0x10EC
#define RTL_DEVICE  0x8139

/* Register offsets */
#define RTL_IDR0    0x00
#define RTL_MAR0    0x08
#define RTL_TSD0    0x10
#define RTL_TSAD0   0x20
#define RTL_RBSTART 0x30
#define RTL_IMR     0x3C
#define RTL_ISR     0x3E
#define RTL_TCR     0x40
#define RTL_RCR     0x44
#define RTL_9346CR  0x52
#define RTL_CR      0x37
#define RTL_CAPR    0x38
#define RTL_CBR     0x3A

/* ISR bits */
#define RTL_ISR_ROK   (1 << 0)   /* Receive OK */
#define RTL_ISR_TOK   (1 << 2)   /* Transmit OK */
#define RTL_ISR_TER   (1 << 3)   /* Transmit Error */
#define RTL_ISR_RER   (1 << 1)   /* Receive Error */

/* CR bits */
#define RTL_CR_RST    (1 << 4)
#define RTL_CR_RE     (1 << 3)
#define RTL_CR_TE     (1 << 2)

#define RTL_RX_BUF_SIZE  (64 * 1024 + 16)
#define RTL_TX_BUF_SIZE  2048
#define RTL_TX_DESCS     4

typedef struct {
    uint16_t   io_base;
    uint8_t   *rx_buf;
    uint8_t   *tx_buf[RTL_TX_DESCS];
    uint32_t   tx_cur;    /* current TX descriptor */
    uint16_t   rx_cur;    /* current RX ring offset */
    net_device_t netdev;
} rtl8139_t;

static rtl8139_t rtl;

/* ---- I/O port helpers ---- */
static inline uint8_t  inb(uint16_t p) { uint8_t  v; __asm__("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outb(uint16_t p, uint8_t v)  { __asm__("outb %0,%1"::"a"(v),"Nd"(p):"memory"); }
static inline void outw(uint16_t p, uint16_t v) { __asm__("outw %0,%1"::"a"(v),"Nd"(p):"memory"); }
static inline void outl(uint16_t p, uint32_t v) { __asm__("outl %0,%1"::"a"(v),"Nd"(p):"memory"); }

#define R8(r)     inb ((uint16_t)(rtl.io_base + (r)))
#define R16(r)    inw ((uint16_t)(rtl.io_base + (r)))
#define R32(r)    inl ((uint16_t)(rtl.io_base + (r)))
#define W8(r,v)   outb((uint16_t)(rtl.io_base + (r)), (uint8_t)(v))
#define W16(r,v)  outw((uint16_t)(rtl.io_base + (r)), (uint16_t)(v))
#define W32(r,v)  outl((uint16_t)(rtl.io_base + (r)), (uint32_t)(v))

/* ---- IRQ handler ---- */

static void rtl8139_irq(interrupt_frame_t *frame)
{
    (void)frame;
    uint16_t status = R16(RTL_ISR);
    W16(RTL_ISR, status);   /* acknowledge */

    if (status & RTL_ISR_ROK) {
        /* Process received packets */
        while (!(R8(RTL_CR) & 0x01)) {   /* while RX buffer not empty */
            uint16_t offset = rtl.rx_cur;
            uint8_t *ptr    = rtl.rx_buf + offset;

            uint16_t pkt_hdr = *(uint16_t *)ptr;
            uint16_t pkt_len = *(uint16_t *)(ptr + 2);

            if (!(pkt_hdr & 1)) {
                serial_puts("[RTL] RX error\n");
                break;
            }

            if (pkt_len > 4 && pkt_len < 1514 + 4) {
                netdev_dispatch_rx(&rtl.netdev, ptr + 4, (size_t)(pkt_len - 4));
            }

            /* Advance CAPR (align to 4 bytes, wrap around 64 KB) */
            rtl.rx_cur = (uint16_t)((offset + pkt_len + 4 + 3) & ~3);
            rtl.rx_cur %= (uint16_t)(RTL_RX_BUF_SIZE - 16);
            W16(RTL_CAPR, (uint16_t)(rtl.rx_cur - 16));
        }
    }

    if (status & RTL_ISR_TER) {
        serial_puts("[RTL] TX error\n");
    }
}

/* ---- Transmit ---- */

static int rtl8139_send(net_device_t *dev, const void *data, size_t len)
{
    (void)dev;
    if (len > RTL_TX_BUF_SIZE) return -1;

    uint32_t desc = rtl.tx_cur % RTL_TX_DESCS;

    /* Wait for OWN bit — previous TX done */
    uint32_t timeout = 1000000;
    while (!(R32((uint8_t)(RTL_TSD0 + desc * 4)) & (1 << 13)) && timeout--);
    if (!timeout) return -1;

    memcpy(rtl.tx_buf[desc], data, len);

    /* Physical address of TX buffer */
    uint64_t phys = (uint64_t)(uintptr_t)rtl.tx_buf[desc];
    /* RTL8139 requires 32-bit physical addresses */
    W32((uint8_t)(RTL_TSAD0 + desc * 4), (uint32_t)(phys & 0xFFFFFFFF));
    /* Set size and clear OWN bit to start TX */
    W32((uint8_t)(RTL_TSD0  + desc * 4), (uint32_t)((len < 60 ? 60 : len) & 0x1FFF));

    rtl.tx_cur++;
    return 0;
}

/* ---- Public: rtl8139_init ---- */

bool rtl8139_init(void)
{
    pci_device_t *pdev = pci_find_device(RTL_VENDOR, RTL_DEVICE);
    if (!pdev) { serial_puts("[RTL] no RTL8139 found\n"); return false; }

    serial_printf("[RTL8139] found at %02x:%02x.%u IRQ=%u\n",
                  pdev->bus, pdev->device, pdev->function, pdev->irq_line);

    pci_enable_bus_master(pdev);
    pci_enable_io_space(pdev);

    /* BAR0 is I/O space */
    rtl.io_base = (uint16_t)(pdev->bar[0] & ~0x3U);

    /* Allocate RX buffer (physically contiguous needed for RTL DMA) */
    rtl.rx_buf = (uint8_t *)kzalloc(RTL_RX_BUF_SIZE);
    if (!rtl.rx_buf) return false;

    /* Allocate TX buffers */
    for (int i = 0; i < RTL_TX_DESCS; i++) {
        rtl.tx_buf[i] = (uint8_t *)kzalloc(RTL_TX_BUF_SIZE);
        if (!rtl.tx_buf[i]) return false;
    }

    /* Software reset */
    W8(RTL_CR, RTL_CR_RST);
    uint32_t t = 100000;
    while ((R8(RTL_CR) & RTL_CR_RST) && t--);

    /* Unlock config registers */
    W8(RTL_9346CR, 0xC0);

    /* Read MAC address */
    for (int i = 0; i < ETH_ALEN; i++)
        rtl.netdev.mac[i] = R8((uint8_t)(RTL_IDR0 + i));

    /* Set RX buffer */
    uint64_t rx_phys = (uint64_t)(uintptr_t)rtl.rx_buf;
    W32(RTL_RBSTART, (uint32_t)(rx_phys & 0xFFFFFFFF));

    /* RCR: accept broadcast + multicast + unicast + promiscuous, 64K+16 wrap */
    W32(RTL_RCR, 0x0000F40F);  /* AB | AM | APM | AAP | wrap | RBLEN=11(64K) */

    /* IMR: enable RX OK and TX error */
    W16(RTL_IMR, RTL_ISR_ROK | RTL_ISR_TOK | RTL_ISR_TER | RTL_ISR_RER);

    /* Lock config registers */
    W8(RTL_9346CR, 0x00);

    /* Enable TX + RX */
    W8(RTL_CR, RTL_CR_RE | RTL_CR_TE);

    rtl.rx_cur = 0;
    rtl.tx_cur = 0;

    /* Register IRQ */
    irq_register(pdev->irq_line, rtl8139_irq);

    /* Set up netdev */
    memcpy(rtl.netdev.name, "eth0", 5);
    rtl.netdev.up      = true;
    rtl.netdev.private = &rtl;
    rtl.netdev.send    = rtl8139_send;
    rtl.netdev.poll    = NULL;

    netdev_register(&rtl.netdev);

    serial_printf("[RTL8139] init OK MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                  rtl.netdev.mac[0], rtl.netdev.mac[1], rtl.netdev.mac[2],
                  rtl.netdev.mac[3], rtl.netdev.mac[4], rtl.netdev.mac[5]);
    return true;
}
