/*---------------------------------------------------------*\
| RGBController_LenovoGPULogo.cpp                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "RGBController_LenovoGPULogo.h"

RGBController_LenovoGPULogo::RGBController_LenovoGPULogo(LenovoGPUController* controller_ptr)
{
    controller  = controller_ptr;

    name        = controller->GetDeviceName() + " Logo";
    vendor      = "Lenovo";
    type        = DEVICE_TYPE_GPU;
    description = "NVIDIA logo panel - on/off only, no colour";
    location    = controller->GetDeviceLocation();
    version     = "";
    serial      = "";

    /*-----------------------------------------------------*\
    | Two modes and no colours. MODE_COLORS_NONE with no     |
    | colour flags means OpenRGB shows a mode dropdown and   |
    | no colour picker - so the dropdown is the on/off       |
    | switch, which is what this hardware actually is.       |
    \*-----------------------------------------------------*/
    mode off_mode;
    off_mode.name       = "Off";
    off_mode.value      = LENOVO_GPU_LOGO_MODE_OFF;
    off_mode.flags      = 0;
    off_mode.color_mode = MODE_COLORS_NONE;
    modes.push_back(off_mode);

    mode on_mode;
    on_mode.name       = "On";
    on_mode.value      = LENOVO_GPU_LOGO_MODE_ON;
    on_mode.flags      = 0;
    on_mode.color_mode = MODE_COLORS_NONE;
    modes.push_back(on_mode);

    /*-----------------------------------------------------*\
    | Reflect the controller's current logo state so the UI  |
    | opens showing reality rather than a guess.             |
    \*-----------------------------------------------------*/
    active_mode = controller->GetLogo() ? 1 : 0;

    SetupZones();
}

RGBController_LenovoGPULogo::~RGBController_LenovoGPULogo()
{
    /*-----------------------------------------------------*\
    | Controller is owned by the plugin and shared with the  |
    | strip device - do not delete it here.                  |
    \*-----------------------------------------------------*/
}

void RGBController_LenovoGPULogo::SetupZones()
{
    /*-----------------------------------------------------*\
    | A single LED is declared purely so the device has a    |
    | valid structure. It carries no colour - the modes do   |
    | all the work.                                          |
    \*-----------------------------------------------------*/
    zone logo_zone;
    logo_zone.name       = "Logo";
    logo_zone.type       = ZONE_TYPE_SINGLE;
    logo_zone.leds_min   = 1;
    logo_zone.leds_max   = 1;
    logo_zone.leds_count = 1;
    logo_zone.matrix_map = NULL;
    zones.push_back(logo_zone);

    led logo_led;
    logo_led.name = "Logo";
    leds.push_back(logo_led);

    SetupColors();
}

void RGBController_LenovoGPULogo::ResizeZone(int /*zone*/, int /*new_size*/)
{
}

void RGBController_LenovoGPULogo::DeviceUpdateMode()
{
    controller->SetLogo(active_mode == LENOVO_GPU_LOGO_MODE_ON);
}

/*---------------------------------------------------------*\
| No colour to push - the logo has none. These exist only    |
| because the base class requires them.                      |
\*---------------------------------------------------------*/
void RGBController_LenovoGPULogo::DeviceUpdateLEDs()
{
}

void RGBController_LenovoGPULogo::UpdateZoneLEDs(int /*zone*/)
{
}

void RGBController_LenovoGPULogo::UpdateSingleLED(int /*led*/)
{
}

void RGBController_LenovoGPULogo::SetCustomMode()
{
}
