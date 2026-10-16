#include "cartridge.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#include "miniz.h"

namespace plumgg {

namespace {

bool isZip(const std::vector<uint8_t>& data) {
    return data.size() >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 3 && data[3] == 4;
}

bool hasRomExtension(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto endsWith = [&](const char* ext) {
        const std::string e(ext);
        return name.size() > e.size() && name.compare(name.size() - e.size(), e.size(), e) == 0;
    };
    return endsWith(".gg") || endsWith(".sms") || endsWith(".bin");
}

// Replaces a zip archive with its first .gg/.sms/.bin entry.
bool extractRomFromZip(std::vector<uint8_t>& data, const std::string& path, std::string* error) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0)) {
        if (error) *error = "el zip está dañado: " + path;
        return false;
    }
    bool found = false;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count && !found; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory || !hasRomExtension(st.m_filename)) continue;
        size_t size = 0;
        void* rom = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!rom) {
            if (error) *error = std::string("no se puede descomprimir ") + st.m_filename + " de " + path;
            mz_zip_reader_end(&zip);
            return false;
        }
        const uint8_t* bytes = static_cast<const uint8_t*>(rom);
        std::vector<uint8_t> extracted(bytes, bytes + size);
        mz_free(rom);
        data = std::move(extracted);
        found = true;
    }
    mz_zip_reader_end(&zip);
    if (!found && error) *error = "el zip no contiene ninguna ROM (.gg, .sms o .bin): " + path;
    return found;
}

}  // namespace

bool Cartridge::loadFile(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "no se puede abrir " + path;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (isZip(data) && !extractRomFromZip(data, path, error)) return false;
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
