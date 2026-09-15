/*---------------------------------------------------------*\
| RGBController_LenovoGPU.h                                 |
|                                                           |
|   OpenRGB device for the Lenovo OEM GPU lighting          |
|   controller. Targets plugin API 4 (OpenRGB 1.0rc3).      |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "RGBController.h"
#include "LenovoGPUController.h"

/*---------------------------------------------------------*\
| The strip and the logo are exposed as SEPARATE OpenRGB     |
| devices. The logo is a switch, not a colour - modelling it |
| as a zone forced "set it to black" as the only way to turn |
| it off, which nobody would guess. As its own device it     |
| gets a mode dropdown, which is a real On/Off control.      |
|                                                            |
| Both devices share one LenovoGPUController, which owns the |
| logo state so a colour burst cannot clobber it.            |
\*---------------------------------------------------------*/
#define LENOVO_GPU_LED_STRIP    0

class RGBController_LenovoGPU : public RGBController
{
public:
    RGBController_LenovoGPU(LenovoGPUController* controller_ptr);
    ~RGBController_LenovoGPU();

    /*-----------------------------------------------------*\
    | Does NOT own the controller - the plugin does, because |
    | the logo device shares it.                             |
    \*-----------------------------------------------------*/

    void        SetupZones();
    void        ResizeZone(int zone, int new_size);

    void        DeviceUpdateLEDs();
    void        UpdateZoneLEDs(int zone);
    void        UpdateSingleLED(int led);

    void        DeviceUpdateMode();
    void        SetCustomMode();

private:
    void                    ApplyState();
    LenovoGPUController*    controller;
};
