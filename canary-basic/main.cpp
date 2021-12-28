/*********************************************************************
 *
 *  Yoctopuce Keep-it-stupid-simple canary demo software
 *
 *  Entry point is in an OS-specific separate file
 *  - winStartup.cpp for Windows
 *  - linStartup.cpp for Linux
 *  - osxStartup.cpp for macOS
 *
 *********************************************************************/

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <WinBase.h>
#else
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket(s) close(s)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

#include <iostream>
#include <cstring>
#include <ctype.h>
#include <stdlib.h>
#include "YoctoLib/yocto_api.h"
#include "YoctoLib/yocto_buzzer.h"
#include "YoctoLib/yocto_colorled.h"
#include "main.h"

static unsigned portList[] = {
    20, 21, 22, 23, 25, 53, 80, 110, 111, 135, 139, 143, 443,
    445, 636, 989, 990, 993, 995, 1723, 3306, 3389, 5900, 8080
};
#define PORT_COUNT (sizeof(portList) / sizeof(unsigned))

static YBuzzer *buz = NULL;
static YColorLed *led = NULL;
static SOCKET sockets[PORT_COUNT];
static SOCKET maxsock = INVALID_SOCKET;
static u64 alarmCount = 0;
static u64 lastBlink = 0;
static u64 lastChirp = 0;

void RaiseAlarm(char* sourceIP, unsigned targetPort, SOCKET incoming);

int CanarySetup(void)
{
    string  errmsg;
    SOCKET  sock;
    int     i, optval, nsockets;
    struct  sockaddr_in addr;
    string  line;

    // Setup the API to use local USB devices
    if (YAPI::RegisterHub("usb", errmsg) != YAPI_SUCCESS) {
        std::cerr << "Failed to open USB port: " << errmsg << std::endl;
        return 1;
    }

    // Look for a Yocto-MaxiBuzzer to raise alarms
    buz = YBuzzer::FirstBuzzer();
    led = YColorLed::FirstColorLed();
    if (buz == NULL || led == NULL) {
        std::cerr << "Yocto-MaxiBuzzer not found (check USB cable !)" << std::endl;
        YAPI::FreeAPI();
        return 1;
    }

    // Initialize the socket list to invalid sockets
    for (i = 0; i < PORT_COUNT; i++) {
        sockets[i] = INVALID_SOCKET;
    }

    // Setup as many socket listeners from the list as possible
    line = "";
    nsockets = 0;
    for (i = 0; i < PORT_COUNT; i++) {
        sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            std::cerr << "No more socket" << std::endl;
            break;
        }
        optval = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(portList[i]);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cout << "Cannot bind to port " << portList[i] << ", skipping" << std::endl;
            closesocket(sock);
            continue;
        }
        if (listen(sock, 16) < 0) {
            std::cerr << "Cannot listen on port " << portList[i] << ", skipping" << std::endl;
            closesocket(sock);
            continue;
        }
        sockets[i] = sock;
        nsockets++;
        if (maxsock == INVALID_SOCKET) {
            maxsock = sock;
        } else if (maxsock < sock) {
            maxsock = sock;
        }
        line += " " + std::to_string(portList[i]);
    }
    if (nsockets == 0) {
        std::cerr << "Could not bind to any port, nothing to do" << std::endl;
        return 1;
    }
    std::cout << "Listening on ports" << line << std::endl;

    return 0;
}

int CanaryRun(void)
{
    string          errmsg;
    fd_set          fds;
    struct timeval  timeout;
    int             i, res;

    // While there is no alarm, flash a short green every while to show that the canary is living
    YAPI::HandleEvents(errmsg);
    if (alarmCount == 0 && YAPI::GetTickCount() - lastBlink > 5*1000) {
        led->set_rgbColor(0x00ff00);
        YAPI::Sleep(5, errmsg);
        led->set_rgbColor(0);
        lastBlink = YAPI::GetTickCount();
        if (YAPI::GetTickCount() - lastChirp > 15*60*1000) {
            buz->resetPlaySeq();
            buz->addVolMoveToPlaySeq(20, 0);
            buz->addPulseToPlaySeq(3000, 10);
            buz->addFreqMoveToPlaySeq(5000, 10);
            buz->addPulseToPlaySeq(5000, 10);
            buz->oncePlaySeq();
            lastChirp = YAPI::GetTickCount();
        }
    }

    // Wait for incoming connections on all listening sockets
    FD_ZERO(&fds);
    for (i = 0; i < PORT_COUNT; i++) {
        if (sockets[i] != INVALID_SOCKET) {
            FD_SET(sockets[i], &fds);
        }
    }
    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000; // 100 ms
    res = select((int)maxsock + 1, &fds, NULL, NULL, &timeout);
    if (res > 0) {
        for (i = 0; i < PORT_COUNT; i++) {
            if (sockets[i] != INVALID_SOCKET && FD_ISSET(sockets[i], &fds)) {
                // Knock knock...
                struct sockaddr_in  remote;
                socklen_t           addrlen;
                SOCKET              newsock;
                char                ipaddrstr[20];

                alarmCount++;
                addrlen = sizeof(remote);
                newsock = accept(sockets[i], (struct sockaddr*)&remote, &addrlen);
                memset(ipaddrstr, 0, sizeof(ipaddrstr));
                if (newsock != INVALID_SOCKET) {
                    inet_ntop(remote.sin_family, &remote.sin_addr, ipaddrstr, sizeof(ipaddrstr));
                }
                RaiseAlarm(ipaddrstr, portList[i], newsock);
                if (newsock == INVALID_SOCKET) {
                    closesocket(newsock);
                }
            }
        }
    }

    // Return in the main loop in case our service is asked to stop
    return 0;
}

void RaiseAlarm(char* sourceIP, unsigned targetPort, SOCKET incoming)
{
    buz->resetPlaySeq();
    buz->addVolMoveToPlaySeq(50, 0);        // Set volume to 50% (100% is too loud for us...)
    buz->addFreqMoveToPlaySeq(500, 0);      // Start at 500 Hz
    buz->addFreqMoveToPlaySeq(5000, 1000);  // Ramp up to 5 kHz in 1000 ms
    buz->addFreqMoveToPlaySeq(500, 1000);   // Ramp down to 500 Hz in 1000 ms
    buz->startPlaySeq();                    // Run in loop
    led->addRgbMoveToBlinkSeq(0xff0000, 300);   // Ramp up Red in 300 ms
    led->addRgbMoveToBlinkSeq(0x000000, 300);   // Ramp down to black in 300ms
    led->startBlinkSeq();                       // Run in loop
    std::cout << "Alarm: incoming connection from " << sourceIP << " on port " << targetPort << std::endl;
}
