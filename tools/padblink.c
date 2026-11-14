// Blinks one Maschine MK3 pad between two colours by sending HID output report 0x81
// (25 touch-strip LEDs followed by 16 pad LEDs; each byte = colour index << 2 | brightness 0-3).
// Usage: padblink <pad 0-15, 0 = top-left> <seconds>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int pad = argc > 1 ? atoi(argv[1]) : 12, seconds = argc > 2 ? atoi(argv[2]) : 10;
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    int vid = 0x17cc, pid = 0x1600;
    CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &vid), p = CFNumberCreate(NULL, kCFNumberIntType, &pid);
    const void *keys[] = { CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey) }, *vals[] = { v, p };
    CFDictionaryRef match = CFDictionaryCreate(NULL, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(manager, match);
    IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    CFSetRef set = IOHIDManagerCopyDevices(manager);
    if (!set || CFSetGetCount(set) == 0) { fprintf(stderr, "Maschine MK3 not found\n"); return 1; }
    IOHIDDeviceRef device; CFSetGetValues(set, (const void **)&device);
    fprintf(stderr, "open 0x%x\n", IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone));
    const uint8_t colours[2] = { (1 << 2) | 3, (11 << 2) | 3 };
    for (int i = 0; i < seconds * 2; i++) {
        uint8_t report[42]; memset(report, 0, sizeof(report));
        report[0] = 0x81;
        report[1 + 25 + pad] = colours[i & 1];
        IOReturn r = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0x81, report, sizeof(report));
        if (i == 0) fprintf(stderr, "set report 0x%x\n", r);
        usleep(500000);
    }
    return 0;
}
