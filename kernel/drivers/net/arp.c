#include "arp.h"
#include "ethernet.h"
#include "netdev.h"
#include "../../arch/x86_64/serial.h"
#include "../../lib/string.h"

#define ARP_HW_ETH      1
#define ARP_PROTO_IPV4  0x0800
#define ARP_REQUEST     1
#define ARP_REPLY       2
#define ARP_CACHE_SIZE  32

typedef struct {
    uint16_t hw_type;       /* 1 = Ethernet */
    uint16_t proto_type;    /* 0x0800 = IPv4 */
    uint8_t  hw_len;        /* 6 */
    uint8_t  proto_len;     /* 4 */
    uint16_t operation;     /* 1=request 2=reply */
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} PACKED arp_packet_t;

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    bool     valid;
} arp_entry_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static uint32_t    my_ip = 0;

void arp_init(uint32_t ip)
{
    my_ip = ip;
    memset(arp_cache, 0, sizeof(arp_cache));
    serial_printf("[ARP] init IP=%u.%u.%u.%u\n",
                  (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                  (ip >> 8) & 0xFF, ip & 0xFF);
}

static void arp_cache_update(uint32_t ip, const uint8_t *mac)
{
    /* Update existing or find free slot */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip    = ip;
            arp_cache[i].valid = true;
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
}

void arp_rx(void *dev, const void *data, size_t len)
{
    if (len < sizeof(arp_packet_t)) return;
    const arp_packet_t *pkt = (const arp_packet_t *)data;

    uint16_t op = ntohs(pkt->operation);
    uint32_t sender_ip = ntohl(pkt->sender_ip);
    uint32_t target_ip = ntohl(pkt->target_ip);

    /* Update our cache with sender's info */
    arp_cache_update(sender_ip, pkt->sender_mac);

    if (op == ARP_REQUEST && target_ip == ntohl(my_ip)) {
        /* Someone is looking for us — send reply */
        arp_packet_t reply;
        reply.hw_type    = htons(ARP_HW_ETH);
        reply.proto_type = htons(ARP_PROTO_IPV4);
        reply.hw_len     = 6;
        reply.proto_len  = 4;
        reply.operation  = htons(ARP_REPLY);
        memcpy(reply.sender_mac, ((net_device_t *)dev)->mac, 6);
        reply.sender_ip = my_ip;
        memcpy(reply.target_mac, pkt->sender_mac, 6);
        reply.target_ip = pkt->sender_ip;
        eth_tx(dev, pkt->sender_mac, ETH_TYPE_ARP, &reply, sizeof(reply));
    }
}

int arp_resolve(void *dev, uint32_t ip, uint8_t *mac_out)
{
    /* Check cache */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac_out, arp_cache[i].mac, 6);
            return 0;
        }
    }

    /* Send ARP request */
    arp_packet_t req;
    req.hw_type    = htons(ARP_HW_ETH);
    req.proto_type = htons(ARP_PROTO_IPV4);
    req.hw_len     = 6;
    req.proto_len  = 4;
    req.operation  = htons(ARP_REQUEST);
    memcpy(req.sender_mac, ((net_device_t *)dev)->mac, 6);
    req.sender_ip = my_ip;
    memset(req.target_mac, 0, 6);
    req.target_ip = htonl(ip);
    eth_tx(dev, eth_broadcast, ETH_TYPE_ARP, &req, sizeof(req));

    return -1;  /* Not resolved yet; caller should retry */
}
