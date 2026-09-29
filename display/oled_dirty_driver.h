#pragma once
// SSD1306 128x32 driver that only transmits the pages whose pixels changed.
// From TouchPlaited's display/oled_dirty_driver.h, minus its I2C interlock:
// there the pads are polled from the audio ISR and could tear an OLED
// transfer, here pads and screen both run in the main loop, so they take
// turns on the bus by construction.
//
// The stock SSD130xDriver::Update() always pushes all four pages (~540 bytes
// at 400 kHz, ~14 ms of blocked main loop). Most screens here change one or
// two pages, and a status row that hasn't changed pushes nothing.

#include "dev/oled_ssd130x.h"
#include <cstring>

namespace touchvink {

class SSD1306DirtyDriver : public daisy::SSD130xDriver<128, 32, daisy::SSD130xI2CTransport> {
  public:
    using Base = daisy::SSD130xDriver<128, 32, daisy::SSD130xI2CTransport>;

    static constexpr size_t kWidth = 128;
    static constexpr size_t kPages = 32 / 8;

    void Init(Base::Config config) {
        Base::Init(config);
        // Nothing sent yet: the first Update() pushes every page.
        shadow_valid_ = false;
    }

    void Update() {
        for (size_t page = 0; page < kPages; page++) {
            uint8_t* src = &buffer_[kWidth * page];
            uint8_t* dst = &shadow_[kWidth * page];
            if (shadow_valid_ && std::memcmp(src, dst, kWidth) == 0) continue;
            // Each page carries its own address commands, so skipping one
            // never leaves the controller pointing somewhere unexpected.
            transport_.SendCommand(static_cast<uint8_t>(0xB0 + page));
            transport_.SendCommand(0x00);
            transport_.SendCommand(0x10);
            transport_.SendData(src, kWidth);
            std::memcpy(dst, src, kWidth);
        }
        shadow_valid_ = true;
    }

  private:
    uint8_t shadow_[kWidth * kPages];
    bool shadow_valid_ = false;
};

}  // namespace touchvink
