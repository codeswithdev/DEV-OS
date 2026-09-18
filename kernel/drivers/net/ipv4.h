/* DevOS — IPv4 */
#pragma once
#include "../../include/types.h"

#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

typedef struct {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
    uint8_t  payload[];
} PACKED ipv4_header_t;

void ipv4_init(uint32_t my_ip);
void ipv4_rx(void *dev, const void *data, size_t len);
int  ipv4_send(void *dev, uint32_t dst_ip, uint8_t proto,
               const void *payload, size_t len);
uint16_t ipv4_checksum(const void *data, size_t len);
