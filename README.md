# Work in Progress...

![Logo](https://github.com/dvdh1961/ADAMP/blob/main/scrcpp/ADAMP.png)

---

![Current progress](https://img.shields.io/badge/Version-V1.0.08.26-yellow)
![Stars](https://img.shields.io/github/stars/dvdh1961/ADAMP_HWD)
![Issues](https://img.shields.io/github/issues/dvdh1961/ADAMP_HWD)

---

Connect all your legacy Coleco Adam hardware devices—such as floppy drives, keyboards, joysticks, FujiNet
to the Adam+ emulator using this hardware device with programmable USB MCUs.

---

![AdamP_HWD](https://github.com/dvdh1961/ADAMP_HWD/blob/main/Images/HWD.jpg)

---

# Let's Go

[![AdamP_HWD](https://github.com/dvdh1961/ADAMP_HWD/blob/main/Images/Fujinet_AdamP.jpg)](https://youtu.be/oGeiW5sdebk)

---

![AdamP_HWD](https://github.com/dvdh1961/ADAMP_HWD/blob/main/Images/ADAM_Plus_Hardware_Block_Diagram.png)

# System Description

- The ADAM+ Hardware Interface contains two independently programmable CH559 USB microcontrollers. 
  An onboard FE2.1 USB hub connects both controllers to the host computer through a single USB connection.

- CH559 MCU 1 – Keyboard and Joystick Gateway

  The first CH559 acts as a gateway for the original Coleco ADAM keyboard and two Coleco-compatible joysticks.
  The keyboard is connected through an RJ11 connector. Its serial signal passes through a 74HC14 Schmitt-trigger circuit,
  which cleans and reshapes the electrical signal before it reaches the CH559 UART interface.

  The CH559 translates the original ADAM keyboard protocol into a standard USB HID keyboard.
  It also reads two hardware joysticks through their DB9 connectors and presents them to the host as USB HID game controllers.

- CH559 MCU 2 – ADAMNet Master

  The second CH559 operates as an ADAMNet master controller.
  It communicates with original ADAMNet peripherals through its UART interface and a 74HC14 Schmitt-trigger signal-conditioning circuit.

  Supported peripherals can include:

      - Coleco ADAM floppy disk drives
      - ADAM printers
      - FujiNet
      - ADE or other drive emulators
      - Other ADAMNet-compatible hardware

- The controller translates communication between the USB host and the original ADAMNet bus, allowing physical ADAM peripherals to be used with the ADAM+ emulator.

---

![AdamP_HWD](https://github.com/dvdh1961/ADAMP_HWD/blob/main/Images/ADAMP_HWD_PCB.png)

---

## Bill of Materials (BOM)

| ID | Component                        | Designators            | Footprint                                | Qty | Manufacturer Part Number | Manufacturer     | Supplier | Supplier Part |
|---:|:---------------------------------|:-----------------------|:-----------------------------------------|----:|:-------------------------|:-----------------|:---------|:--------------|
|  1 | 2-pin header                     | H1, H2, H3             | `HDR-TH_2P-P2.54-V-M`                    |   3 | `PZ254V-11-02P`          | XFCN             | LCSC     | `C492401`     |
|  2 | FE2.1 USB Hub Controller         | U1                     | `LQFP-48_L7.0-W7.0-P0.50-LS9.0-BL`       |   1 | `FE2.1-CQFP48A`          | Terminus         | LCSC     | `C39693`      |
|  3 | CH559L Microcontroller           | U2, U3                 | `LQFP-48_L7.0-W7.0-P0.50-LS9.0-BL`       |   2 | `CH559L`                 | Jiangsu Qin Heng | LCSC     | `C150548`     |
|  4 | 74HC14 Schmitt-Trigger Inverter  | U4                     | `SOIC-14_L8.7-W3.9-P1.27-LS6.0-BL`       |   1 | `74HC14D,653`            | Nexperia         | LCSC     | `C5605`       |
|  5 | 2×6-pin IDC header, 2.54 mm      | P1                     | `IDC-TH_12P-P2.54_BOOMELE-2X6P-2.54MM`   |   1 | `2.54-2×6P`              | BOOMELE          | LCSC     | `C9136`       |
|  6 | 2-position DIP switch            | SW1, SW2               | `SW-SMD_DSHP02TSGER`                     |   2 | `DSHP02TSGER`            | Kangshen         | LCSC     | `C3293142`    |
|  7 | Tactile push button              | SW3                    | `KEY-SMD_4P-L6.1-W6.1-P4.50-LS9.0`       |   1 | `TSA063G60-250`          | BRIGHT           | LCSC     | `C294566`     |
|  8 | RJ11 6P6C connector              | EXT, KB                | `RJ11-TH_PCB-6P6C-90`                    |   2 | `DS1133-S60APS`          | CONNFLY          | LCSC     | `C77857`      |
|  9 | Yellow 0805 LED                  | DIAG_EXT, DIAG_JK      | `LED0805-FD`                             |   2 | `YLED0805RA`             | YONGYUTAI        | LCSC     | `C52989003`   |
| 10 | USB Type-B connector             | USB3                   | `USB-B-TH_USB-B01`                       |   1 | `USB-B01`                | SOFNG            | LCSC     | `C498173`     |
| 11 | 100 nF capacitor                 | C4, C6, C8             | `C0805`                                  |   3 | `CC0805ZRY5V8BB104`      | Yageo            | LCSC     | `C519930`     |
| 12 | 10 nF capacitor                  | C5, C7, C9             | `C0805`                                  |   3 | `AC0805KRX7R0BB103`      | Yageo            | LCSC     | `C723703`     |
| 13 | 2.7 kΩ resistor                  | R1                     | `R0603`                                  |   1 | `QR0603J2K70P05Z`        | Ever Ohms        | LCSC     | `C176164`     |
| 14 | 330 Ω resistor                   | R2, R3                 | `R0603`                                  |   2 | `RMC06033305%N`          | Tyohm            | LCSC     | `C269524`     |
| 15 | 1 kΩ resistor                    | R4, R5                 | `R0603`                                  |   2 | `PTFR0603Q1K00N9`        | RESI             | LCSC     | `C23067434`   |
| 16 | 10 µF capacitor                  | C1                     | `C0805`                                  |   1 | `CL21A106KAYNNNE`        | Samsung          | LCSC     | `C15850`      |
| 17 | 10 µF capacitor                  | C2, C3                 | `C0805`                                  |   2 | `0805F106M250NT`         | FH               | EasyEDA  | `E48181`      |
| 18 | 12 MHz crystal                   | X1                     | `HC-49S_L11.4-W4.5-LS12.5`               |   1 | `XJHCELNANF-12MHZ`       | Taitien          | LCSC     | `C295091`     |
| 19 | DE-9 male connector              | DSUB1-JOY1, DSUB1-JOY2 | `DSUB-TH_DMR-9P`                         |   2 | `D-DMR009PM-D002`        | CKMTW            | LCSC     | `C141880`     |
| 20 | USB Type-A connector, right angle| USB1, USB2             | `USB-A-TH_USB-M-45_C42670`               |   2 | `906-351A1011D10200`     | Jintuo           | LCSC     | `C42670`      |

**Total:** 20 unique component types and 33 components.

---
