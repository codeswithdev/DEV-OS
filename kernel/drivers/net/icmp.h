/* DevOS — ICMP */
#pragma once
#include "../../include/types.h"
#include "ipv4.h"

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
    uint8_t  payload[];
} PACKED icmp_header_t;

#define ICMP_ECHO_REQUEST  8
#define ICMP_ECHO_REPLY    0

void icmp_rx(void *dev, const ipv4_header_t *ip_hdr,
             const void *data, size_t len);
