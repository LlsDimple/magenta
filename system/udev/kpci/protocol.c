// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/pci.h>

#include <assert.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>

#include "kpci-private.h"

static mx_status_t pci_claim_device(mx_device_t* dev) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_claim_device(device->handle);
}

static mx_status_t pci_enable_bus_master(mx_device_t* dev, bool enable) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_enable_bus_master(device->handle, enable);
}

static mx_status_t pci_enable_pio(mx_device_t* dev, bool enable) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_enable_pio(device->handle, enable);
}

static mx_status_t pci_reset_device(mx_device_t* dev) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_reset_device(device->handle);
}

static mx_status_t pci_map_resource(mx_device_t* dev,
                                mx_pci_resource_t *resource,
                                mx_cache_policy_t cache_policy,
                                void** vaddr) {
    mx_status_t status = NO_ERROR;

    if (!dev || !resource | !vaddr) {
        return ERR_INVALID_ARGS;
    }

    // 0 is valid, so the best that can be done is checking the mask
    if ((cache_policy & ~MX_CACHE_POLICY_MASK) != 0) {
        return ERR_INVALID_ARGS;
    }

    // TODO(cja): PIO may be mappable on non-x86 architectures
    if (resource->type == PCI_RESOURCE_TYPE_PIO) {
        return ERR_NOT_SUPPORTED;
    }

    // Set a policy if one hasn't already been set by the PCI bus driver
    if (cache_policy != MX_CACHE_POLICY_USE_EXISTING) {
        status = mx_vmo_set_cache_policy(resource->mmio_handle, cache_policy);
        if (status != NO_ERROR) {
            return status;
        }
    }

    status = mx_vmar_map(mx_vmar_root_self(), 0, resource->mmio_handle, 0, resource->size,
                           MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_MAP_RANGE,
                           (uintptr_t*)vaddr);
    if (status != NO_ERROR) {
        *vaddr = NULL;
    }

    return status;
}
static mx_status_t pci_map_interrupt(mx_device_t* dev, int which_irq, mx_handle_t* out_handle) {
    mx_status_t status = NO_ERROR;

    if (!dev || !out_handle) {
        return ERR_INVALID_ARGS;
    }

    kpci_device_t* device = get_kpci_device(dev);
    if (device->handle == MX_HANDLE_INVALID) {
        return ERR_BAD_HANDLE;
    }

    status = mx_pci_map_interrupt(device->handle, which_irq, out_handle);
    if (status != NO_ERROR) {
        *out_handle = MX_HANDLE_INVALID;
        return status;
    }

    return NO_ERROR;
}

// TODO(cja): Figure out how to handle passing PIO privileges to other
// processes in the future when PCI is moved out of the kernel into
// userspace.
static mx_status_t do_resource_bookkeeping(mx_pci_resource_t* res) {
    mx_status_t status;

    if (!res) {
        return ERR_INVALID_ARGS;
    }

    switch(res->type) {
        case PCI_RESOURCE_TYPE_PIO:
#if __x86_64__
            // x86 PIO space access requires permission in the I/O bitmap
            status = mx_mmap_device_io(get_root_resource(), res->pio_addr, res->size);
#else
            status = ERR_NOT_SUPPORTED;
#endif
            break;
        default:
            status = NO_ERROR;
    }

    return status;
}

static mx_status_t pci_get_bar(mx_device_t* dev, uint32_t bar_num, mx_pci_resource_t* out_bar) {
    mx_status_t status = NO_ERROR;

    if (!dev || !out_bar) {
        return ERR_INVALID_ARGS;
    }

    kpci_device_t* device = get_kpci_device(dev);
    if (device->handle == MX_HANDLE_INVALID) {
        return ERR_BAD_HANDLE;
    }

    status = mx_pci_get_bar(device->handle, bar_num, out_bar);
    if (status != NO_ERROR) {
        return status;
    }

    return do_resource_bookkeeping(out_bar);
}

static mx_status_t pci_get_config_ex(mx_device_t* dev, mx_pci_resource_t* out_config) {
    if (!dev || !out_config) {
        return ERR_INVALID_ARGS;
    }

    kpci_device_t* device = get_kpci_device(dev);
    if (device->handle == MX_HANDLE_INVALID) {
        return ERR_BAD_HANDLE;
    }

    mx_status_t status = mx_pci_get_config(device->handle, out_config);
    if (status != NO_ERROR) {
        return status;
    }

    return do_resource_bookkeeping(out_config);
}

static mx_status_t pci_query_irq_mode_caps(mx_device_t* dev,
                                           mx_pci_irq_mode_t mode,
                                           uint32_t* out_max_irqs) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_query_irq_mode_caps(device->handle, mode, out_max_irqs);
}

static mx_status_t pci_set_irq_mode(mx_device_t* dev,
                                    mx_pci_irq_mode_t mode,
                                    uint32_t requested_irq_count) {
    kpci_device_t* device = get_kpci_device(dev);
    assert(device->handle != MX_HANDLE_INVALID);
    return mx_pci_set_irq_mode(device->handle, mode, requested_irq_count);
}

static pci_protocol_t _pci_protocol = {
    .claim_device = pci_claim_device,
    .enable_bus_master = pci_enable_bus_master,
    .enable_pio = pci_enable_pio,
    .reset_device = pci_reset_device,
    .map_resource = pci_map_resource,
    .map_interrupt = pci_map_interrupt,
    .get_config_ex = pci_get_config_ex,
    .get_bar = pci_get_bar,
    .query_irq_mode_caps = pci_query_irq_mode_caps,
    .set_irq_mode = pci_set_irq_mode,
};
