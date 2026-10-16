#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace plumgg {

// Game Gear cartridge with the standard Sega mapper (16 KB ROM pages, optional 32 KB RAM).
class Cartridge {
public:
    bool loadFile(const std::string& path, std::string* error);
    void loadData(std::vector<uint8_t> rom);

    bool loaded() const { return !rom_.empty(); }
    size_t romSize() const { return rom_.size(); }
    const std::string& path() const { return path_; }

    void reset();
    uint8_t read(uint16_t addr) const;
    // Writes to 0x8000-0xBFFF (cart RAM) and the mapper registers at 0xFFFC-0xFFFF.
    void write(uint16_t addr, uint8_t value);

    bool hasSaveData() const { return ramUsed_; }
    bool loadSave(const std::string& path);
    bool writeSave(const std::string& path) const;

private:
    std::vector<uint8_t> rom_;
    std::array<uint8_t, 0x8000> ram_{};
    std::array<uint8_t, 4> mapper_{};  // 0xFFFC control, 0xFFFD-0xFFFF page numbers
    size_t pageCount_ = 0;
    bool ramUsed_ = false;
    std::string path_;
};

}  // namespace plumgg
