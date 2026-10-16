#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "cartridge.h"
#include "psg.h"
#include "vdp.h"
#include "z80.h"

namespace plumgg {

struct Buttons {
    bool up = false, down = false, left = false, right = false;
    bool button1 = false, button2 = false, start = false;
};

// The whole console: Z80, VDP, cartridge, work RAM and I/O ports.
// It knows nothing about windows, rendering or audio devices; frontends read frame()
// and psg().samples() and feed input.
class GameGear : private Z80Bus {
public:
    static constexpr int kCyclesPerLine = 228;  // 3.579545 MHz / (262 lines * 59.92 Hz)

    GameGear();

    bool loadRom(const std::string& path, std::string* error);
    void reset();
    void setButtons(const Buttons& b) { buttons_ = b; }
    void runFrame();

    const VideoFrame& frame() const { return vdp_.frame(); }
    Cartridge& cartridge() { return cart_; }
    Z80& cpu() { return cpu_; }
    Vdp& vdp() { return vdp_; }
    Psg& psg() { return psg_; }
    uint8_t peek(uint16_t addr) { return read(addr); }  // CPU view of memory, for tools and tests

private:
    uint8_t read(uint16_t addr) override;
    void write(uint16_t addr, uint8_t value) override;
    uint8_t in(uint16_t port) override;
    void out(uint16_t port, uint8_t value) override;

    Z80 cpu_;
    Vdp vdp_;
    Psg psg_;
    Cartridge cart_;
    std::array<uint8_t, 0x2000> ram_{};
    std::array<uint8_t, 7> ggRegs_{};  // ports 0x00-0x06: start button, link port, stereo
    Buttons buttons_;
    int lineCycle_ = 0;
};

}  // namespace plumgg
