/*---------------------------------------------------------*\
| LenovoGPUPlugin.h                                         |
|                                                           |
|   OpenRGB plugin for Lenovo OEM NVIDIA GPU lighting.      |
|   Targets plugin API 4 (OpenRGB 1.0rc3 / 1.0rc3.1).       |
|                                                           |
|   NOTE: API 5 (post-rc3 master) changed the interface IID  |
|   and moved to virtual controllers. This will not load     |
|   into an API 5 build and vice versa.                      |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QtPlugin>
#include <vector>

#include "OpenRGBPluginInterface.h"
#include "ResourceManagerInterface.h"
#include "LenovoGPUNvAPI.h"
#include "LenovoGPUController.h"
#include "RGBController_LenovoGPU.h"
#include "RGBController_LenovoGPULogo.h"

/*---------------------------------------------------------*\
| Card identity. Gated on the SUBSYSTEM id as well as the    |
| device id, so this cannot fire on a non-Lenovo 5070 Ti.    |
\*---------------------------------------------------------*/
#define LENOVO_GPU_PCI_VENDOR   0x10DE
#define LENOVO_GPU_PCI_DEVICE   0x2C05
#define LENOVO_GPU_SUB_VENDOR   0x17AA
#define LENOVO_GPU_SUB_DEVICE   0xC773

class LenovoGPUPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID)
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    LenovoGPUPlugin();
    ~LenovoGPUPlugin();

    OpenRGBPluginInfo   GetPluginInfo() override;
    unsigned int        GetPluginAPIVersion() override;

    void                Load(ResourceManagerInterface* resource_manager_ptr) override;
    QWidget*            GetWidget() override;
    QMenu*              GetTrayMenu() override;
    void                Unload() override;

private:
    ResourceManagerInterface*                   resource_manager;
    LenovoGPUNvAPI*                             nvapi;

    /*-----------------------------------------------------*\
    | The plugin owns the hardware controllers because each   |
    | one is shared between a strip device and a logo device. |
    \*-----------------------------------------------------*/
    std::vector<LenovoGPUController*>           hw;
    std::vector<RGBController*>                 controllers;
};
