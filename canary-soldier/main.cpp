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
#include "YoctoLib/yocto_serialport.h"
#include "main.h"

static unsigned portList[] = {
    20, 21, 22, 23, 25, 53, 80, 110, 111, 135, 139, 143, 443,
    445, 636, 989, 990, 993, 995, 1723, 3306, 3389, 5900, 8080
};
#define PORT_COUNT (sizeof(portList) / sizeof(unsigned))

static YSerialPort *rs485 = NULL;
static SOCKET sockets[PORT_COUNT];
static SOCKET maxsock = INVALID_SOCKET;
static string linehdr = "";
static u64 lastPing = 0;

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

    // Look for a Yocto-RS485 to report status to commander node
    rs485 = YSerialPort::FirstSerialPort();
    if (rs485 == NULL) {
        std::cerr << "Yocto-RS485 not found (check USB cable !)" << std::endl;
        YAPI::FreeAPI();
        return 1;
    }

    // Use the Yocto-RS485 logical name as line header for reports
    linehdr = rs485->get_module()->get_logicalName()+";";

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
    rs485->writeLine(linehdr + "checking ports" + line);

    return 0;
}

int CanaryRun(void)
{
    string          errmsg;
    fd_set          fds;
    struct timeval  timeout;
    int             i, res;

    // Every 15 seconds, sends the amount of disk I/O access
    YAPI::HandleEvents(errmsg);
    if (YAPI::GetTickCount() - lastPing > 15*1000) {
        string line;
        double mb_read = 0.0, mb_written = 0.0;
        GetDiskStats(&mb_read, &mb_written);
        line = linehdr + "io;" + std::to_string(mb_read) + ";" + std::to_string(mb_written);
        rs485->writeLine(line);
        lastPing = YAPI::GetTickCount();
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
    string line = linehdr + "alarm;" + string(sourceIP) + ";" + std::to_string(targetPort);
    rs485->writeLine(line);
    std::cout << "Alarm: incoming connection from " << sourceIP << " on port " << targetPort << std::endl;
}
