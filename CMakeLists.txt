cmake_minimum_required(VERSION 3.12)
project(untitled)

set(CMAKE_CXX_STANDARD 14)

add_executable(untitled
        Amldbglog/Amldbglog.c AmlLibusb/AmlLibusb.cpp AmlUsbScan/AmlUsbScan.cpp AmlUsbScanX3/AmlUsbScanX3.c  UsbRomDrv/UsbRomDrv.cpp AmlTime.c  update.cpp)
