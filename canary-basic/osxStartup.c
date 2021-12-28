/*********************************************************************
 *
 *  Yoctopuce Keep-it-stupid-simple canary demo software
 *
 *  macOS entry point. Can run as a regular program or as a service
 *
 *
 **********************************************************************/

#include <stdio.h>
#include <string.h>
#include "main.h"

int main (int argc, char * argv[])
{
    if(CanarySetup()) {
        fprintf(stderr, "\nInit failed\n");
        return 1;
    }
    while(1) {
        CanaryRun();
    }
}

