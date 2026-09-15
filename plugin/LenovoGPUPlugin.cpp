/*---------------------------------------------------------*\
| LenovoGPUPlugin.cpp                                       |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "LenovoGPUPlugin.h"

#include <QLabel>

LenovoGPUPlugin::LenovoGPUPlugin()
{
    resource_manager = nullptr;
    nvapi            = nullptr;
}

LenovoGPUPlugin::~LenovoGPUPlugin()
{
}

OpenRGBPluginInfo LenovoGPUPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;

    info.Name        = "Lenovo Legion GPU";
    info.Description = "Lighting for Lenovo OEM NVIDIA cards whose controller sits "
                       "on the DDC side of the NvAPI I2C bus";
    info.Version     = "0.1.0";
    info.Commit      = "";
    info.URL         = "";
    info.Location    = OPENRGB_PLUGIN_LOCATION_INFORMATION;
    info.Label       = "Lenovo GPU";

    return(info);
}

unsigned int LenovoGPUPlugin::GetPluginAPIVersion()
{
    return(OPENRGB_PLUGIN_API_VERSION);
}

void LenovoGPUPlugin::Load(ResourceManagerInterface* resource_manager_ptr)
{
    resource_manager = resource_manager_ptr;

    nvapi = new LenovoGPUNvAPI();

    if(!nvapi->Initialized())
    {
        return;
    }

    std::vector<void*> handles = nvapi->FindGPUs(LENOVO_GPU_PCI_VENDOR, LENOVO_GPU_PCI_DEVICE,
                                                 LENOVO_GPU_SUB_VENDOR, LENOVO_GPU_SUB_DEVICE);

    for(unsigned int i = 0; i < handles.size(); i++)
    {
        LenovoGPUController* controller = new LenovoGPUController(nvapi, handles[i], i);
        hw.push_back(controller);

        /*-------------------------------------------------*\
        | Two devices per card: the RGB strip, and the logo  |
        | as an On/Off switch. Both drive the same           |
        | controller, which owns the logo state so a colour  |
        | burst cannot clobber it.                           |
        \*-------------------------------------------------*/
        RGBController_LenovoGPU*     strip = new RGBController_LenovoGPU(controller);
        RGBController_LenovoGPULogo* logo  = new RGBController_LenovoGPULogo(controller);

        controllers.push_back(strip);
        controllers.push_back(logo);

        if(resource_manager != nullptr)
        {
            resource_manager->RegisterRGBController(strip);
            resource_manager->RegisterRGBController(logo);
        }
    }
}

void LenovoGPUPlugin::Unload()
{
    for(unsigned int i = 0; i < controllers.size(); i++)
    {
        if(resource_manager != nullptr)
        {
            resource_manager->UnregisterRGBController(controllers[i]);
        }

        delete controllers[i];
    }

    controllers.clear();

    /*-----------------------------------------------------*\
    | Hardware controllers are deleted after the devices,    |
    | since two devices reference each one.                  |
    \*-----------------------------------------------------*/
    for(unsigned int i = 0; i < hw.size(); i++)
    {
        delete hw[i];
    }

    hw.clear();

    if(nvapi != nullptr)
    {
        delete nvapi;
        nvapi = nullptr;
    }
}

QWidget* LenovoGPUPlugin::GetWidget()
{
    QLabel* label = new QLabel();

    if(nvapi == nullptr || !nvapi->Initialized())
    {
        label->setText("NvAPI unavailable - an NVIDIA driver is required.");
    }
    else if(controllers.empty())
    {
        label->setText("No matching Lenovo OEM GPU found (10DE:2C05 / 17AA:C773).");
    }
    else
    {
        label->setText("Lenovo OEM GPU detected - exposed as two devices:\n\n"
                       "  \u2022 \"... Strip\"  - the RGB strip and backplate\n"
                       "  \u2022 \"... Logo\"   - the NVIDIA logo, mode dropdown = On/Off\n\n"
                       "The logo has no colour of its own; register 0x50 accepts only\n"
                       "0 and 1, so it is a switch rather than a light.\n\n"
                       "Brightness lives on the Strip device but is GLOBAL in hardware -\n"
                       "it dims the logo too.\n\n"
                       "Close Legion Space before use; it reasserts its own lighting and\n"
                       "will fight OpenRGB for the bus.\n\n"
                       "Static only: one colour change is a 37-write burst at Lenovo's\n"
                       "15 ms pacing, roughly 555 ms, so Direct mode is not achievable.");
    }

    label->setWordWrap(true);

    return(label);
}

QMenu* LenovoGPUPlugin::GetTrayMenu()
{
    return(nullptr);
}
