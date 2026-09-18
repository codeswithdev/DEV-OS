#include "ethernet.h"
#include "netdev.h"
#include "arp.h"
#include "ipv4.h"
#include "../../arch/x86_64/serial.h"
#include "../../mm/heap.h"
#include "../../lib/string.h"

const uint8_t eth_broadcast[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* Called by netdev when a packet arrives */
void eth_rx(void *dev_ptr, const void *frame, size_t len)
{
    net_device_t *dev = (net_device_t *)dev_ptr;
    if (len < ETH_HEADER_LEN) return;

    const eth_frame_t *eth = (const eth_frame_t *)frame;
    uint16_t ethertype = ntohs(eth->ethertype);

    switch (ethertype) {
    case ETH_TYPE_ARP:
        arp_rx(dev, eth->payload, len - ETH_HEADER_LEN);
        break;
    case ETH_TYPE_IPV4:
        ipv4_rx(dev, eth->payload, len - ETH_HEADER_LEN);
        break;
    default:
        /* Unknown/unsupported */
        break;
    }
}

int eth_tx(void *dev_ptr, const uint8_t *dst, uint16_t ethertype,
           const void *payload, size_t payload_len)
{
    net_device_t *dev = (net_device_t *)dev_ptr;
    if (!dev || !dev->send) return -1;

    size_t total = ETH_HEADER_LEN + payload_len;
    uint8_t *frame = (uint8_t *)kmalloc(total < 64 ? 64 : total);
    if (!frame) return -1;

    eth_frame_t *hdr = (eth_frame_t *)frame;
    memcpy(hdr->dst, dst, ETH_ALEN);
    memcpy(hdr->src, dev->mac, ETH_ALEN);
    hdr->ethertype = htons(ethertype);
    memcpy(hdr->payload, payload, payload_len);

    /* Pad to minimum 60 bytes (+ 4 bytes FCS added by NIC) */
    if (total < 60) { memset(frame + total, 0, 60 - total); total = 60; }

    int r = dev->send(dev, frame, total);
    kfree(frame);
    return r;
}
