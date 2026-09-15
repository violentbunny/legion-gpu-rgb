/*---------------------------------------------------------*\
| LenovoGPUController.cpp                                   |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "LenovoGPUController.h"

#include <chrono>

LenovoGPUController::LenovoGPUController(LenovoGPUNvAPI* nvapi_ptr, void* handle, unsigned int index)
{
    nvapi           = nvapi_ptr;
    gpu_handle      = handle;
    gpu_index       = index;

    thread_running  = true;
    req_r           = 0;
    req_g           = 0;
    req_b           = 0;
    req_brightness  = LENOVO_GPU_BRIGHTNESS_MAX;
    req_logo        = true;      /* card ships with the logo lit */
    color_dirty     = false;     /* loading the plugin must not write */
    logo_dirty      = false;

    has_last        = false;
    last_r          = 0;
    last_g          = 0;
    last_b          = 0;
    last_brightness = 0;
    has_last_logo   = false;
    last_logo       = true;

    worker          = std::thread(&LenovoGPUController::WorkerThread, this);
}

LenovoGPUController::~LenovoGPUController()
{
    {
        std::lock_guard<std::mutex> lock(state_lock);
        thread_running = false;
    }

    state_cv.notify_all();

    if(worker.joinable())
    {
        /*-------------------------------------------------*\
        | Worst case this waits out one in-flight burst,     |
        | about 0.6 s. Detaching instead would let the       |
        | thread outlive the NvAPI object it writes through. |
        \*-------------------------------------------------*/
        worker.join();
    }
}

std::string LenovoGPUController::GetDeviceName()
{
    return("Lenovo Legion GPU (RTX 5070 Ti)");
}

std::string LenovoGPUController::GetDeviceLocation()
{
    return("NvAPI I2C GPU " + std::to_string(gpu_index) + " port 1 (DDC), addr 0x49");
}

/*---------------------------------------------------------*\
| Public setters - record and wake, never write.             |
\*---------------------------------------------------------*/
bool LenovoGPUController::SetColor(uint8_t red, uint8_t green, uint8_t blue,
                                   uint8_t brightness)
{
    if(brightness > LENOVO_GPU_BRIGHTNESS_MAX)
    {
        brightness = LENOVO_GPU_BRIGHTNESS_MAX;
    }

    {
        std::lock_guard<std::mutex> lock(state_lock);

        req_r          = red;
        req_g          = green;
        req_b          = blue;
        req_brightness = brightness;
        color_dirty    = true;
    }

    state_cv.notify_one();

    return(true);
}

bool LenovoGPUController::SetLogo(bool new_logo_on)
{
    {
        std::lock_guard<std::mutex> lock(state_lock);

        req_logo   = new_logo_on;
        logo_dirty = true;
    }

    state_cv.notify_one();

    return(true);
}

bool LenovoGPUController::GetLogo()
{
    std::lock_guard<std::mutex> lock(state_lock);

    return(req_logo);
}

/*---------------------------------------------------------*\
| Worker                                                     |
\*---------------------------------------------------------*/
void LenovoGPUController::WorkerThread()
{
    std::unique_lock<std::mutex> lock(state_lock);

    while(thread_running)
    {
        if(!color_dirty && !logo_dirty)
        {
            state_cv.wait(lock);
            continue;
        }

        /*-------------------------------------------------*\
        | Take the latest wanted state and clear the flags.  |
        | Anything that arrives during the burst below sets   |
        | them again and is picked up on the next pass, so    |
        | intermediate frames are coalesced away rather than  |
        | queued.                                             |
        \*-------------------------------------------------*/
        bool    do_color = color_dirty;
        uint8_t red      = req_r;
        uint8_t green    = req_g;
        uint8_t blue     = req_b;
        uint8_t bright   = req_brightness;
        bool    logo     = req_logo;

        color_dirty = false;
        logo_dirty  = false;

        lock.unlock();

        if(do_color)
        {
            /*---------------------------------------------*\
            | The colour burst carries the logo register, so  |
            | it satisfies a pending logo change too.         |
            \*---------------------------------------------*/
            bool changed = !has_last
                        || red    != last_r
                        || green  != last_g
                        || blue   != last_b
                        || bright != last_brightness
                        || !has_last_logo
                        || logo   != last_logo;

            if(changed && ApplyColorBurst(red, green, blue, bright, logo))
            {
                has_last        = true;
                last_r          = red;
                last_g          = green;
                last_b          = blue;
                last_brightness = bright;
                has_last_logo   = true;
                last_logo       = logo;
            }
        }
        else
        {
            if((!has_last_logo || logo != last_logo) && ApplyLogoBurst(logo))
            {
                has_last_logo = true;
                last_logo     = logo;
            }
        }

        lock.lock();
    }
}

/*---------------------------------------------------------*\
| Bus access - worker thread only.                           |
\*---------------------------------------------------------*/
bool LenovoGPUController::Write(uint8_t reg, uint8_t value)
{
    bool ok = nvapi->WriteRegister(gpu_handle, reg, value);
    std::this_thread::sleep_for(std::chrono::milliseconds(LENOVO_GPU_WRITE_DELAY_MS));
    return(ok);
}

bool LenovoGPUController::ApplyColorBurst(uint8_t red, uint8_t green, uint8_t blue,
                                          uint8_t brightness, bool logo)
{
    bool ok = true;

    /*-----------------------------------------------------*\
    | Colour block                                           |
    \*-----------------------------------------------------*/
    ok &= Write(LENOVO_GPU_REG_BEGIN,      LENOVO_GPU_MAGIC_BEGIN);
    ok &= Write(LENOVO_GPU_REG_SELECT,     0x01);
    ok &= Write(LENOVO_GPU_REG_EFFECT,     LENOVO_GPU_EFFECT_STATIC);
    ok &= Write(LENOVO_GPU_REG_UNK16,      0x00);
    ok &= Write(LENOVO_GPU_REG_BRIGHTNESS, brightness);
    ok &= Write(LENOVO_GPU_REG_R,          red);
    ok &= Write(LENOVO_GPU_REG_G,          green);
    ok &= Write(LENOVO_GPU_REG_B,          blue);
    ok &= Write(LENOVO_GPU_REG_SEC_R,      0x00);
    ok &= Write(LENOVO_GPU_REG_SEC_G,      0xC8);
    ok &= Write(LENOVO_GPU_REG_SEC_B,      0xFF);
    ok &= Write(0x20, 0x00);
    ok &= Write(0x21, 0x00);
    ok &= Write(0x22, 0x00);
    ok &= Write(0x23, 0x00);
    ok &= Write(LENOVO_GPU_REG_COMMIT,     LENOVO_GPU_MAGIC_COMMIT);
    ok &= Write(LENOVO_GPU_REG_APPLY,      LENOVO_GPU_MAGIC_APPLY);

    /*-----------------------------------------------------*\
    | Trailing frame - note there is no APPLY after this     |
    | COMMIT, matching the captured traffic exactly.         |
    \*-----------------------------------------------------*/
    ok &= Write(LENOVO_GPU_REG_BEGIN,  LENOVO_GPU_MAGIC_BEGIN);
    ok &= Write(LENOVO_GPU_REG_SELECT, 0x01);
    ok &= Write(LENOVO_GPU_REG_UNK14,  0x01);
    ok &= Write(LENOVO_GPU_REG_COMMIT, LENOVO_GPU_MAGIC_COMMIT);

    /*-----------------------------------------------------*\
    | Logo frame, sent four times as Lenovo does.            |
    \*-----------------------------------------------------*/
    for(int i = 0; i < 4; i++)
    {
        ok &= Write(LENOVO_GPU_REG_BEGIN,  LENOVO_GPU_MAGIC_BEGIN);
        ok &= Write(LENOVO_GPU_REG_LOGO,   logo ? 0x01 : 0x00);
        ok &= Write(LENOVO_GPU_REG_COMMIT, LENOVO_GPU_MAGIC_COMMIT);
        ok &= Write(LENOVO_GPU_REG_APPLY,  LENOVO_GPU_MAGIC_APPLY);
    }

    return(ok);
}

bool LenovoGPUController::ApplyLogoBurst(bool logo)
{
    bool ok = true;

    for(int i = 0; i < 4; i++)
    {
        ok &= Write(LENOVO_GPU_REG_BEGIN,  LENOVO_GPU_MAGIC_BEGIN);
        ok &= Write(LENOVO_GPU_REG_LOGO,   logo ? 0x01 : 0x00);
        ok &= Write(LENOVO_GPU_REG_COMMIT, LENOVO_GPU_MAGIC_COMMIT);
        ok &= Write(LENOVO_GPU_REG_APPLY,  LENOVO_GPU_MAGIC_APPLY);
    }

    return(ok);
}
