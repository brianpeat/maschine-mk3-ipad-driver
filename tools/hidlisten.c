// Prints every HID input report from a Maschine MK3 (17cc:1600) as hex, with a timestamp.
// Usage: hidlisten <seconds>      Build: clang -o hidlisten hidlisten.c -framework IOKit -framework CoreFoundation
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <mach/mach_time.h>

static uint8_t buffer[256];
static double t0;
static double now(void) { static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9; }

static void onReport(void *ctx, IOReturn result, void *sender, IOHIDReportType type, uint32_t reportID,
                     uint8_t *report, CFIndex length) {
    printf("%8.3f id=%02x len=%ld :", now() - t0, reportID, (long)length);
    for (CFIndex i = 0; i < length; i++) printf(" %02x", report[i]);
    printf("\n");
}

static void onDevice(void *ctx, IOReturn result, void *sender, IOHIDDeviceRef device) {
    fprintf(stderr, "device found, open result 0x%x\n", IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone));
    IOHIDDeviceRegisterInputReportCallback(device, buffer, sizeof(buffer), onReport, NULL);
}

int main(int argc, char **argv) {
    double seconds = argc > 1 ? atof(argv[1]) : 10;
    setvbuf(stdout, NULL, _IOLBF, 0);
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    int vid = 0x17cc, pid = 0x1600;
    CFNumberRef v = CFNumberCreate(NULL, kCFNumberIntType, &vid), p = CFNumberCreate(NULL, kCFNumberIntType, &pid);
    const void *keys[] = { CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey) }, *vals[] = { v, p };
    CFDictionaryRef match = CFDictionaryCreate(NULL, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(manager, match);
    IOHIDManagerRegisterDeviceMatchingCallback(manager, onDevice, NULL);
    IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    fprintf(stderr, "manager open 0x%x\n", IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone));
    t0 = now();
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
    return 0;
}
