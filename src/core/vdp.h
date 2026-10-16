#pragma once

#include <array>
#include <cstdint>

#include "video_frame.h"

namespace plumgg {

// Sega 315-5378 (Game Gear VDP): Master System mode 4 with a 12-bit palette.
class Vdp {
public:
    static constexpr int kLinesPerFrame = 262;  // NTSC
    static constexpr int kActiveLines = 192;

    Vdp();
    void reset();

    uint8_t readData();
    void writeData(uint8_t v);
    uint8_t readControl();
    void writeControl(uint8_t v);
    uint8_t vCounter() const;
    uint8_t hCounter(int lineCycle) const;

    // Called once at the start of every scanline, before the CPU runs that line.
    void beginLine(int line);

    bool irqAsserted() const;
    const VideoFrame& frame() const { return frame_; }

    // Exposed for tests and debugging tools.
    const std::array<uint8_t, 0x4000>& vram() const { return vram_; }
    uint8_t reg(int n) const { return regs_[n & 15]; }

private:
    void renderLine(int line);
    void updatePalette(int index);
    uint16_t nameTableBase() const { return static_cast<uint16_t>((regs_[2] & 0x0E) << 10); }
    uint16_t spriteTableBase() const { return static_cast<uint16_t>((regs_[5] & 0x7E) << 7); }

    std::array<uint8_t, 0x4000> vram_{};
    std::array<uint8_t, 64> cram_{};
    std::array<uint32_t, 32> palette_{};
    std::array<uint8_t, 16> regs_{};

    uint16_t address_ = 0;
    uint8_t code_ = 0;
    bool secondByte_ = false;
    uint8_t readBuffer_ = 0;
    uint8_t cramLatch_ = 0;

    int line_ = 0;
    uint8_t lineCounter_ = 0;
    uint8_t scrollYLatch_ = 0;
    bool frameIrqPending_ = false;
    bool lineIrqPending_ = false;
    bool spriteOverflow_ = false;
    bool spriteCollision_ = false;

    VideoFrame frame_;
};

}  // namespace plumgg
