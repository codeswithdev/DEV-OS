/* DevOS — Ethernet frame layer */
#pragma once
#include "../../include/types.h"

#define ETH_TYPE_ARP    0x0806
#define ETH_TYPE_IPV4   0x0800
#define ETH_TYPE_IPV6   0x86DD

#define ETH_ALEN        6
#define ETH_HEADER_LEN  14

typedef struct {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t ethertype;   /* network byte order */
    uint8_t  payload[];
} PACKED eth_frame_t;

/* Byte-order helpers */
static inline uint16_t htons(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }
static inline uint32_t htonl(uint32_t v)
{
    return ((v & 0xFF) << 24) | (((v >> 8) & 0xFF) << 16) |
           (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
}
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }

void eth_rx(void *dev, const void *frame, size_t len);
int  eth_tx(void *dev, const uint8_t *dst, uint16_t ethertype,
            const void *payload, size_t payload_len);

extern const uint8_t eth_broadcast[ETH_ALEN];
