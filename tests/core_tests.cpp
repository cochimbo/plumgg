// Core tests that need no external files: mapper paging, tiny ROMs drawing tiles and a
// sprite, loading from a zip, the PSG, and the pseudo-3D planes and their edge distance.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "core/gamegear.h"
#include "miniz.h"
#include "render/planes.h"

using namespace plumgg;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

void testMapper() {
    std::vector<uint8_t> rom(4 * 0x4000);
    for (size_t i = 0; i < rom.size(); ++i) rom[i] = static_cast<uint8_t>(i / 0x4000);
    Cartridge cart;
    cart.loadData(rom);
    check(cart.read(0x0000) == 0 && cart.read(0x4000) == 1 && cart.read(0x8000) == 2, "default pages 0,1,2");
    cart.write(0xFFFF, 3);
    check(cart.read(0x8000) == 3, "slot 2 switches to page 3");
    cart.write(0xFFFD, 2);
    check(cart.read(0x0100) == 0 && cart.read(0x0400) == 2, "first 1 KB stays fixed");
    cart.write(0xFFFE, 7);
    check(cart.read(0x4000) == 3, "page numbers wrap to the ROM size");
    cart.write(0xFFFC, 0x08);
    cart.write(0x8000, 0x5A);
    check(cart.read(0x8000) == 0x5A && cart.hasSaveData(), "cartridge RAM maps into slot 2");
}

struct Asm {
    std::vector<uint8_t> code;
    void bytes(std::initializer_list<uint8_t> b) { code.insert(code.end(), b); }
    void outPort(uint8_t port, uint8_t v) { bytes({0x3E, v, 0xD3, port}); }  // ld a,v ; out (port),a
    void vdpReg(int reg, uint8_t v) { outPort(0xBF, v); outPort(0xBF, static_cast<uint8_t>(0x80 | reg)); }
    void vdpAddr(uint16_t addr, int code) {
        outPort(0xBF, static_cast<uint8_t>(addr));
        outPort(0xBF, static_cast<uint8_t>(((addr >> 8) & 0x3F) | (code << 6)));
    }
};

void testTileRendering() {
    Asm a;
    a.bytes({0xF3});              // di
    a.vdpReg(2, 0xFF);            // name table at 0x3800
    a.vdpAddr(0x0000, 3);         // CRAM: colour 0 black, colour 1 red
    for (uint8_t v : {0x00, 0x00, 0x0F, 0x00}) a.outPort(0xBE, v);
    a.vdpAddr(0x0020, 1);         // tile 1: every pixel colour 1
    for (int row = 0; row < 8; ++row)
        for (uint8_t v : {0xFF, 0x00, 0x00, 0x00}) a.outPort(0xBE, v);
    a.vdpAddr(0x3800 + 3 * 64 + 6 * 2, 1);  // tile 1 at row 3, column 6: the LCD's top-left
    a.outPort(0xBE, 0x01);
    a.outPort(0xBE, 0x00);
    a.vdpReg(1, 0x40);            // display on
    a.bytes({0x18, 0xFE});        // jr $

    GameGear gg;
    std::vector<uint8_t> rom = a.code;
    rom.resize(0x8000, 0xFF);
    gg.cartridge().loadData(rom);
    gg.reset();
    gg.runFrame();
    gg.runFrame();

    const VideoFrame& f = gg.frame();
    const int topLeft = VideoFrame::kVisibleY * VideoFrame::kWidth + VideoFrame::kVisibleX;
    check(f.rgba[topLeft] == 0xFFFF0000u, "tile pixel is red");
    check(f.rgba[topLeft + 7 + 7 * VideoFrame::kWidth] == 0xFFFF0000u, "whole tile is red");
    check(f.rgba[topLeft + 8] == 0xFF000000u, "neighbouring tile is black");
    check(f.info[topLeft].layer == PixelLayer::Background && f.info[topLeft].source == 1, "pixel tagged as tile 1");
}

// A sprite over a normal tile and a priority tile: the VDP keeps both planes apart and
// splitPlanes() sorts the pixels into the pseudo-3D planes.
void testPlanes() {
    Asm a;
    a.bytes({0xF3});              // di
    a.vdpReg(2, 0xFF);            // name table at 0x3800
    a.vdpReg(5, 0xFF);            // sprite table at 0x3F00
    a.vdpReg(6, 0x00);            // sprite patterns from tile 0
    a.vdpAddr(0x0000, 3);         // CRAM: colour 0 black, colour 1 red
    for (uint8_t v : {0x00, 0x00, 0x0F, 0x00}) a.outPort(0xBE, v);
    a.vdpAddr(0x0022, 3);         // sprite colour 1 (CRAM 17) green
    for (uint8_t v : {0xF0, 0x00}) a.outPort(0xBE, v);
    a.vdpAddr(0x0020, 1);         // tile 1: every pixel colour 1
    for (int row = 0; row < 8; ++row)
        for (uint8_t v : {0xFF, 0x00, 0x00, 0x00}) a.outPort(0xBE, v);
    a.vdpAddr(0x3800 + 3 * 64 + 6 * 2, 1);  // LCD top-left: tile 1, then tile 1 with priority
    for (uint8_t v : {0x01, 0x00, 0x01, 0x10}) a.outPort(0xBE, v);
    a.vdpAddr(0x3F00, 1);         // sprite 0 on the LCD's first line, then the end marker
    for (uint8_t v : {23, 0xD0}) a.outPort(0xBE, v);
    a.vdpAddr(0x3F80, 1);         // x = 4 pixels into the LCD, tile 1
    for (uint8_t v : {VideoFrame::kVisibleX + 4, 1}) a.outPort(0xBE, v);
    a.vdpReg(1, 0x40);            // display on
    a.bytes({0x18, 0xFE});        // jr $

    GameGear gg;
    std::vector<uint8_t> rom = a.code;
    rom.resize(0x8000, 0xFF);
    gg.cartridge().loadData(rom);
    gg.reset();
    gg.runFrame();
    gg.runFrame();

    const uint32_t red = 0xFFFF0000u, green = 0xFF00FF00u, black = 0xFF000000u;
    const VideoFrame& f = gg.frame();
    const int top = VideoFrame::kVisibleY * VideoFrame::kWidth + VideoFrame::kVisibleX;
    check(f.info[top + 4].layer == PixelLayer::Sprite && f.rgba[top + 4] == green, "sprite covers a normal tile");
    check(f.backgroundRgba[top + 4] == red, "background kept under the sprite");
    check(f.info[top + 9].layer == PixelLayer::BackgroundPriority && f.rgba[top + 9] == red,
          "priority tile covers the sprite");
    check(f.spriteRgba[top + 9] == green, "sprite kept under the priority tile");
    check(f.spriteRgba[top + 20] == 0, "no sprite outside it");

    Planes planes;
    splitPlanes(f, planes);
    check(planes[PlaneBackground][4] == red && planes[PlaneSprites][4] == green, "planes: tile and sprite");
    check(planes[PlanePriority][9] == red && planes[PlaneBackground][9] == 0, "planes: priority tile on its own");
    check(planes[PlaneSky][20] == black && planes[PlaneBackground][20] == 0, "planes: colour 0 is sky");
    check(planes[PlaneSky][4] == black, "planes: sky filled in behind solid tiles");

    std::vector<uint32_t> solid;
    markSolidTiles(f, solid);
    check(solid[4] && solid[9], "solid tiles: a tile with no colour 0 is solid");
    check(!solid[20], "solid tiles: sky is not");
}

// Edge distance for the voxel view: 1 on the outline of a shape, growing inwards up
// to the cap, and empty pixels untouched.
void testEdgeDistance() {
    constexpr int kW = VideoFrame::kVisibleWidth;
    std::vector<uint32_t> plane(kW * VideoFrame::kVisibleHeight, 0);
    for (int y = 10; y < 30; ++y)
        for (int x = 10; x < 30; ++x) plane[y * kW + x] = 0xFF123456u;
    markEdgeDistance(plane);
    auto distance = [&](int x, int y) { return kMaxEdgeDistance - (255 - int(plane[y * kW + x] >> 24)) / 16; };
    check(distance(10, 10) == 1 && distance(29, 20) == 1, "edge distance: 1 on the outline");
    check(distance(11, 20) == 2 && distance(20, 12) == 3, "edge distance: grows inwards");
    check(distance(20, 20) == kMaxEdgeDistance, "edge distance: capped in the middle");
    check(plane[0] == 0 && (plane[20 * kW + 20] & 0xFFFFFF) == 0x123456, "edge distance: colours untouched");
}

void testVdpInterrupt() {
    // ei; im 1; halt-loop. The frame interrupt handler at 0x38 counts into RAM.
    std::vector<uint8_t> rom(0x8000, 0x00);
    const uint8_t main[] = {0x31, 0xF0, 0xDF,              // ld sp,0xDFF0
                            0x3E, 0x20, 0xD3, 0xBF,        // ld a,0x20 ; out (0xbf),a
                            0x3E, 0x81, 0xD3, 0xBF,        // ld a,0x81 ; out (0xbf),a  -> R1 = frame IRQ on
                            0xED, 0x56, 0xFB,              // im 1 ; ei
                            0x76, 0x18, 0xFD};             // halt ; jr -3
    std::copy(std::begin(main), std::end(main), rom.begin());
    const uint8_t isr[] = {0xF5, 0xDB, 0xBF,               // push af ; in a,(0xbf) (acknowledge)
                           0x3A, 0x00, 0xC0, 0x3C, 0x32, 0x00, 0xC0,  // inc byte at 0xC000
                           0xF1, 0xFB, 0xC9};              // pop af ; ei ; ret
    std::copy(std::begin(isr), std::end(isr), rom.begin() + 0x38);

    GameGear gg;
    gg.cartridge().loadData(rom);
    gg.reset();
    for (int i = 0; i < 10; ++i) gg.runFrame();
    check(gg.peek(0xC000) >= 9 && gg.peek(0xC000) <= 10, "one frame interrupt per frame");
    check(gg.peek(0xE000) == gg.peek(0xC000), "work RAM is mirrored at 0xE000");
}

void testPsg() {
    Psg psg;
    psg.setSampleRate(44100);
    psg.write(0x80 | 0x0E);  // tone 0 period low bits: 0x0FE -> ~440 Hz
    psg.write(0x0F);
    psg.write(0x90);         // tone 0 at full volume
    psg.setStereo(0x01);     // tone 0 on the right only
    psg.run(3579545 / 10);   // a tenth of a second
    const auto& s = psg.samples();
    check(s.size() / 2 >= 4405 && s.size() / 2 <= 4415, "a tenth of a second at 44.1 kHz");
    int crossings = 0, leftPeak = 0, rightPeak = 0;
    for (size_t i = 0; i + 2 < s.size(); i += 2) {
        if ((s[i + 1] > 0) != (s[i + 3] > 0)) ++crossings;
        leftPeak = std::max(leftPeak, std::abs(int(s[i])));
        rightPeak = std::max(rightPeak, std::abs(int(s[i + 1])));
    }
    check(crossings >= 86 && crossings <= 90, "tone 0 plays at about 440 Hz");
    check(leftPeak == 0 && rightPeak > 7000, "stereo register routes tone 0 to the right");

    psg.clearSamples();
    psg.write(0x9F);         // tone 0 off
    psg.write(0xE4);         // white noise, fastest rate
    psg.write(0xF0);         // noise at full volume
    psg.setStereo(0xFF);
    psg.run(3579545 / 10);
    int changes = 0;
    for (size_t i = 0; i + 2 < psg.samples().size(); i += 2)
        if (psg.samples()[i] != psg.samples()[i + 2]) ++changes;
    check(changes > 300, "noise channel produces noise");
}

bool writeZip(const std::string& path, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) return false;
    for (const auto& [name, data] : entries) {
        mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
    }
    void* buf = nullptr;
    size_t size = 0;
    const bool ok = mz_zip_writer_finalize_heap_archive(&zip, &buf, &size);
    if (ok) std::ofstream(path, std::ios::binary).write(static_cast<const char*>(buf), static_cast<std::streamsize>(size));
    mz_free(buf);
    mz_zip_writer_end(&zip);
    return ok;
}

void testZipRom() {
    std::vector<uint8_t> rom(2 * 0x4000);
    for (size_t i = 0; i < rom.size(); ++i) rom[i] = static_cast<uint8_t>(i / 0x4000 + 0x40);
    const std::string path = "core_tests_rom.zip";
    writeZip(path, {{"LEEME.txt", {'h', 'o', 'l', 'a'}}, {"juego/Sonic.GG", rom}, {"otro.sms", {0x00}}});
    Cartridge cart;
    std::string error;
    check(cart.loadFile(path, &error) && cart.romSize() == rom.size(), "ROM loads from a zip");
    check(cart.read(0x0000) == 0x40 && cart.read(0x4000) == 0x41, "first ROM entry in the zip is used");

    writeZip(path, {{"LEEME.txt", {'h', 'o', 'l', 'a'}}});
    check(!cart.loadFile(path, &error) && !error.empty(), "zip without a ROM is rejected");
    std::remove(path.c_str());
}

}  // namespace

int main() {
    testMapper();
    testTileRendering();
    testPlanes();
    testEdgeDistance();
    testVdpInterrupt();
    testPsg();
    testZipRom();
    std::printf(failures ? "%d fallos\n" : "todo bien\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
