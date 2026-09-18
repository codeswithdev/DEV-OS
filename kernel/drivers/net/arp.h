/* DevOS — ARP implementation */
#pragma once
#include "../../include/types.h"

void arp_init(uint32_t ip);    /* register our IP address */
void arp_rx(void *dev, const void *data, size_t len);
int  arp_resolve(void *dev, uint32_t ip, uint8_t *mac_out);
