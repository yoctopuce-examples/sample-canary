# Canary Commander example

This example of Canary Commander is based on Yocto-Visualization, a Yoctopuce application to visualize data from any [Yoctopuce](https://www.yoctopuce.com) sensor. This makes it possible to easily graph data sent by the canary soldiers (disk I/O access). It can run on Windows as well as Linux.

Compared to the original version, we have simply added a callback for incoming serial data from the [Yocto-RS485-V2](https://www.yoctopuce.com/FR/products/interfaces-electriques-usb/yocto-rs485-v2). This is implemented in method `serialCallback` of class `sensorsManager`. The callback code detects alarm messages and reacts accordingly.

**Note that this Canary commander sample code is not fit for use in production, it is just a quick-and-dirty example.**

### On Linux

Make sure that Mono is installed (min version 4) as well as Mono-Develop (min version 5) and open the  *.csprog* project with Mono-Develop. Avoid the flatpak based Mono-Develop version as it is sand-boxed and can't access to the libusb. More info on [this page](https://www.yoctopuce.com/EN/article/yocto-visualization-v2-on-linux)
