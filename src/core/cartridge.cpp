#include "cartridge.h"

#include <fstream>
#include <iterator>

namespace plumgg {

bool Cartridge::loadFile(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "no se puede abrir " + path;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Some dumps carry a 512-byte copier header in front of the ROM.
    if (data.size() % 0x4000 == 512) data.erase(data.begin(), data.begin() + 512);
    if (data.empty()) {
        if (error) *error = "la ROM está vacía: " + path;
        return false;
    }
    loadData(std::move(data));
    path_ = path;
    return true;
}

void Cartridge::loadData(std::vector<uint8_t> rom) {
    // Pad to a power-of-two number of 16 KB pages so page numbers can be masked.
    size_t pages = 1;
    while (pages * 0x4000 < rom.size()) pages <<= 1;
    rom.resize(pages * 0x4000, 0xFF);
    rom_ = std::move(rom);
    pageCount_ = pages;
    path_.clear();
    ram_.fill(0);
    ramUsed_ = false;
    reset();
}

void Cartridge::reset() { mapper_ = {0, 0, 1, 2}; }

uint8_t Cartridge::read(uint16_t addr) const {
    if (rom_.empty()) return 0xFF;
    if (addr < 0x0400) return rom_[addr];  // first 1 KB is never paged
    if (addr < 0xC000) {
        const int slot = addr >> 14;
        if (slot == 2 && (mapper_[0] & 0x08)) {
            return ram_[((mapper_[0] & 0x04) ? 0x4000 : 0) + (addr & 0x3FFF)];
        }
        const size_t page = mapper_[1 + slot] & (pageCount_ - 1);
        return rom_[page * 0x4000 + (addr & 0x3FFF)];
    }
    return 0xFF;
}

void Cartridge::write(uint16_t addr, uint8_t value) {
    if (addr >= 0xFFFC) {
        mapper_[addr - 0xFFFC] = value;
        return;
    }
    if (addr >= 0x8000 && addr < 0xC000 && (mapper_[0] & 0x08)) {
        ram_[((mapper_[0] & 0x04) ? 0x4000 : 0) + (addr & 0x3FFF)] = value;
        ramUsed_ = true;
    }
}

bool Cartridge::loadSave(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.read(reinterpret_cast<char*>(ram_.data()), static_cast<std::streamsize>(ram_.size()));
    ramUsed_ = in.gcount() > 0;
    return ramUsed_;
}

bool Cartridge::writeSave(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(ram_.data()), static_cast<std::streamsize>(ram_.size()));
    return static_cast<bool>(out);
}

}  // namespace plumgg
