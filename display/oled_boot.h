#pragma once
// One-shot boot animation, after TouchPlaited's display/oled_boot: "TouchVink"
// types itself in letter by letter ("Vink" one font size above "Touch"),
// holds, then breaks into particles that scatter and fall away. The screen
// then settles on a status line until the first control is touched.
//
// Runs from main() before StartAudio, so it may block for its ~2 s. It also
// blinks the user LED, so a unit with no screen still shows it is booting.

#include <cstdint>
#include "oled_screen.h"

namespace touchvink {

class OledBoot {
  public:
    // seed: System::GetNow(), so the scatter differs per boot.
    // status: the value row of the closing status line.
    static void Run(OledScreen& oled, uint32_t seed, const char* status, void (*set_led)(bool));

  private:
    OledBoot() = delete;
};

}  // namespace touchvink
