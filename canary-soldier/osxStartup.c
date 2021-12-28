/*********************************************************************
 *
 *  Yoctopuce Keep-it-stupid-simple canary demo software
 *
 *  macOS entry point. Can run as a regular program or as a service
 *
 *
 **********************************************************************/

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <stdio.h>
#include <string.h>
#include "main.h"

io_iterator_t drivelist = IO_OBJECT_NULL;

int main (int argc, char * argv[])
{
    // get ports and services for drive stats
    mach_port_t masterPort = IO_OBJECT_NULL;
    IOMasterPort(bootstrap_port, &masterPort);
    IOServiceGetMatchingServices(masterPort, IOServiceMatching("IOBlockStorageDriver"), &drivelist);

    // Make a dry run of GetDiskStats to reset relative counters
    // and to verify that we have proper access
    double read, written;
    if(GetDiskStats(&read, &written) < 0) {
        return 1;
    }

    if(CanarySetup()) {
        fprintf(stderr, "\nInit failed\n");
        return 1;
    }
    while(1) {
        CanaryRun();
    }
}

UInt64 prevBytesRead = 0;
UInt64 prevBytesWritten = 0;

// Retrieve macOS disk I/O information from IOKit, as described in
// https://stackoverflow.com/questions/11962296/how-can-i-retrieve-read-write-disk-info
int GetDiskStats(double *mb_read, double *mb_written)
{
    io_registry_entry_t drive = 0;          /* needs release */
    UInt64 bytesRead = 0;
    UInt64 bytesWritten = 0;

    while ((drive = IOIteratorNext(drivelist))) {
        CFNumberRef number = 0;             /* don't release */
        CFDictionaryRef properties = 0;     /* needs release */
        CFDictionaryRef statistics = 0;     /* don't release */
        UInt64 value = 0;

        /* Obtain the properties for this drive object */
        IORegistryEntryCreateCFProperties(drive, (CFMutableDictionaryRef *) &properties, kCFAllocatorDefault, kNilOptions);

        /* Obtain the statistics from the drive properties */
        statistics = (CFDictionaryRef) CFDictionaryGetValue(properties, CFSTR(kIOBlockStorageDriverStatisticsKey));

        if (statistics) {
            /* Obtain the number of bytes read from the drive statistics */
            number = (CFNumberRef) CFDictionaryGetValue(statistics, CFSTR(kIOBlockStorageDriverStatisticsBytesReadKey));
            if (number) {
                CFNumberGetValue(number, kCFNumberSInt64Type, &value);
                bytesRead += value;
            }

            /* Obtain the number of bytes written from the drive statistics */
            number = (CFNumberRef) CFDictionaryGetValue (statistics, CFSTR(kIOBlockStorageDriverStatisticsBytesWrittenKey));
            if (number) {
                CFNumberGetValue(number, kCFNumberSInt64Type, &value);
                bytesWritten += value;
            }
        }
        CFRelease(properties); properties = 0;
        IOObjectRelease(drive); drive = 0;

    }
    IOIteratorReset(drivelist);

    *mb_read = (bytesRead - prevBytesRead) / 1024.0e3;
    *mb_written = (bytesWritten - prevBytesWritten) / 1024.0e3;
    prevBytesRead = bytesRead;
    prevBytesWritten = bytesWritten;

    return 0;
}
