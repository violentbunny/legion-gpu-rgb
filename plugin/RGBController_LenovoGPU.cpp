/*---------------------------------------------------------*\
| RGBController_LenovoGPU.cpp                               |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "RGBController_LenovoGPU.h"

RGBController_LenovoGPU::RGBController_LenovoGPU(LenovoGPUController* controller_ptr)
{
    controller  = controller_ptr;

    name        = controller->GetDeviceName() + " Strip";
    vendor      = "Lenovo";
    type        = DEVICE_TYPE_GPU;
    description = "Lenovo OEM GPU lighting over NvAPI I2C (DDC port)";
    location    = controller->GetDeviceLocation();
    version     = "";
    serial      = "";

    /*-----------------------------------------------------*\
    | Effect id 0x01 is the only value ever observed on the  |
    | wire. Undefined ids do not fail safely - they leave    |
    | the controller in a runaway colour cycle - so the      |
    | animated Lenovo effects stay out until captured.       |
    |                                                        |
    | Brightness is exposed at mode level because 0x17 is    |
    | GLOBAL: it dims the strip and the logo together,       |
    | confirmed by sweeping it and watching both zones.      |
    \*-----------------------------------------------------*/
    mode static_mode;
    static_mode.name           = "Static";
    static_mode.value          = LENOVO_GPU_EFFECT_STATIC;
    static_mode.flags          = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_BRIGHTNESS;
    static_mode.color_mode     = MODE_COLORS_PER_LED;
    static_mode.brightness_min = 0;
    static_mode.brightness_max = LENOVO_GPU_BRIGHTNESS_MAX;
    static_mode.brightness     = LENOVO_GPU_BRIGHTNESS_MAX;
    static_mode.speed_min      = 0;
    static_mode.speed_max      = 0;
    static_mode.speed          = 0;
    static_mode.colors_min     = 0;
    static_mode.colors_max     = 0;
    modes.push_back(static_mode);

    SetupZones();
}

RGBController_LenovoGPU::~RGBController_LenovoGPU()
{
    /*-----------------------------------------------------*\
    | The controller is owned by the plugin and shared with  |
    | the logo device - do not delete it here.               |
    \*-----------------------------------------------------*/
}

void RGBController_LenovoGPU::SetupZones()
{
    /*-----------------------------------------------------*\
    | One zone: the RGB strip and backplate. The logo is a    |
    | separate device.                                        |
    \*-----------------------------------------------------*/
    zone strip_zone;
    strip_zone.name       = "Strip";
    strip_zone.type       = ZONE_TYPE_SINGLE;
    strip_zone.leds_min   = 1;
    strip_zone.leds_max   = 1;
    strip_zone.leds_count = 1;
    strip_zone.matrix_map = NULL;
    zones.push_back(strip_zone);

    led strip_led;
    strip_led.name = "Strip";
    leds.push_back(strip_led);

    SetupColors();
}

void RGBController_LenovoGPU::ResizeZone(int /*zone*/, int /*new_size*/)
{
    /*-----------------------------------------------------*\
    | Fixed hardware layout - nothing is resizable.          |
    \*-----------------------------------------------------*/
}

void RGBController_LenovoGPU::ApplyState()
{
    if(colors.empty())
    {
        return;
    }

    RGBColor strip = colors[LENOVO_GPU_LED_STRIP];

    unsigned int brightness = LENOVO_GPU_BRIGHTNESS_MAX;

    if(active_mode >= 0 && (unsigned int)active_mode < modes.size())
    {
        brightness = modes[active_mode].brightness;
    }

    controller->SetColor((unsigned char)RGBGetRValue(strip),
                         (unsigned char)RGBGetGValue(strip),
                         (unsigned char)RGBGetBValue(strip),
                         (unsigned char)brightness);
}

/*---------------------------------------------------------*\
| No partial-update command exists - one change is a         |
| 37-write burst - so every entry point applies full state.  |
| The controller suppresses no-op bursts internally.         |
\*---------------------------------------------------------*/
void RGBController_LenovoGPU::DeviceUpdateLEDs()
{
    ApplyState();
}

void RGBController_LenovoGPU::UpdateZoneLEDs(int /*zone*/)
{
    ApplyState();
}

void RGBController_LenovoGPU::UpdateSingleLED(int /*led*/)
{
    ApplyState();
}

void RGBController_LenovoGPU::DeviceUpdateMode()
{
    ApplyState();
}

void RGBController_LenovoGPU::SetCustomMode()
{
    active_mode = 0;
}
