#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "driver/device.h"

typedef struct _Device {
	DeviceType	type;
	void*		driver;
	void*		device;
	int		id;
} Device;

Device* device_register(DeviceType type, void* driver, void* device, void* data);
bool device_deregister(int id);
int device_count(DeviceType type);
Device* device_get(DeviceType type, int idx);
int device_module_init();

#endif /* __DEVICE_H__ */
