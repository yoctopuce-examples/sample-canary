Content of this Example:
=======================

---|---
main.cpp          | Main source file of the canary software
main.h            | Interface between main.c and the OS-specific code
winStartup.cpp    | Windows startup code (including run-as-service)
linStartup.c      | Linux startup code (including run-as-service)
osxStartup.c      | macOS startup code (no run-as-service code, sorry!)
GNUmakefile       | Makefile for Linux/macOS
VisualStudio/     | Visual Studio 2019 project 
Xcode/            | Xcode Project for macOS
YoctoLib/*        | Source code for (a part of) Yoctopuce Library 
Binaries/*        | Directory for YoctoLib intermediate object files
Binaries_Osx/     | Directory that will contains Max OS X executable
Binaries_Linux/   | Directory that will contains Linux executable

Linux Specific Notes :
======================

### Libusb 1.0:

In order to compile the library you have to install the version 1.0 of libusb. 
Take care to use version 1.0 and not version 0.1. To install libusb 1.0 on 
Ubuntu, run "sudo apt-get install libusb-1.0-0-dev".

### Access rights:

In order to bind to privileged ports, this software must be run as root
(sudo, or started as a service). This will also authorize access to USB
devices without the need to configure udev rules.
