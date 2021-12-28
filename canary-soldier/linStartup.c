/*********************************************************************
 *
 *  Yoctopuce Keep-it-stupid-simple canary demo software
 *
 *  Linux entry point. Can run as a regular program or as a service
 *
 *
 **********************************************************************/

#include <err.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <cstdio>
#include <cstring>
#include "main.h"

#define PIDFILE "/var/run/yellowsvc.pid"

const char * systemd_scipt=
    "[Unit]\n"
    "Description=yellowscv\n"
    "After=network.target\n"
    "\n"
    "[Service]\n"
    "ExecStart=%s -d\n"
    "Type=simple\n"
    "\n"
    "[Install]\n"
    "WantedBy=multi-user.target\n";

static int InstallSystemD(int install, const char *exe)
{
    if (access("/etc/systemd/system/", W_OK) < 0){
       fprintf(stderr, "Permission denied (use sudo!)\n");
       return -1;
    }

    if (install) {
        char self[2048];
        FILE *f;

        int size = readlink("/proc/self/exe", self, sizeof(self));
        if (size >= 0) {
            self[size] = 0;
            exe = self;
        }
        f = fopen("/etc/systemd/system/yellowsvc.service", "w");
        if (!f < 0) {
            fprintf(stderr, "Failed to rewrite service file\n");
            return -1;
        }
        fprintf(f, systemd_scipt, exe);
        fclose(f);
        if (system("systemctl daemon-reload") < 0) {
            fprintf(stderr, "Failed to run deamon-reload\n");
            return -1;
        }
        if (system("systemctl start yellowsvc.service") < 0) {
            fprintf(stderr, "Failed to start yellowsvc.service\n");
            return -1;
        }
        if (system("systemctl enable yellowsvc.service") < 0) {
            fprintf(stderr, "Failed to enable yellowsvc.service\n");
            return -1;
        }
    } else {
        if (system("systemctl stop yellowsvc.service") < 0) {
            fprintf(stderr, "Failed to stop yellowsvc.service\n");
            return -1;
        }
        if (system("systemctl disable yellowsvc.service") < 0) {
            fprintf(stderr, "Failed to disable yellowsvc.service\n");
            return -1;
        }
    }
    return 0;
}

int InstallService(int install, const char *exe)
{
    if (access("/etc/systemd/system.conf", F_OK) == 0){
        return InstallSystemD(install, exe);
    } else {
        fprintf(stderr, "Unsupported init.d system\n");
        return -1;
    }
}

void help(void)
{
    fprintf(stdout, "valid command line arguments:\n");
    fprintf(stdout, " -h : show help\n");
    fprintf(stdout, " -i : install as a systemd service\n");
    fprintf(stdout, " -u : disable service\n");
    fprintf(stdout, " -d : run as daemon\n");
}

int main(int argc, char* argv[])
{
    int runAsDaemon = 0;

    fprintf(stdout, "Hint: use option -h for information about options.\n");
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] != '-' || !arg[1] || arg[2]) {
            fprintf(stderr, "\nUnknown argument: %s\n", arg);
            help();
            return 1;
        }
        switch(arg[1]) {
            case 'h': 
                help(); 
                return 0;
            case 'i': 
                return InstallService(1, argv[0]); 
            case 'u':
                return InstallService(0, argv[0]);
            case 'd': 
                runAsDaemon = 1;
                break;
            default:
                fprintf(stderr, "\nUnknown argument: %s\n", arg);
                help();
                return 1;
        }
    }

    // Make a dry run of GetDiskStats to reset relative counters
    // and to verify that we have proper access
    double read, written;
    if(GetDiskStats(&read, &written) < 0) {
        return 1;
    }

    if(runAsDaemon) {
#ifdef CREATE_PIDFILE
        struct  pidfh *pfh;
        pid_t   otherpid, childpid;
        pfh = pidfile_open(PIDFILE, 0600, &otherpid);
        if (pfh == NULL) {
            if (errno == EEXIST) {
                errx(1, "yellowcv already running, pid: %jd.",(intmax_t)otherpid);
            }
            /* If we cannot create pidfile from other reasons, only warn. */
            warn("Cannot open or create pidfile");
        }
        if (daemon(0, 0) == -1) {
            warn("Cannot daemonize");
            pidfile_remove(pfh);
            exit(1);
        }
        pidfile_write(pfh);
        if(CanarySetup()) {
            while(1) {
                CanaryRun();
            }
        }
        pidfile_remove(pfh);
        exit(1);
#else
        if (daemon(0, 0) < 0) {
            exit(1);
        }
#endif
    }

    if(CanarySetup()) {
        fprintf(stderr, "\nInit failed\n");
        return 1;
    }
    while(1) {
        CanaryRun();
    }
}


double prevMbRead = 0;
double prevMbWritten = 0;

int GetDiskStats(double *mb_read, double *mb_written)
{
    FILE *f;
    char buff[32768], *end, *p, *eol;
    size_t len;
    double mbRead = 0;
    double mbWritten = 0;
    int i;

    f = fopen("/proc/diskstats", "r");
    if(!f) {
        fprintf(stderr, "Error opening /proc/diskstats");
        return -1;
    }
    end = buff + fread(buff, 1, sizeof(buff), f);
    fclose(f);

    // scan the buffer line by line, in order to
    // sum the number of bytes read/written to hda,hdb,sda,sdb,etc.
    for(p = buff; p < end; p = eol+1) {
        // search for end of line
        for(eol = p; eol < end && *eol != '\n';) eol++;
        // skip over field 1 (device major number)
        while(p < eol && *p == ' ') p++;
        while(p < eol && *p != ' ') p++;
        while(p < eol && *p == ' ') p++;
        if(p >= eol) continue;
        // ignore lines with a non-zero field 2 (device minor number)
        if(p[0] != '0') continue;
        // skip over field 2 (device minor number)
        while(p < eol && *p++ != ' ');
        if(p >= eol) continue;
        // ignore lines where field 3 does not match [hs]d[a-h] or mmcblk0
        if(memcmp(p, "mmcblk0", 7)) {
            if(p[0] != 'h' && p[0] != 's') continue;
            if(p[1] != 'd') continue;
            if(p[2] < 'a' || p[2] > 'h') continue;
        }
        // skip over fields 3-5
        while(p < eol && *p++ != ' ');
        while(p < eol && *p++ != ' ');
        while(p < eol && *p++ != ' ');
        if(p >= eol) continue;
        // add the number of sectors read
        mbRead += atof(p) / 2048;
        // skip over fields 6-9
        while(p < eol && *p++ != ' ');
        while(p < eol && *p++ != ' ');
        while(p < eol && *p++ != ' ');
        while(p < eol && *p++ != ' ');
        if(p >= eol) continue;
        // add the number of sectors written
        mbWritten += atof(p) / 2048;
    }

    *mb_read = mbRead - prevMbRead;
    *mb_written = mbWritten - prevMbWritten;
    prevMbRead = mbRead;
    prevMbWritten = mbWritten;

    return 0;
}
