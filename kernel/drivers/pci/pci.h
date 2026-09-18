/*
 * DevOS — PCI bus driver (pci.h)
 * Configuration-space access via I/O ports 0xCF8/0xCFC (PCI CAM)
 */
#pragma once
#include "../../include/types.h"

/* PCI device descriptor */
typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;

    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  header_type;
    uint8_t  revision;

    uint8_t  irq_line;
    uint8_t  irq_pin;

    uint32_t bar[6];        /* raw BAR values */
} pci_device_t;

#define PCI_MAX_DEVICES 256

extern pci_device_t pci_devices[];
extern int          pci_device_count;

void          pci_init(void);
void          pci_scan(void);
pci_device_t *pci_find_device(uint16_t vendor, uint16_t device_id);
pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass);
pci_device_t *pci_find_class_progif(uint8_t class_code, uint8_t subclass, uint8_t prog_if);

/* Config-space I/O */
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
uint8_t  pci_read8 (uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val);
void     pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint16_t val);

/* Enable bus-mastering DMA for a device */
void pci_enable_bus_master(pci_device_t *dev);
void pci_enable_memory_space(pci_device_t *dev);
void pci_enable_io_space(pci_device_t *dev);

/* BAR decoding */
uint64_t pci_bar_address(pci_device_t *dev, int bar_index);
bool     pci_bar_is_mmio(pci_device_t *dev, int bar_index);
bool     pci_bar_is_64bit(pci_device_t *dev, int bar_index);

/* Map a MMIO BAR into kernel virtual space */
void *pci_map_bar(pci_device_t *dev, int bar_index);

/* Shell diagnostic */
void pci_print_devices(void);
