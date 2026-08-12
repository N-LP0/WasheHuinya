# ESP32-S3-Zero Overview

## Introduction

ESP32-S3-Zero (without pin header) and ESP32-S3-Zero-M (with pin header) are tiny in size with castellated holes, making them easy to integrate into other host boards.

ESP32-S3-Zero comes with an onboard Type-C USB connector, which exposes most of the unused pins in a small form factor. It is equipped with the ESP32-FH4R2 chip, integrated Wi-Fi and BLE 5.0, featuring 4 MB Flash and 2 MB PSRAM.

In addition, there are hardware encryption accelerator, RNG, HMAC and Digital Signature modules to meet the safety requirements of IoT and provide rich peripheral interfaces. Moreover, its multiple low-power working modes support most application scenarios such as IoT, mobile devices, wearable electronic devices and smart homes.

## Features

- Equipped with Xtensa® 32-bit LX7 dual-core processor, up to 240 MHz main frequency.
- Supports 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth® 5 (LE).
- Built-in 512 KB of SRAM and 384 KB ROM, onboard 4 MB Flash memory and 2 MB PSRAM.
- Castellated module and onboard ceramic antenna, allows soldering direct to carrier boards.
- Supports flexible clock, module power supply independent setting and other controls to realize low power consumption in different scenarios.
- Integrated with USB serial port full-speed controller, 24 × GPIO pins allow flexible configuring pin functions.
- 4 × SPI, 2 × I2C, 3 × UART, 2 × I2S, 2 × ADC, etc.

## Hardware Description

- When using ESP32-S3-Zero with daughterboards, avoid covering the ceramic antenna with PCB boards, metal or plastic components.
- In ESP32-S3-Zero, GPIO33 to GPIO37 pins are not exposed; these pins are used for Octal PSRAM.
- ESP32-S3-Zero uses GPIO21 to connect with WS2812 RGB LED.
- ESP32-S3-Zero does not employ a USB to UART chip. When flashing firmware, press and hold the BOOT button (GPIO0), and then connect the Type-C cable.
- The `TX` and `RX` markings on the board indicate the default UART0 pins for ESP32-S3-Zero. Specifically, TX is GPIO43, and RX is GPIO44.

## Hardware Connection

- Press the BOOT (GPIO0) key before connecting the Type-C cable each time you download the firmware.
- Input 3.7 V to 6 V for the castellated hole with 5 V silkscreen when connecting external power.
