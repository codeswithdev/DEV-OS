#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "../../arch/x86_64/serial.h"
#include "../../mm/heap.h"
#include "../../lib/string.h"

static uint32_t g_my_ip  = 0;
static uint16_t g_ip_id  = 1;

uint16_t ipv4_checksum(const void *data, size_t len)
{
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *ptr++; len -= 2; }
    if (len) sum += *(uint8_t *)ptr;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

void ipv4_init(uint32_t my_ip)
{
    g_my_ip = my_ip;
    arp_init(my_ip);
    serial_printf("[IPv4] init IP=%u.%u.%u.%u\n",
                  (my_ip >> 24) & 0xFF, (my_ip >> 16) & 0xFF,
                  (my_ip >> 8) & 0xFF,   my_ip & 0xFF);
}

void ipv4_rx(void *dev, const void *data, size_t len)
{
    if (len < sizeof(ipv4_header_t)) return;
    const ipv4_header_t *hdr = (const ipv4_header_t *)data;

    /* Validate header checksum */
    uint8_t ihl = (hdr->version_ihl & 0xF) * 4;
    if (ihl < 20 || ihl > (uint8_t)len) return;
    if (ipv4_checksum(hdr, ihl) != 0) return;

    /* Only process packets addressed to us */
    if (ntohl(hdr->dst) != g_my_ip) return;

    size_t payload_len = ntohs(hdr->total_length) - ihl;

    switch (hdr->protocol) {
    case IP_PROTO_ICMP:
        icmp_rx(dev, hdr, (const uint8_t *)data + ihl, payload_len);
        break;
    default:
        break;
    }
}

int ipv4_send(void *dev, uint32_t dst_ip, uint8_t proto,
              const void *payload, size_t payload_len)
{
    size_t total = sizeof(ipv4_header_t) + payload_len;
    uint8_t *pkt = (uint8_t *)kmalloc(total);
    if (!pkt) return -1;

    ipv4_header_t *hdr = (ipv4_header_t *)pkt;
    hdr->version_ihl     = 0x45;   /* IPv4, IHL=5 */
    hdr->tos             = 0;
    hdr->total_length    = htons((uint16_t)total);
    hdr->id              = htons(g_ip_id++);
    hdr->flags_fragment  = 0;
    hdr->ttl             = 64;
    hdr->protocol        = proto;
    hdr->checksum        = 0;
    hdr->src             = htonl(g_my_ip);
    hdr->dst             = htonl(dst_ip);
    hdr->checksum        = ipv4_checksum(hdr, sizeof(ipv4_header_t));

    memcpy(hdr->payload, payload, payload_len);

    /* Resolve MAC via ARP */
    uint8_t dst_mac[6];
    if (arp_resolve(dev, dst_ip, dst_mac) != 0) {
        /* ARP request was sent; packet dropped this time */
        kfree(pkt);
        return -1;
    }

    int r = eth_tx(dev, dst_mac, ETH_TYPE_IPV4, pkt, total);
    kfree(pkt);
    return r;
}
