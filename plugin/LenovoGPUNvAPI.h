/*---------------------------------------------------------*\
| LenovoGPUNvAPI.h                                          |
|                                                           |
|   Minimal, self-contained NvAPI I2C access.               |
|                                                           |
|   Deliberately does NOT use OpenRGB's i2c_smbus_nvapi.     |
|   That class hardcodes is_ddc_port = 0 in both of its      |
|   transfer functions, and this controller only answers     |
|   with is_ddc_port = 1. Doing our own NvAPI calls means    |
|   the plugin works against a stock OpenRGB build with no   |
|   core patch.                                              |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <cstdint>
#include <vector>

class LenovoGPUNvAPI
{
public:
    LenovoGPUNvAPI();
    ~LenovoGPUNvAPI();

    bool        Initialized() const { return initialized; }

    /*-----------------------------------------------------*\
    | Returns handles of GPUs matching the given PCI and     |
    | subsystem identifiers.                                 |
    \*-----------------------------------------------------*/
    std::vector<void*>  FindGPUs(uint16_t pci_vendor, uint16_t pci_device,
                                 uint16_t sub_vendor, uint16_t sub_device);

    /*-----------------------------------------------------*\
    | Single register byte + single data byte, the only      |
    | transaction shape this controller uses.                |
    \*-----------------------------------------------------*/
    bool        WriteRegister(void* gpu, uint8_t reg, uint8_t value);

private:
    void*       QueryInterface(uint32_t function_id);

    bool        initialized;
    void*       nvapi_module;
    void*       query_interface;
    void*       fn_i2c_write;
    void*       fn_enum_gpus;
    void*       fn_get_pci_ids;
};
