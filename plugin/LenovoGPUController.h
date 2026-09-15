/*---------------------------------------------------------*\
| LenovoGPUController.h                                     |
|                                                           |
|   Lenovo OEM RTX 5070 Ti lighting controller.             |
|   Device 0x49 (0x92 8-bit) on NvAPI I2C port 1, DDC side. |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <cstdint>
#include <string>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "LenovoGPUNvAPI.h"

/*---------------------------------------------------------*\
| Register map. Every value below was confirmed by isolating |
| one control at a time and diffing consecutive captures of  |
| Lenovo's own traffic - not inferred from its config files, |
| which describe the UI model rather than the wire protocol. |
\*---------------------------------------------------------*/
#define LENOVO_GPU_REG_BEGIN        0x30    /* <- 0xB0  begin frame             */
#define LENOVO_GPU_REG_COMMIT       0x31    /* <- 0xB1  commit                  */
#define LENOVO_GPU_REG_APPLY        0x32    /* <- 0xB2  apply / latch           */
#define LENOVO_GPU_REG_SELECT       0x40    /* <- 0x01  constant                */
#define LENOVO_GPU_REG_UNK14        0x14    /* <- 0x01  constant                */
#define LENOVO_GPU_REG_EFFECT       0x15    /* <- 0x01  static colour           */
#define LENOVO_GPU_REG_UNK16        0x16    /* <- 0x00  constant                */
#define LENOVO_GPU_REG_BRIGHTNESS   0x17    /* <- 0..100 PERCENT, global        */
#define LENOVO_GPU_REG_R            0x18
#define LENOVO_GPU_REG_G            0x19
#define LENOVO_GPU_REG_B            0x1A
#define LENOVO_GPU_REG_SEC_R        0x1B    /* <- 0x00 ) constant triple,       */
#define LENOVO_GPU_REG_SEC_G        0x1C    /* <- 0xC8 ) NOT a secondary colour */
#define LENOVO_GPU_REG_SEC_B        0x1D    /* <- 0xFF )                        */
#define LENOVO_GPU_REG_LOGO         0x50    /* <- 0x01 on / 0x00 off, boolean   */

#define LENOVO_GPU_MAGIC_BEGIN      0xB0
#define LENOVO_GPU_MAGIC_COMMIT     0xB1
#define LENOVO_GPU_MAGIC_APPLY      0xB2

#define LENOVO_GPU_EFFECT_STATIC    0x01
#define LENOVO_GPU_BRIGHTNESS_MAX   100

/*---------------------------------------------------------*\
| Lenovo paces its register writes ~15 ms apart. Writing     |
| faster is associated with updates being ignored.           |
\*---------------------------------------------------------*/
#define LENOVO_GPU_WRITE_DELAY_MS   15

/*---------------------------------------------------------*\
| A full colour burst is 37 paced writes, about 555 ms. That  |
| is far slower than anything that streams frames at it - the |
| OpenRGB SDK, and through it SignalRGB via the bridge, will  |
| happily call UpdateLEDs 60 times a second.                  |
|                                                             |
| So the public setters do not touch the bus. They record the |
| wanted state and wake a worker thread, which applies the    |
| LATEST state and drops everything superseded while it was   |
| busy. Callers never block, the bus is never queued up, and  |
| the hardware lands on the most recent colour instead of     |
| grinding through a backlog of stale ones.                   |
\*---------------------------------------------------------*/
class LenovoGPUController
{
public:
    LenovoGPUController(LenovoGPUNvAPI* nvapi, void* gpu_handle, unsigned int gpu_index);
    ~LenovoGPUController();

    std::string     GetDeviceName();
    std::string     GetDeviceLocation();

    /*-----------------------------------------------------*\
    | Strip colour + brightness. The burst also carries the   |
    | logo register, so it reuses the logo state held here    |
    | rather than taking it as an argument - otherwise        |
    | setting a colour would clobber the logo, which is owned |
    | by a separate OpenRGB device.                           |
    |                                                         |
    | Returns immediately; the write happens on the worker.   |
    \*-----------------------------------------------------*/
    bool            SetColor(uint8_t red, uint8_t green, uint8_t blue,
                             uint8_t brightness);

    /*-----------------------------------------------------*\
    | Minimal logo-only frame. Also asynchronous.            |
    \*-----------------------------------------------------*/
    bool            SetLogo(bool logo_on);

    /*-----------------------------------------------------*\
    | The wanted logo state, which is what the UI should      |
    | show - not necessarily what is on the wire yet.         |
    \*-----------------------------------------------------*/
    bool            GetLogo();

private:
    bool            Write(uint8_t reg, uint8_t value);
    void            WorkerThread();
    bool            ApplyColorBurst(uint8_t red, uint8_t green, uint8_t blue,
                                    uint8_t brightness, bool logo);
    bool            ApplyLogoBurst(bool logo);

    LenovoGPUNvAPI* nvapi;
    void*           gpu_handle;
    unsigned int    gpu_index;

    /*-----------------------------------------------------*\
    | Wanted state. Guarded by state_lock.                   |
    \*-----------------------------------------------------*/
    std::mutex              state_lock;
    std::condition_variable state_cv;
    bool                    thread_running;
    uint8_t                 req_r, req_g, req_b, req_brightness;
    bool                    req_logo;
    bool                    color_dirty;
    bool                    logo_dirty;

    /*-----------------------------------------------------*\
    | Last state actually written. Worker thread only, so no  |
    | locking - do not read these from anywhere else.         |
    \*-----------------------------------------------------*/
    bool            has_last;
    uint8_t         last_r, last_g, last_b, last_brightness;
    bool            has_last_logo;
    bool            last_logo;

    /*-----------------------------------------------------*\
    | Declared last so every member above is initialised      |
    | before the thread can observe it.                       |
    \*-----------------------------------------------------*/
    std::thread     worker;
};
