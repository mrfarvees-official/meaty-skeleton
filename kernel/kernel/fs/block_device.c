#include <kernel/block_device.h>

bool block_device_valid(const block_device_t *device)
{
    if (device == NULL) return false;

    if (device->sector_size == 0) return false;

    if (device->sector_count == 0) return false;

    if (device->read == NULL) return false;

    return true;
}

int block_read(block_device_t *device, uint64_t lba, uint32_t count, void *buffer)
{
    if (device == NULL || buffer == NULL || count == 0) return -1;

    if (!block_device_valid(device)) return -1;

    if (lba >= device->sector_count) return -1;

    if ((uint64_t)count > device->sector_count - lba) return -1;

    return device->read(device, lba, count, buffer);
}

int block_write(block_device_t *device, uint64_t lba, uint32_t count, const void *buffer)
{
    if (device == NULL || buffer == NULL || count == 0) return -1;

    if (!block_device_valid(device)) return -1;

    if (device->write == NULL) return -1;

    if (lba >= device->sector_count) return -1;

    if ((uint64_t)count > device->sector_count - lba) return -1;

    return device->write(device, lba, count, buffer);
}