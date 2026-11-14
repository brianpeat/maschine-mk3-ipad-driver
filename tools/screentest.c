// Draws a test pattern on both Maschine MK3 screens (USB interface 5, bulk OUT endpoint 4).
// Each screen is 480 x 272, RGB565 with the high byte first. One transfer per screen:
//   16-byte header [84 00 <display> 60 00 00 00 00  x y w h (16-bit big-endian)]
//   command [00, half the pixel count (24-bit big-endian)], then the pixels,
//   then [03 00 00 00] (blit) and [40 00 00 00] (end).
// The NI background helpers hold this interface while they run, so quit them first.
// Build: clang -o screentest screentest.c -framework IOKit -framework CoreFoundation
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 480
#define H 272

static uint16_t rgb(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

static void put(uint8_t *px, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    px[(y * W + x) * 2] = c >> 8; px[(y * W + x) * 2 + 1] = c & 0xFF;
}

static void pattern(uint8_t *px, int display) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        uint16_t c;
        if (display == 0) {                       // left screen: red, green, blue thirds
            c = x < W / 3 ? rgb(255, 0, 0) : x < 2 * W / 3 ? rgb(0, 255, 0) : rgb(0, 0, 255);
        } else {                                  // right screen: eight grey steps, dark to light
            int step = x / (W / 8); c = rgb(step * 36, step * 36, step * 36);
        }
        if (x < 4 || y < 4 || x >= W - 4 || y >= H - 4) c = rgb(255, 255, 255);   // white border
        put(px, x, y, c);
    }
    for (int y = 4; y < 44; y++) for (int x = 4; x < 44; x++) put(px, x, y, rgb(255, 255, 0));   // yellow square, top-left
    for (int i = 0; i < H; i++) { put(px, i, i, rgb(255, 255, 255)); put(px, i + 1, i, rgb(255, 255, 255)); }  // diagonal from top-left
}

int main(void) {
    CFMutableDictionaryRef m = IOServiceMatching("IOUSBHostDevice");
    int v = 0x17cc, p = 0x1600;
    CFNumberRef nv = CFNumberCreate(0, kCFNumberIntType, &v), np = CFNumberCreate(0, kCFNumberIntType, &p);
    CFDictionarySetValue(m, CFSTR("idVendor"), nv); CFDictionarySetValue(m, CFSTR("idProduct"), np);
    io_service_t dev = IOServiceGetMatchingService(kIOMainPortDefault, m);
    if (!dev) { fprintf(stderr, "Maschine MK3 not found\n"); return 1; }
    IOCFPlugInInterface **pl; SInt32 score;
    if (IOCreatePlugInInterfaceForService(dev, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &pl, &score)) { fprintf(stderr, "device plugin failed\n"); return 1; }
    IOUSBDeviceInterface **d; (*pl)->QueryInterface(pl, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (void **)&d);

    IOUSBFindInterfaceRequest req = { kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare };
    io_iterator_t it; (*d)->CreateInterfaceIterator(d, &req, &it);
    IOUSBInterfaceInterface **intf = NULL; io_service_t s;
    while ((s = IOIteratorNext(it))) {
        IOCFPlugInInterface **ipl; IOUSBInterfaceInterface **i;
        if (IOCreatePlugInInterfaceForService(s, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &ipl, &score)) continue;
        (*ipl)->QueryInterface(ipl, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID), (void **)&i);
        UInt8 n = 0xFF; (*i)->GetInterfaceNumber(i, &n);
        if (n == 5) { intf = i; break; }
        (*i)->Release(i);
    }
    if (!intf) { fprintf(stderr, "display interface not found\n"); return 1; }
    kern_return_t kr = (*intf)->USBInterfaceOpen(intf);
    if (kr) { fprintf(stderr, "could not open the display interface (0x%x) - is an NI helper still running?\n", kr); return 2; }

    UInt8 pipes = 0, pipe = 0; (*intf)->GetNumEndpoints(intf, &pipes);
    for (UInt8 r = 1; r <= pipes; r++) {
        UInt8 dir, num, type, interval; UInt16 max;
        (*intf)->GetPipeProperties(intf, r, &dir, &num, &type, &max, &interval);
        if (dir == kUSBOut && num == 4) pipe = r;
    }
    if (!pipe) { fprintf(stderr, "bulk OUT endpoint 4 not found\n"); return 1; }

    const size_t pixels = W * H, total = 16 + 4 + pixels * 2 + 8;
    uint8_t *buf = malloc(total);
    for (int display = 0; display < 2; display++) {
        const uint8_t header[16] = { 0x84, 0, (uint8_t)display, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, W >> 8, W & 0xFF, H >> 8, H & 0xFF };
        const size_t half = pixels / 2;
        memcpy(buf, header, 16);
        buf[16] = 0; buf[17] = (half >> 16) & 0xFF; buf[18] = (half >> 8) & 0xFF; buf[19] = half & 0xFF;
        pattern(buf + 20, display);
        const uint8_t tail[8] = { 0x03, 0, 0, 0, 0x40, 0, 0, 0 };
        memcpy(buf + 20 + pixels * 2, tail, 8);
        kr = (*intf)->WritePipeTO(intf, pipe, buf, (UInt32)total, 3000, 3000);
        fprintf(stderr, "screen %d: write result 0x%x\n", display, kr);
    }
    // Partial update: an orange 120 x 60 rectangle at (180, 100) on each screen, to check that
    // the x, y, width and height fields in the header work for small redraws.
    for (int display = 0; display < 2; display++) {
        const int x = 180, y = 100, w = 120, h = 60;
        const uint8_t header[16] = { 0x84, 0, (uint8_t)display, 0x60, 0, 0, 0, 0, x >> 8, x & 0xFF, y >> 8, y & 0xFF, w >> 8, w & 0xFF, h >> 8, h & 0xFF };
        const size_t count = (size_t)w * h, half = count / 2;
        memcpy(buf, header, 16);
        buf[16] = 0; buf[17] = (half >> 16) & 0xFF; buf[18] = (half >> 8) & 0xFF; buf[19] = half & 0xFF;
        for (size_t i = 0; i < count; i++) { buf[20 + i * 2] = rgb(255, 140, 0) >> 8; buf[21 + i * 2] = rgb(255, 140, 0) & 0xFF; }
        const uint8_t tail[8] = { 0x03, 0, 0, 0, 0x40, 0, 0, 0 };
        memcpy(buf + 20 + count * 2, tail, 8);
        kr = (*intf)->WritePipeTO(intf, pipe, buf, (UInt32)(20 + count * 2 + 8), 3000, 3000);
        fprintf(stderr, "screen %d: partial write result 0x%x\n", display, kr);
    }
    (*intf)->USBInterfaceClose(intf);
    return 0;
}
