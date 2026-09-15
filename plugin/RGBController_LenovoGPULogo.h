/*---------------------------------------------------------*\
| RGBController_LenovoGPULogo.h                             |
|                                                           |
|   The NVIDIA logo panel as its own OpenRGB device.        |
|                                                           |
|   Register 0x50 accepts only 0x00 and 0x01 - verified by  |
|   sweeping 0-255 - so this is a switch, not a light with  |
|   a colour. It is exposed as two modes, "Off" and "On",   |
|   which makes OpenRGB's mode dropdown the actual toggle.  |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "RGBController.h"
#include "LenovoGPUController.h"

#define LENOVO_GPU_LOGO_MODE_OFF    0
#define LENOVO_GPU_LOGO_MODE_ON     1

class RGBController_LenovoGPULogo : public RGBController
{
public:
    RGBController_LenovoGPULogo(LenovoGPUController* controller_ptr);
    ~RGBController_LenovoGPULogo();

    void        SetupZones();
    void        ResizeZone(int zone, int new_size);

    void        DeviceUpdateLEDs();
    void        UpdateZoneLEDs(int zone);
    void        UpdateSingleLED(int led);

    void        DeviceUpdateMode();
    void        SetCustomMode();

private:
    /*-----------------------------------------------------*\
    | Shared with the strip device; owned by the plugin.     |
    \*-----------------------------------------------------*/
    LenovoGPUController*    controller;
};
