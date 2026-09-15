/*---------------------------------------------------------*\
| LenovoGPUNvAPI.cpp                                        |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "LenovoGPUNvAPI.h"

#include <windows.h>
#include <cstring>

/*---------------------------------------------------------*\
| NvAPI function ids, from NVIDIA/nvapi nvapi_interface.h.   |
| nvapi64.dll exports only nvapi_QueryInterface; everything  |
| else is resolved by id through it.                         |
\*---------------------------------------------------------*/
static const uint32_t ID_INITIALIZE         = 0x0150E828;
static const uint32_t ID_UNLOAD             = 0xD22BDD7E;
static const uint32_t ID_ENUM_PHYSICAL_GPUS = 0xE5AC921F;
static const uint32_t ID_GPU_GET_PCI_IDS    = 0x2DDFB66E;
static const uint32_t ID_I2C_WRITE          = 0xE812EB07;

/*---------------------------------------------------------*\
| NV_I2C_INFO_V3, exactly as captured from Lenovo's          |
| SmartEngineHost64.exe. Struct size 0x40, version field     |
| 0x00030040.                                                |
\*---------------------------------------------------------*/
#define NV_I2C_INFO_V3_SIZE     0x40
#define NV_I2C_INFO_V3_VERSION  (NV_I2C_INFO_V3_SIZE | (3 << 16))

#define OFF_VERSION       0x00
#define OFF_DISPLAYMASK   0x04
#define OFF_ISDDCPORT     0x08
#define OFF_DEVADDR       0x09
#define OFF_PREGADDR      0x10
#define OFF_REGADDRSIZE   0x18
#define OFF_PDATA         0x20
#define OFF_CBSIZE        0x28
#define OFF_SPEED         0x2C
#define OFF_SPEEDKHZ      0x30
#define OFF_PORTID        0x34
#define OFF_ISPORTIDSET   0x38

/*---------------------------------------------------------*\
| displayMask is IGNORED once bIsPortIdSet/portId are set -   |
| verified by issuing colour writes with deliberately wrong   |
| masks (0x1, 0x4), which applied normally. The captured      |
| 0x100 is kept only for fidelity with Lenovo's traffic.      |
\*---------------------------------------------------------*/
#define LENOVO_DISPLAY_MASK  0x00000100
#define LENOVO_IS_DDC_PORT   1
#define LENOVO_PORT_ID       1
#define LENOVO_SPEED_KHZ     4

typedef void*   (*QueryInterface_t)(uint32_t);
typedef int32_t (*NvAPI_Initialize_t)();
typedef int32_t (*NvAPI_Unload_t)();
typedef int32_t (*NvAPI_EnumPhysicalGPUs_t)(void**, int32_t*);
typedef int32_t (*NvAPI_GPU_GetPCIIdentifiers_t)(void*, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
typedef int32_t (*NvAPI_I2CWrite_t)(void*, void*);

LenovoGPUNvAPI::LenovoGPUNvAPI()
{
    initialized     = false;
    nvapi_module    = NULL;
    query_interface = NULL;
    fn_i2c_write    = NULL;
    fn_enum_gpus    = NULL;
    fn_get_pci_ids  = NULL;

    HMODULE mod = LoadLibraryA("nvapi64.dll");
    if(mod == NULL)
    {
        return;
    }
    nvapi_module = (void*)mod;

    FARPROC qi = GetProcAddress(mod, "nvapi_QueryInterface");
    if(qi == NULL)
    {
        return;
    }
    query_interface = (void*)qi;

    NvAPI_Initialize_t init = (NvAPI_Initialize_t)QueryInterface(ID_INITIALIZE);
    if(init == NULL || init() != 0)
    {
        return;
    }

    fn_enum_gpus   = QueryInterface(ID_ENUM_PHYSICAL_GPUS);
    fn_get_pci_ids = QueryInterface(ID_GPU_GET_PCI_IDS);
    fn_i2c_write   = QueryInterface(ID_I2C_WRITE);

    initialized = (fn_enum_gpus != NULL && fn_get_pci_ids != NULL && fn_i2c_write != NULL);
}

LenovoGPUNvAPI::~LenovoGPUNvAPI()
{
    if(query_interface != NULL)
    {
        NvAPI_Unload_t unload = (NvAPI_Unload_t)QueryInterface(ID_UNLOAD);
        if(unload != NULL)
        {
            unload();
        }
    }

    if(nvapi_module != NULL)
    {
        FreeLibrary((HMODULE)nvapi_module);
    }
}

void* LenovoGPUNvAPI::QueryInterface(uint32_t function_id)
{
    if(query_interface == NULL)
    {
        return(NULL);
    }
    return(((QueryInterface_t)query_interface)(function_id));
}

std::vector<void*> LenovoGPUNvAPI::FindGPUs(uint16_t pci_vendor, uint16_t pci_device,
                                            uint16_t sub_vendor, uint16_t sub_device)
{
    std::vector<void*> matches;

    if(!initialized)
    {
        return(matches);
    }

    void*   handles[64];
    int32_t count = 0;

    if(((NvAPI_EnumPhysicalGPUs_t)fn_enum_gpus)(handles, &count) != 0)
    {
        return(matches);
    }

    for(int32_t i = 0; i < count; i++)
    {
        uint32_t device_id = 0, sub_system_id = 0, revision_id = 0, ext_device_id = 0;

        if(((NvAPI_GPU_GetPCIIdentifiers_t)fn_get_pci_ids)(handles[i], &device_id,
                                                           &sub_system_id, &revision_id,
                                                           &ext_device_id) != 0)
        {
            continue;
        }

        /*-------------------------------------------------*\
        | device_id packs device in the high half, vendor in |
        | the low half; sub_system_id likewise.              |
        \*-------------------------------------------------*/
        uint16_t dev    = (uint16_t)(device_id     >> 16);
        uint16_t ven    = (uint16_t)(device_id     & 0xFFFF);
        uint16_t subdev = (uint16_t)(sub_system_id >> 16);
        uint16_t subven = (uint16_t)(sub_system_id & 0xFFFF);

        if(ven == pci_vendor && dev == pci_device && subven == sub_vendor && subdev == sub_device)
        {
            matches.push_back(handles[i]);
        }
    }

    return(matches);
}

bool LenovoGPUNvAPI::WriteRegister(void* gpu, uint8_t reg, uint8_t value)
{
    if(!initialized || gpu == NULL)
    {
        return(false);
    }

    uint8_t reg_buf  = reg;
    uint8_t data_buf = value;
    uint8_t info[NV_I2C_INFO_V3_SIZE];

    memset(info, 0, sizeof(info));

    uint32_t  version      = NV_I2C_INFO_V3_VERSION;
    uint32_t  display_mask = LENOVO_DISPLAY_MASK;
    uint32_t  one          = 1;
    uint32_t  speed        = 0xFFFF;
    uint32_t  speed_khz    = LENOVO_SPEED_KHZ;
    uint32_t  port_id      = LENOVO_PORT_ID;
    uint8_t*  p_reg        = &reg_buf;
    uint8_t*  p_data       = &data_buf;

    memcpy(&info[OFF_VERSION],     &version,      4);
    memcpy(&info[OFF_DISPLAYMASK], &display_mask, 4);
    info[OFF_ISDDCPORT] = LENOVO_IS_DDC_PORT;
    info[OFF_DEVADDR]   = 0x92;                 /* 0x49 shifted to 8-bit form */
    memcpy(&info[OFF_PREGADDR],     &p_reg,     sizeof(p_reg));
    memcpy(&info[OFF_REGADDRSIZE],  &one,       4);
    memcpy(&info[OFF_PDATA],        &p_data,    sizeof(p_data));
    memcpy(&info[OFF_CBSIZE],       &one,       4);
    memcpy(&info[OFF_SPEED],        &speed,     4);
    memcpy(&info[OFF_SPEEDKHZ],     &speed_khz, 4);
    memcpy(&info[OFF_PORTID],       &port_id,   4);
    memcpy(&info[OFF_ISPORTIDSET],  &one,       4);

    return(((NvAPI_I2CWrite_t)fn_i2c_write)(gpu, info) == 0);
}
