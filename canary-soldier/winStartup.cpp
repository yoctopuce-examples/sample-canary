/*********************************************************************
 *
 *  Yoctopuce Keep-it-stupid-simple canary demo software
 *
 *  Windows entry point. Can run as a regular program or as a service
 *
 *  inspired from
 *  https://docs.microsoft.com/en-us/windows/win32/services/the-complete-service-sample?redirectedfrom=MSDN
 *
**********************************************************************/

#include <windows.h>
#include <iostream>
#include <algorithm>
#include "main.h"

#pragma comment(lib, "advapi32.lib")

#define SVCNAME "yellowsvc"

SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;
HANDLE                  ghSvcStopEvent = NULL;

HANDLE                  diskdev;
LONGLONG                prevBytesRead;
LONGLONG                prevBytesWritten;

VOID SvcInstall(void);
VOID SvcDelete(void);
VOID SvcStart(void);
VOID SvcStop(void);
VOID WINAPI SvcCtrlHandler(DWORD);
VOID WINAPI SvcMain(DWORD, LPTSTR*);

VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcInit(DWORD, LPTSTR*);
VOID SvcReportEvent(LPTSTR);

//   Main windows entrypoint that will be used by app and service
//
int winSetup()
{
    WSADATA wsaData;
    int     iResult;
    DISK_PERFORMANCE disk_info{ };
    DWORD bytes;

    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "Unable to start Windows Socket" << std::endl;
        return -1;
    }

    // Prepare to collect disk access statistics
    diskdev = CreateFileA("\\\\.\\C:", FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (diskdev == INVALID_HANDLE_VALUE) {
        std::cerr << "Error opening disk to measure I/O access" << std::endl;
        return -1;
    }
    if (!DeviceIoControl(diskdev, IOCTL_DISK_PERFORMANCE,
        NULL, 0, &disk_info, sizeof(disk_info), &bytes, NULL)) {
        std::cerr << "Failure in DeviceIoControl IOCTL_DISK_PERFORMANCE" << std::endl;
        return -1;
    }
    prevBytesRead = disk_info.BytesRead.QuadPart;
    prevBytesWritten = disk_info.BytesRead.QuadPart;

    return CanarySetup();
}

void help(void)
{
    std::cout << "valid command line arguments:\n";
    std::cout << " -help : show help\n";
    std::cout << " -install : install program as a Windows service\n";
    std::cout << " -uninstall : remove service\n";
    std::cout << " -start : start service\n";
    std::cout << " -stop : stop service\n";
}

int __cdecl main(int argc, char *argv[])
{
    std::cout << "Hint: use option -help for information about options." << std::endl;
    bool svcCmdfound = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        std::transform(arg.begin(), arg.end(), arg.begin(), ::toupper);
        if (arg == "-HELP") {
            help();
        } else if (arg == "-INSTALL") {
            svcCmdfound = true;  SvcInstall();
        } else if (arg == "-UNINSTALL") {
            svcCmdfound = true;  SvcDelete();;
        } else if (arg == "-START") {
            svcCmdfound = true;   SvcStart();
        } else if (arg == "-STOP") {
            svcCmdfound = true;   SvcStop();
        } else {
            std::cout << "\nUnknown argument : " << arg << "\n";
            help();
            return 0;
        }
    }
    if (svcCmdfound) return 0;

    SERVICE_TABLE_ENTRYA DispatchTable[] = {
        { SVCNAME, (LPSERVICE_MAIN_FUNCTIONA)SvcMain },
        { NULL, NULL }
    };

    // When started as a service, this call returns TRUE when the service has stopped. 
    // The process should simply terminate when the call returns.
    if (!StartServiceCtrlDispatcherA(DispatchTable)) {
        // The call fails when we are running as a command line
        if (winSetup()) {
            std::cerr << "Init failed" << std::endl;
        } else while (1) {
            CanaryRun();
        }
    }
}

//
//   Installs a service in the SCM database
//
VOID SvcInstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    char szPath[MAX_PATH];

    if (!GetModuleFileNameA(NULL, szPath, MAX_PATH)) {
        std::cerr << "Cannot retrieve program path" << std::endl;
        return;
    }

    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (NULL == schSCManager) {
        std::cerr << "OpenSCManager failed (run as administrator!)" << std::endl;
        return;
    }

    schService = CreateServiceA(
        schSCManager,              // SCM database 
        SVCNAME,                   // name of service 
        SVCNAME,                   // service name to display 
        SERVICE_ALL_ACCESS,        // desired access 
        SERVICE_WIN32_OWN_PROCESS, // service type 
        SERVICE_AUTO_START,        // start type 
        SERVICE_ERROR_NORMAL,      // error control type 
        szPath,                    // path to service's binary 
        NULL,                      // no load ordering group 
        NULL,                      // no tag identifier 
        NULL,                      // no dependencies 
        NULL,                      // LocalSystem account 
        NULL);                     // no password 
    if (schService == NULL) {
        std::cerr << "CreateService failed" << std::endl;
        CloseServiceHandle(schSCManager);
        return;
    } else {
        std::cout << "Service installed successfully" << std::endl;
    }
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

VOID  SvcDelete(void)
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;

    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (NULL == schSCManager) {
        std::cerr << "OpenSCManager failed (run as administrator!)" << std::endl;
        return;
    }

    schService = OpenServiceA(schSCManager, SVCNAME, SERVICE_ALL_ACCESS);
    if (schService == NULL) {
        std::cerr << "OpenService failed" << std::endl;
        CloseServiceHandle(schSCManager);
        return;
    }
    
    if (!DeleteService(schService)) {
        std::cerr << "DeleteService failed" << std::endl;
    }
    else {
        std::cout << "Service deleted successfully" << std::endl;
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

//   Starts the service if possible.
//
VOID  SvcStart(void)
{
    SERVICE_STATUS_PROCESS ssStatus;
    DWORD dwOldCheckPoint;
    ULONGLONG dwStartTickCount;
    DWORD dwWaitTime;
    DWORD dwBytesNeeded;
    SC_HANDLE schSCManager;
    SC_HANDLE schService;

    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (NULL == schSCManager) {
        std::cerr << "OpenSCManager failed (run as administrator!)" << std::endl;
        return;
    }

    schService = OpenServiceA(schSCManager, SVCNAME, SERVICE_ALL_ACCESS);
    if (schService == NULL) {
        std::cerr << "OpenService failed" << std::endl;
        CloseServiceHandle(schSCManager);
        return;
    }

    // Check the status in case the service is not stopped. 
    if (!QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO,
        (LPBYTE)&ssStatus, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
        std::cerr << "QueryServiceStatusEx failed" << std::endl;
        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
        return;
    }

    // Check if the service is already running. It would be possible 
    // to stop the service here, but for simplicity this example just returns. 

    if (ssStatus.dwCurrentState != SERVICE_STOPPED && ssStatus.dwCurrentState != SERVICE_STOP_PENDING)
    {
        std::cerr << "Cannot start the service because it is already running" << std::endl;
        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
        return;
    }

    // Save the tick count and initial checkpoint.
    dwStartTickCount = GetTickCount64();
    dwOldCheckPoint = ssStatus.dwCheckPoint;

    // Wait for the service to stop before attempting to start it.
    while (ssStatus.dwCurrentState == SERVICE_STOP_PENDING)
    {
        // Do not wait longer than the wait hint. A good interval is 
        // one-tenth of the wait hint but not less than 1 second  
        // and not more than 10 seconds. 
        dwWaitTime = ssStatus.dwWaitHint / 10;
        if (dwWaitTime < 1000)
            dwWaitTime = 1000;
        else if (dwWaitTime > 10000)
            dwWaitTime = 10000;
        Sleep(dwWaitTime);

        if (!QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO,
            (LPBYTE)&ssStatus, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
            std::cerr << "QueryServiceStatusEx failed" << std::endl;
            CloseServiceHandle(schService);
            CloseServiceHandle(schSCManager);
            return;
        }

        if (ssStatus.dwCheckPoint > dwOldCheckPoint) {
            // Continue to wait and check.
            dwStartTickCount = GetTickCount64();
            dwOldCheckPoint = ssStatus.dwCheckPoint;
        } else if (GetTickCount64() - dwStartTickCount > ssStatus.dwWaitHint) {
            std::cerr << "Timeout waiting for service to stop" << std::endl;
            CloseServiceHandle(schService);
            CloseServiceHandle(schSCManager);
            return;
        }
    }

    // Attempt to start the service.
    if (!StartService(schService, 0, NULL)) {
        std::cerr << "StartService failed" << std::endl;
        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
        return;
    } else {
        std::cout << "Service start pending..." << std::endl;
    }

    if (!QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO,
        (LPBYTE)&ssStatus, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
        std::cerr << "QueryServiceStatusEx failed" << std::endl;
        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
        return;
    }

    // Save the tick count and initial checkpoint.
    dwStartTickCount = GetTickCount64();
    dwOldCheckPoint = ssStatus.dwCheckPoint;

    while (ssStatus.dwCurrentState == SERVICE_START_PENDING) {
        // Do not wait longer than the wait hint. A good interval is 
        // one-tenth the wait hint, but no less than 1 second and no 
        // more than 10 seconds. 
        dwWaitTime = ssStatus.dwWaitHint / 10;
        if (dwWaitTime < 1000)
            dwWaitTime = 1000;
        else if (dwWaitTime > 10000)
            dwWaitTime = 10000;
        Sleep(dwWaitTime);

        // Check the status again. 
        if (!QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO,
            (LPBYTE)&ssStatus, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
            std::cerr << "QueryServiceStatusEx failed" << std::endl;
            CloseServiceHandle(schService);
            CloseServiceHandle(schSCManager);
            return;
        }

        if (ssStatus.dwCheckPoint > dwOldCheckPoint) {
            dwStartTickCount = GetTickCount64();
            dwOldCheckPoint = ssStatus.dwCheckPoint;
        } else {
            if (GetTickCount64() - dwStartTickCount > ssStatus.dwWaitHint) {
                // No progress made within the wait hint.
                break;
            }
        }
    }

    // Determine whether the service is running.
    if (ssStatus.dwCurrentState == SERVICE_RUNNING) {
        std::cout << "Service started successfully." << std::endl;
    } else {
        std::cerr << "Service not started. " << std::endl;
    }
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

//   Stops the service.
//
VOID  SvcStop(void)
{
    SERVICE_STATUS_PROCESS ssp;
    ULONGLONG dwStartTime = GetTickCount64();
    DWORD dwBytesNeeded;
    DWORD dwTimeout = 30000; // 30-second time-out
    DWORD dwWaitTime;
    SC_HANDLE schSCManager;
    SC_HANDLE schService;

    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (NULL == schSCManager) {
        std::cerr << "OpenSCManager failed (run as administrator!)" << std::endl;
        return;
    }

    schService = OpenServiceA(schSCManager, SVCNAME,
        SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS);
    if (schService == NULL) {
        std::cerr << "OpenService failed" << std::endl;
        CloseServiceHandle(schSCManager);
        return;
    }

    // Make sure the service is not already stopped.
    if (!QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO,
        (LPBYTE)&ssp, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
        std::cerr << "QueryServiceStatusEx failed" << std::endl;
        goto stop_cleanup;
    }

    if (ssp.dwCurrentState == SERVICE_STOPPED) {
        std::cerr << "Service is already stopped" << std::endl;
        goto stop_cleanup;
    }

    while (ssp.dwCurrentState == SERVICE_STOP_PENDING) {
        std::cout << "Service stop pending..." << std::endl;
        dwWaitTime = ssp.dwWaitHint / 10;
        if (dwWaitTime < 1000)
            dwWaitTime = 1000;
        else if (dwWaitTime > 10000)
            dwWaitTime = 10000;
        Sleep(dwWaitTime);

        if (!QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO,
            (LPBYTE)&ssp, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
            std::cerr << "QueryServiceStatusEx failed" << std::endl;
            goto stop_cleanup;
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            std::cout << "Service stopped successfully." << std::endl;
            goto stop_cleanup;
        }

        if (GetTickCount64() - dwStartTime > dwTimeout) {
            std::cerr << "Service stop timed out." << std::endl;
            goto stop_cleanup;
        }
    }

    // Send a stop code to the service.
    if (!ControlService(schService, SERVICE_CONTROL_STOP, (LPSERVICE_STATUS)&ssp)) {
        std::cerr << "ControlService failed" << std::endl;
        goto stop_cleanup;
    }

    // Wait for the service to stop.
    while (ssp.dwCurrentState != SERVICE_STOPPED) {
        Sleep(ssp.dwWaitHint);
        if (!QueryServiceStatusEx(schService, SC_STATUS_PROCESS_INFO,
            (LPBYTE)&ssp, sizeof(SERVICE_STATUS_PROCESS), &dwBytesNeeded)) {
            std::cerr << "QueryServiceStatusEx failed" << std::endl;
            goto stop_cleanup;
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED)
            break;

        if (GetTickCount64() - dwStartTime > dwTimeout) {
            std::cerr << "Service stop timed out." << std::endl;
            goto stop_cleanup;
        }
    }
    std::cout << "Service stopped successfully" << std::endl;

stop_cleanup:
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}


//
// Purpose: 
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None.
//
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
    // Register the handler function for the service
    gSvcStatusHandle = RegisterServiceCtrlHandlerA(SVCNAME, SvcCtrlHandler);
    if (!gSvcStatusHandle) return;

    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gSvcStatus.dwServiceSpecificExitCode = 0;
    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Create an event. The control handler function, SvcCtrlHandler,
    // signals this event when it receives the stop control code.
    ghSvcStopEvent = CreateEvent(
        NULL,    // default security attributes
        TRUE,    // manual reset event
        FALSE,   // not signaled
        NULL);   // no name

    if (ghSvcStopEvent == NULL) {
        ReportSvcStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    if (winSetup()) {
        // Init error
        ReportSvcStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    // Report running status when initialization is complete.
    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

    // Perform work until service stops.
    DWORD state;
    do {
        CanaryRun();
        state = WaitForSingleObject(ghSvcStopEvent, 1);
    } while (state != WAIT_OBJECT_0);

    ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

// Purpose: 
//   Sets the current service status and reports it to the SCM.
//
// Parameters:
//   dwCurrentState - The current state (see SERVICE_STATUS)
//   dwWin32ExitCode - The system error code
//   dwWaitHint - Estimated time for pending operation, in milliseconds
// 
VOID ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING)
        gSvcStatus.dwControlsAccepted = 0;
    else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    if ((dwCurrentState == SERVICE_RUNNING) ||
        (dwCurrentState == SERVICE_STOPPED))
        gSvcStatus.dwCheckPoint = 0;
    else gSvcStatus.dwCheckPoint = dwCheckPoint++;

    // Report the status of the service to the SCM.
    SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
    switch (dwCtrl) {
    case SERVICE_CONTROL_STOP:
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
        SetEvent(ghSvcStopEvent);
        ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);
        return;
    default:
        break;
    }
}

int GetDiskStats(double *mb_read, double *mb_written)
{
    DISK_PERFORMANCE disk_info{ };
    DWORD bytes;

    if (!DeviceIoControl(diskdev, IOCTL_DISK_PERFORMANCE,
        NULL, 0, &disk_info, sizeof(disk_info), &bytes, NULL)) {
        std::cerr << "Failure in DeviceIoControl" << std::endl;
        return -1;
    }

    *mb_read = (disk_info.BytesRead.QuadPart - prevBytesRead) / 1.0e6;
    *mb_written = (disk_info.BytesWritten.QuadPart - prevBytesWritten) / 1.0e6;
    prevBytesRead = disk_info.BytesRead.QuadPart;
    prevBytesWritten = disk_info.BytesWritten.QuadPart;

    return 0;
}