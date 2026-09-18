#include "icmp.h"
#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"
#include "../../arch/x86_64/serial.h"
#include "../../mm/heap.h"
#include "../../lib/string.h"

void icmp_rx(void *dev, const ipv4_header_t *ip_hdr,
             const void *data, size_t len)
{
    if (len < sizeof(icmp_header_t)) return;
    const icmp_header_t *icmp = (const icmp_header_t *)data;

    /* Validate checksum */
    if (ipv4_checksum(data, len) != 0) {
        serial_puts("[ICMP] bad checksum\n");
        return;
    }

    if (icmp->type == ICMP_ECHO_REQUEST) {
        serial_printf("[ICMP] ping from %u.%u.%u.%u seq=%u\n",
                      (ntohl(ip_hdr->src) >> 24) & 0xFF,
                      (ntohl(ip_hdr->src) >> 16) & 0xFF,
                      (ntohl(ip_hdr->src) >> 8)  & 0xFF,
                       ntohl(ip_hdr->src)         & 0xFF,
                      ntohs(icmp->sequence));

        /* Build ICMP echo reply */
        size_t   reply_len = len;
        uint8_t *reply     = (uint8_t *)kmalloc(reply_len);
        if (!reply) return;

        memcpy(reply, data, len);
        icmp_header_t *r = (icmp_header_t *)reply;
        r->type     = ICMP_ECHO_REPLY;
        r->checksum = 0;
        r->checksum = ipv4_checksum(reply, reply_len);

        ipv4_send(dev, ntohl(ip_hdr->src), IP_PROTO_ICMP, reply, reply_len);
        kfree(reply);
    }
}
