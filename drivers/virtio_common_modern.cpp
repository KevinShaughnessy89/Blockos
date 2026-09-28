/*
 * Modern VirtIO PCI transport is implemented in virtio_common.cpp.
 *
 * This translation unit is kept intentionally empty so the historical
 * standalone MMIO implementation cannot introduce duplicate symbols or
 * accidentally bypass the PCI capability based transport used by the server
 * profile.
 */
