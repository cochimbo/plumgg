#include "vdp.h"

#include <algorithm>

namespace plumgg {

Vdp::Vdp() { reset(); }

void Vdp::reset() {
    vram_.fill(0);
    cram_.fill(0);
    // Register values the Game Gear BIOS leaves behind; games without a BIOS expect them.
    static constexpr uint8_t kInitRegs[16] = {0x36, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x00,
                                              0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::copy(std::begin(kInitRegs), std::end(kInitRegs), regs_.begin());
    for (int i = 0; i < 32; ++i) updatePalette(i);
    address_ = 0;
    code_ = 0;
    secondByte_ = false;
    readBuffer_ = 0;
    cramLatch_ = 0;
    line_ = 0;
    lineCounter_ = regs_[10];
    scrollYLatch_ = 0;
    frameIrqPending_ = lineIrqPending_ = false;
    spriteOverflow_ = spriteCollision_ = false;
    frame_ = VideoFrame{};
}

void Vdp::updatePalette(int index) {
    // Game Gear CRAM entry: byte 0 = GGGGRRRR, byte 1 = ----BBBB.
    uint8_t lo = cram_[index * 2];
    uint8_t hi = cram_[index * 2 + 1];
    uint32_t r = (lo & 0x0F) * 17u;
    uint32_t g = (lo >> 4) * 17u;
    uint32_t b = (hi & 0x0F) * 17u;
    palette_[index] = 0xFF000000u | (r << 16) | (g << 8) | b;
}

uint8_t Vdp::readData() {
    secondByte_ = false;
    uint8_t v = readBuffer_;
    readBuffer_ = vram_[address_];
    address_ = (address_ + 1) & 0x3FFF;
    return v;
}

void Vdp::writeData(uint8_t v) {
    secondByte_ = false;
    if (code_ == 3) {
        // CRAM is written a 16-bit word at a time: even bytes latch, odd bytes commit.
        uint8_t a = address_ & 0x3F;
        if ((a & 1) == 0) {
            cramLatch_ = v;
        } else {
            cram_[a & 0x3E] = cramLatch_;
            cram_[a] = v;
            updatePalette(a >> 1);
        }
    } else {
        vram_[address_] = v;
    }
    readBuffer_ = v;
    address_ = (address_ + 1) & 0x3FFF;
}

uint8_t Vdp::readControl() {
    uint8_t status = static_cast<uint8_t>((frameIrqPending_ ? 0x80 : 0) | (spriteOverflow_ ? 0x40 : 0) |
                                          (spriteCollision_ ? 0x20 : 0) | 0x1F);
    frameIrqPending_ = false;
    lineIrqPending_ = false;
    spriteOverflow_ = false;
    spriteCollision_ = false;
    secondByte_ = false;
    return status;
}

void Vdp::writeControl(uint8_t v) {
    if (!secondByte_) {
        address_ = static_cast<uint16_t>((address_ & 0x3F00) | v);
        secondByte_ = true;
        return;
    }
    secondByte_ = false;
    address_ = static_cast<uint16_t>((address_ & 0x00FF) | ((v & 0x3F) << 8));
    code_ = v >> 6;
    switch (code_) {
        case 0:
            readBuffer_ = vram_[address_];
            address_ = (address_ + 1) & 0x3FFF;
            break;
        case 2:
            regs_[v & 0x0F] = static_cast<uint8_t>(address_ & 0xFF);
            break;
        default:
            break;
    }
}

uint8_t Vdp::vCounter() const {
    // NTSC 192-line mode counts 00-DA, then jumps back to D5-FF.
    return static_cast<uint8_t>(line_ <= 0xDA ? line_ : line_ - 6);
}

uint8_t Vdp::hCounter(int lineCycle) const {
    int v = lineCycle * 3 / 4;  // 228 CPU cycles map onto 171 counter steps
    if (v > 0x93) v += 0xE9 - 0x94;
    return static_cast<uint8_t>(v);
}

bool Vdp::irqAsserted() const {
    return (frameIrqPending_ && (regs_[1] & 0x20)) || (lineIrqPending_ && (regs_[0] & 0x10));
}

void Vdp::beginLine(int line) {
    line_ = line;
    if (line == 0) {
        scrollYLatch_ = regs_[9];
        ++frame_.frameNumber;
    }
    if (line < kActiveLines) renderLine(line);

    if (line <= kActiveLines) {
        if (lineCounter_ == 0) {
            lineCounter_ = regs_[10];
            lineIrqPending_ = true;
        } else {
            --lineCounter_;
        }
    } else {
        lineCounter_ = regs_[10];
    }
    if (line == kActiveLines) frameIrqPending_ = true;
}

void Vdp::renderLine(int line) {
    const int base = line * VideoFrame::kWidth;
    const bool display = regs_[1] & 0x40;
    const uint8_t scrollX = regs_[8];
    frame_.lines[line] = {scrollX, scrollYLatch_, display};

    const uint8_t backdrop = static_cast<uint8_t>(16 + (regs_[7] & 0x0F));
    if (!display) {
        for (int x = 0; x < VideoFrame::kWidth; ++x) {
            frame_.rgba[base + x] = frame_.backgroundRgba[base + x] = palette_[backdrop];
            frame_.info[base + x] = frame_.backgroundInfo[base + x] = {backdrop, PixelLayer::Backdrop, 0};
            frame_.spriteRgba[base + x] = 0;
        }
        return;
    }

    // Sprites: the first sprite in the table wins where two overlap.
    std::array<int8_t, VideoFrame::kWidth> spriteColor;
    std::array<uint8_t, VideoFrame::kWidth> spriteNumber{};
    spriteColor.fill(-1);
    const bool tall = regs_[1] & 0x02;
    const int zoom = (regs_[1] & 0x01) ? 2 : 1;
    const int height = (tall ? 16 : 8) * zoom;
    const uint16_t sat = spriteTableBase();
    int onLine = 0;
    for (int s = 0; s < 64; ++s) {
        int y = vram_[sat + s];
        if (y == 0xD0) break;
        int top = y + 1;
        if (top > 0xE0) top -= 256;
        int row = line - top;
        if (row < 0 || row >= height) continue;
        if (++onLine > 8) {
            spriteOverflow_ = true;
            break;
        }
        int x = vram_[(sat + 0x80 + s * 2) & 0x3FFF];
        int pattern = vram_[(sat + 0x81 + s * 2) & 0x3FFF];
        if (regs_[6] & 0x04) pattern |= 0x100;
        if (tall) pattern &= ~1;
        if (regs_[0] & 0x08) x -= 8;
        row /= zoom;
        pattern += row >> 3;
        const int addr = (pattern * 32 + (row & 7) * 4) & 0x3FFF;
        const uint8_t p0 = vram_[addr], p1 = vram_[addr + 1], p2 = vram_[addr + 2], p3 = vram_[addr + 3];
        for (int px = 0; px < 8; ++px) {
            const int bit = 7 - px;
            const int color = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2) |
                              (((p3 >> bit) & 1) << 3);
            if (color == 0) continue;
            for (int dx = 0; dx < zoom; ++dx) {
                const int sx = x + px * zoom + dx;
                if (sx < 0 || sx >= VideoFrame::kWidth) continue;
                if (spriteColor[sx] >= 0) {
                    spriteCollision_ = true;
                    continue;
                }
                spriteColor[sx] = static_cast<int8_t>(color);
                spriteNumber[sx] = static_cast<uint8_t>(s);
            }
        }
    }

    // Background, combined with the sprite line.
    const uint16_t nameTable = nameTableBase();
    const bool lockTopRows = (regs_[0] & 0x40) && line < 16;
    const bool lockRightColumns = regs_[0] & 0x80;
    const bool maskColumn0 = regs_[0] & 0x20;
    for (int x = 0; x < VideoFrame::kWidth; ++x) {
        const int scy = (lockRightColumns && x >= 192) ? 0 : scrollYLatch_;
        const int scx = lockTopRows ? 0 : scrollX;
        const int row = (line + scy) % 224;
        const int bx = (x - scx) & 0xFF;
        const int entryAddr = (nameTable + (row >> 3) * 64 + (bx >> 3) * 2) & 0x3FFF;
        const uint16_t entry = static_cast<uint16_t>(vram_[entryAddr] | (vram_[entryAddr + 1] << 8));
        const int pattern = entry & 0x1FF;
        int tileRow = row & 7;
        int tileCol = bx & 7;
        if (entry & 0x400) tileRow = 7 - tileRow;
        if (entry & 0x200) tileCol = 7 - tileCol;
        const int addr = (pattern * 32 + tileRow * 4) & 0x3FFF;
        const int bit = 7 - tileCol;
        const int color = ((vram_[addr] >> bit) & 1) | (((vram_[addr + 1] >> bit) & 1) << 1) |
                          (((vram_[addr + 2] >> bit) & 1) << 2) | (((vram_[addr + 3] >> bit) & 1) << 3);
        const bool priority = entry & 0x1000;
        const uint8_t bgIndex = static_cast<uint8_t>(color + ((entry & 0x800) ? 16 : 0));

        const bool masked = maskColumn0 && x < 8;
        const PixelInfo background =
            masked ? PixelInfo{backdrop, PixelLayer::Backdrop, 0}
                   : PixelInfo{bgIndex, priority ? PixelLayer::BackgroundPriority : PixelLayer::Background,
                               static_cast<uint16_t>(pattern)};
        const bool sprite = !masked && spriteColor[x] >= 0;
        const PixelInfo info = sprite && !(priority && color != 0)
                                   ? PixelInfo{static_cast<uint8_t>(16 + spriteColor[x]), PixelLayer::Sprite,
                                               spriteNumber[x]}
                                   : background;
        frame_.info[base + x] = info;
        frame_.rgba[base + x] = palette_[info.colorIndex];
        frame_.backgroundInfo[base + x] = background;
        frame_.backgroundRgba[base + x] = palette_[background.colorIndex];
        frame_.spriteRgba[base + x] = sprite ? palette_[16 + spriteColor[x]] : 0;
    }
}

}  // namespace plumgg
