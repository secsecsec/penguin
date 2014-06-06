#include <string.h>
#include <util/list.h>
#include "device.h"
#include "port.h"
#include "shared.h"
#include "gmalloc.h"

#include "pci.h"

#define PCI_CONFIG_ADDRESS	0xcf8
#define PCI_CONFIG_DATA		0xcfc

#define INVALID_VENDOR		0xffff

#define MAX_BUS		256
#define MAX_SLOT	32
#define MAX_FUNCTION	8

int pci_cache_line_size;

static uint8_t _pci_read8(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg);
static void _pci_write8(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg, uint8_t data);
static uint16_t _pci_read16(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg);
static void _pci_write16(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg, uint16_t data);
static uint32_t _pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg);
static void _pci_write32(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg, uint32_t data);

static uint32_t pci_count() {
	uint32_t count = 0;
	
	uint32_t bus, slot, function;
	for(bus = 0; bus < MAX_BUS; bus++) {
		for(slot = 0; slot < MAX_SLOT; slot++) {
			for(function = 0; function < MAX_FUNCTION; function++) {
				uint16_t vendor_id = _pci_read16(bus, slot, function, PCI_VENDOR_ID);
				if(vendor_id == INVALID_VENDOR)
					goto next_slot;
				
				uint8_t header_type = _pci_read8(bus, slot, function, PCI_HEADER_TYPE);
				
				count++;
				
				if(!(header_type & 0x80))
					break;
			}
		}
next_slot:
		;
	}
	
	return count;
}

static void pci_analyze() {
	uint32_t count = 0;
	uint32_t bus, slot, function;
	for(bus = 0; bus < MAX_BUS; bus++) {
		for(slot = 0; slot < MAX_SLOT; slot++) {
			for(function = 0; function < MAX_FUNCTION; function++) {
				//uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (function << 8) | 0;
				
				uint16_t vendor_id = _pci_read16(bus, slot, function, PCI_VENDOR_ID);
				if(vendor_id == INVALID_VENDOR)
					goto next_slot;
				
				uint16_t device_id = _pci_read16(bus, slot, function, PCI_DEVICE_ID);
				uint8_t header_type = _pci_read8(bus, slot, function, PCI_HEADER_TYPE);
				
				PCI_Device* device = &shared->pci_devices[count];
				bzero(device, sizeof(PCI_Device));
				device->vendor_id = vendor_id;
				device->device_id = device_id;
				device->bus = bus;
				device->slot = slot;
				device->function = function;
				
				// ioaddr, membase
				for(int reg = PCI_BASE_ADDRESS_0; reg <= PCI_BASE_ADDRESS_5; reg += 4) {
					uint32_t bar = pci_read32(device, reg);
					
					if(bar & PCI_BASE_ADDRESS_SPACE_IO) {
						if(device->ioaddr == 0)
							device->ioaddr = bar & PCI_BASE_ADDRESS_IO_MASK;
					} else {
						if(device->membase == NULL) {
							if(bar & PCI_BASE_ADDRESS_MEM_TYPE_64) {
								reg += 4;
								uint32_t bar2 = pci_read32(device, reg);
								device->membase = (void*)(((uint64_t)bar2 << 32) | ((uint64_t)(bar & PCI_BASE_ADDRESS_MEM_MASK)));
							} else {
								device->membase = (void*)(uint64_t)(bar & PCI_BASE_ADDRESS_MEM_MASK);
							}
						}
					}
				}
				
				device->irq = pci_read8(device, PCI_INTERRUPT_LINE);
				
				// capabilities
				if(pci_read16(device, PCI_STATUS) & PCI_STATUS_CAP_LIST) {
					int reg = PCI_CAPABILITY_LIST;
					if(pci_read8(device, PCI_HEADER_TYPE) == PCI_HEADER_TYPE_CARDBUS)
						reg = PCI_CB_CAPABILITY_LIST;
					
					for(int i = 0; i < 48; i++) { // TTL is 48 (from Linux)
						reg = pci_read8(device, reg);
						if(reg < 0x40)
							break;
						
						reg &= ~3;
						
						uint8_t id = pci_read8(device, reg + PCI_CAP_LIST_ID);
						if(id == 0xff)
							break;
						
						if(id <= PCI_CAP_ID_MAX)
							device->capabilities[id] = reg;
						
						reg += PCI_CAP_LIST_NEXT;
					}
				}
				
				device->priv = NULL;
				
				count++;
				
				if(!(header_type & 0x80))
					break;
			}
		}
next_slot:
		;
	}
}

#include <stdio.h>
void pci_init() {
	uint32_t count = pci_count();
	shared->pci_devices = gmalloc(sizeof(PCI_Device) * count);
	shared->pci_devices_count = count;
	
	pci_analyze();
	/*
	int i;
	for(i = 0; i < shared->pci_devices_count; i++) {
		PCI_Device* device = &shared->pci_devices[i];
		printf("Vendor ID: %x, Device ID: %x\n", device->vendor_id, device->device_id);
		printf("Bus: %d, Slot: %d, Function: %d\n", device->bus, device->slot, device->function);
		
		printf("Command: %x, Status: %x\n", pci_read16(device, PCI_COMMAND), pci_read16(device, PCI_STATUS));
		printf("Revision ID: %x, Prog IF: %x, Subclass: %x, Class code: %x\n", pci_read8(device, PCI_REVISION_ID), pci_read8(device, PCI_PROG_IF), pci_read8(device, PCI_SUB_CLASS), pci_read8(device, PCI_CLASS_CODE));
		printf("Subsystem Vendor ID: %x, Subsystem ID: %x\n", pci_read16(device, PCI_SUBSYSTEM_VENDOR_ID), pci_read16(device, PCI_SUBSYSTEM_ID));
	}
	*/
}

uint32_t pci_device_size(uint16_t vendor_id, uint16_t device_id) {
	uint32_t count = 0;
	
	uint32_t devices_count = shared->pci_devices_count;
	uint32_t i;
	for(i = 0; i < devices_count; i++) {
		PCI_Device* device = &shared->pci_devices[i];
		if(device->vendor_id == vendor_id && device->device_id == device_id)
			count++;
	}
	
	return count;
}

PCI_Device* pci_device(uint16_t vendor_id, uint16_t device_id, uint32_t index) {
	uint32_t devices_count = shared->pci_devices_count;
	uint32_t i;
	for(i = 0; i < devices_count; i++) {
		PCI_Device* device = &shared->pci_devices[i];
		if(device->vendor_id == vendor_id && device->device_id == device_id && index-- == 0)
			return device;
	}
	
	return NULL;
}

bool pci_enable(PCI_Device* device) {
	bool changed = false;
	
	uint16_t enable = PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	uint16_t command = pci_read16(device, PCI_COMMAND);
	if((command & enable) != enable) {
		command |= enable;
		pci_write16(device, PCI_COMMAND, command);
		changed = true;
	}
	
	uint8_t latency_timer = pci_read8(device, PCI_LATENCY_TIMER);
	if(latency_timer < 32) {
		pci_write8(device, PCI_LATENCY_TIMER, 32);
		changed = true;
	}
	
	return changed;
}

bool pci_power_set(PCI_Device* device) {
	return false;
}

uint8_t pci_read8(PCI_Device* device, uint32_t reg) {
	return _pci_read8(device->bus, device->slot, device->function, reg);
}

static uint8_t _pci_read8(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg) {
	uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (function << 8) | (reg & 0xfc);
	port_out32(PCI_CONFIG_ADDRESS, address);
	return (uint8_t)(port_in32(PCI_CONFIG_DATA) >> (reg & 0x3) * 8);
}

void pci_write8(PCI_Device* device, uint32_t reg, uint8_t data) {
	return _pci_write8(device->bus, device->slot, device->function, reg, data);
}

static void _pci_write8(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg, uint8_t data) {
	uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (function << 8) | (reg & 0xfc);
	port_out32(PCI_CONFIG_ADDRESS, address);
	port_out32(PCI_CONFIG_DATA, port_in32(PCI_CONFIG_DATA) | ((uint32_t)data) << ((reg & 0x03) * 8));
}

uint16_t pci_read16(PCI_Device* device, uint32_t reg) {
	return _pci_read16(device->bus, device->slot, device->function, reg);
}

static uint16_t _pci_read16(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg) {
	uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (function << 8) | (reg & 0xfc);
	port_out32(PCI_CONFIG_ADDRESS, address);
	return (uint16_t)(port_in32(PCI_CONFIG_DATA) >> (reg & 0x3) * 8);
}

void pci_write16(PCI_Device* device, uint32_t reg, uint16_t data) {
	return _pci_write16(device->bus, device->slot, device->function, reg, data);
}

static void _pci_write16(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg, uint16_t data) {
	uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (function << 8) | (reg & 0xfc);
	port_out32(PCI_CONFIG_ADDRESS, address);
	port_out32(PCI_CONFIG_DATA, port_in32(PCI_CONFIG_DATA) | ((uint32_t)data) << ((reg & 0x03) * 8));
}

uint32_t pci_read32(PCI_Device* device, uint32_t reg) {
	return _pci_read32(device->bus, device->slot, device->function, reg);
}

static uint32_t _pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg) {
	uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (function << 8) | reg;
	port_out32(PCI_CONFIG_ADDRESS, address);
	return port_in32(PCI_CONFIG_DATA);
}

void pci_write32(PCI_Device* device, uint32_t reg, uint32_t data) {
	return _pci_write32(device->bus, device->slot, device->function, reg, data);
}

static void _pci_write32(uint8_t bus, uint8_t slot, uint8_t function, uint32_t reg, uint32_t data) {
	uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (function << 8) | reg;
	port_out32(PCI_CONFIG_ADDRESS, address);
	port_out32(PCI_CONFIG_DATA, data);
}

int pci_probe(DeviceType type, PCI_ID* ids, void* driver) {
	int count = 0;
	
	for(int i = 0; i < shared->pci_devices_count; i++) {
		PCI_Device* pci = &shared->pci_devices[i];
		if(pci->name)
			continue;
		
		// Try to probe
		for(int j = 0; ids[j].vendor_id != 0 && ids[j].device_id != 0; j++) {
			PCI_ID* id = &ids[j];
			if(pci->vendor_id == id->vendor_id && pci->device_id == id->device_id) {
				if(device_register(type, driver, pci, id->data))
					count++;
			}
		}
	}
	
	return count;
}
