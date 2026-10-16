#include "gamegear.h"

namespace plumgg {

GameGear::GameGear() : cpu_(*this) { reset(); }

bool GameGear::loadRom(const std::string& path, std::string* error) {
    if (!cart_.loadFile(path, error)) return false;
    reset();
    return true;
}

void GameGear::reset() {
    cpu_.reset();
    cpu_.sp = 0xDFF0;
    cpu_.im = 1;
    vdp_.reset();
    cart_.reset();
    ram_.fill(0);
    ggRegs_ = {0xC0, 0x7F, 0xFF, 0x00, 0xFF, 0x00, 0xFF};
    lineCycle_ = 0;
}

void GameGear::runFrame() {
    for (int line = 0; line < Vdp::kLinesPerFrame; ++line) {
        vdp_.beginLine(line);
        // Cycles that overran the previous line are carried over.
        while (lineCycle_ < kCyclesPerLine) {
            cpu_.setIrqLine(vdp_.irqAsserted());
            lineCycle_ += cpu_.step();
        }
        lineCycle_ -= kCyclesPerLine;
    }
}

uint8_t GameGear::read(uint16_t addr) {
    if (addr >= 0xC000) return ram_[addr & 0x1FFF];
    return cart_.read(addr);
}

void GameGear::write(uint16_t addr, uint8_t value) {
    if (addr >= 0xC000) ram_[addr & 0x1FFF] = value;
    cart_.write(addr, value);
}

uint8_t GameGear::in(uint16_t port16) {
    const uint8_t port = static_cast<uint8_t>(port16);
    if (port < 0x40) {
        if (port == 0x00) return static_cast<uint8_t>((ggRegs_[0] & 0x7F) | (buttons_.start ? 0 : 0x80));
        if (port <= 0x06) return ggRegs_[port];
        return 0xFF;
    }
    if (port < 0x80) return (port & 1) ? vdp_.hCounter(lineCycle_) : vdp_.vCounter();
    if (port < 0xC0) return (port & 1) ? vdp_.readControl() : vdp_.readData();
    if ((port & 1) == 0) {
        // Joypad, active low: up, down, left, right, 1, 2.
        uint8_t v = 0xFF;
        if (buttons_.up) v &= ~0x01;
        if (buttons_.down) v &= ~0x02;
        if (buttons_.left) v &= ~0x04;
        if (buttons_.right) v &= ~0x08;
        if (buttons_.button1) v &= ~0x10;
        if (buttons_.button2) v &= ~0x20;
        return v;
    }
    return 0xFF;
}

void GameGear::out(uint16_t port16, uint8_t value) {
    const uint8_t port = static_cast<uint8_t>(port16);
    if (port < 0x40) {
        if (port >= 0x01 && port <= 0x06) ggRegs_[port] = value;
        return;  // memory / I/O control registers have no visible effect on the Game Gear
    }
    if (port < 0x80) return;  // SN76489 PSG: sound is not emulated yet
    if (port < 0xC0) {
        if (port & 1) vdp_.writeControl(value); else vdp_.writeData(value);
    }
}

}  // namespace plumgg
