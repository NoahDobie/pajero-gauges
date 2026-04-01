# Pajero Gauges

A custom PCB designed for a **1993 Mitsubishi Pajero with 4D56T Diesel** using an **ESP32 30-pin development board** to read vehicle sensor data and display it across multiple OLED screens.

This board is intended for a **12 V automotive electrical system** and supports:

- 2x GM 3 Bar MAP sensors
- 2x battery voltage sense inputs
- 1x wideband analog input from a **14point7 Spartan 3 Lite v2**
- 1x MAX31855 thermocouple interface
- 1x TCA9548A I2C multiplexer
- 4x primary OLED outputs, plus 1 spare OLED output for future expansion

The PCB uses a removable **30-pin ESP32 dev board** mounted in female headers, allowing the ESP32 to be programmed separately over USB inside the house rather than in the car.

---

## Project Purpose

This project is designed as a **custom gauge and monitoring system for a 1993 Pajero**.

Its goal is to provide a clean, compact way to monitor important engine and electrical system data using small OLED displays, while keeping the design simple, serviceable, and easy to expand in the future.

---

## What It Monitors

This board is intended to read and display:

- **Battery 1 voltage**
- **Battery 2 voltage**
- **MAP sensor 1**
- **MAP sensor 2**
- **Wideband AFR / lambda analog signal**
- **Thermocouple temperature input**

The display system is designed around **four 1.3 inch OLED screens**, with one extra display output included for future use.

---

## System Overview

The board takes in switched **12 V ignition power** from the vehicle and passes it through an input protection stage before stepping it down to **5 V** for the ESP32 dev board and related components.

### Power Flow

`IGN 12V -> input fuse -> reverse polarity protection -> TVS protection + bulk capacitance -> protected 12V rail -> 5V buck converter -> ESP32 dev board`

The ESP32 dev board then provides regulated **3.3 V** for lower-power logic and peripherals such as:

- TCA9548A I2C multiplexer
- MAX31855
- OLED displays

---

## Board Inputs

### 1. Main Power Input

**Connector:** 2-pin JST VH  
**Purpose:** switched ignition 12 V and chassis ground from the Pajero

**Pins:**
- IGN 12V
- Chassis GND

### 2. Battery Voltage Sense

**Connector:** 2-pin JST VH  
**Purpose:** monitor Battery 1 and Battery 2 voltages

**Pins:**
- Battery 1 voltage
- Battery 2 voltage

### 3. MAP Sensor Inputs

**Connectors:** 2x 3-pin JST VH  
**Purpose:** inputs for two GM 3 Bar MAP sensors

**Pins per connector:**
- GND
- Signal Out
- 5V

**Sensors:**
- MAP1
- MAP2

### 4. Wideband Input

**Connector:** 2-pin JST VH  
**Purpose:** analog signal input from a **14point7 Spartan 3 Lite v2**

**Pins:**
- Circuit GND
- Wideband Signal Out

### 5. Thermocouple Input

The board includes a **MAX31855 thermocouple interface** for temperature measurement.

---

## Board Outputs

### OLED Display Outputs

**Connectors:** 5x 4-pin JST XH  
**Purpose:** I2C OLED displays through the TCA9548A multiplexer

**Pins per connector:**
- SDA
- SCL
- GND
- 3.3V

**Outputs:**
- OLED 1
- OLED 2
- OLED 3
- OLED 4
- OLED 5 (spare / future use)

The first 4 outputs are intended for the main display setup in the Pajero, and the 5th is included for future expansion.

---

## ESP32 Integration

The PCB uses a removable **30-pin ESP32 dev board** rather than mounting the ESP32 directly to the PCB.

It connects through:

- 2x 15-pin female headers
- 1x removable ESP32 dev board

This makes the board easier to:

- program
- replace
- debug
- update in the future

### Why This Approach Was Used

- No onboard USB-UART circuit required
- No custom boot/reset circuitry required
- ESP32 can be removed and programmed separately
- Simpler and lower-risk first PCB revision

---

## Sensor Input Scaling

The following analog inputs are scaled down to a safe range for the ESP32 ADC:

- Wideband analog output
- MAP1 output
- MAP2 output
- Battery 1 voltage
- Battery 2 voltage

### 0-5 V Sensor Inputs

Used for:

- MAP1
- MAP2
- Wideband analog output

**Divider:**
- Top resistor: 10k
- Bottom resistor: 20k

This scales **5.0 V** down to about **3.33 V** for the ESP32 ADC.

### Battery Voltage Sense

Used for:

- Battery 1
- Battery 2

**Divider:**
- Top resistor: 27k
- Bottom resistor: 5.1k

This provides headroom for normal automotive charging voltage and higher transient conditions.

---

## Main Components

### Power and Protection

- **NTF2955T1G** — reverse polarity PMOS
- **SMBJ18A-Q** — TVS diode for input protection
- **AP64352QSP-13** — 5 V buck converter
- **100 uF bulk capacitor** on protected 12 V rail
- **100 nF bypass capacitor** on protected 12 V rail
- **2x 10 uF buck input capacitors**
- **2x 22 uF buck output capacitors**

### Signal and Logic

- **MAX31855TASA+T** — thermocouple interface
- **TCA9548ARGERQ1** — I2C multiplexer

### Indicators

- 3x red 0603 LEDs for:
  - protected 12 V rail
  - 5 V rail
  - 3.3 V rail

### Connectors

- JST VH for vehicle power and sensor inputs
- JST XH for OLED outputs
- Female 2.54 mm headers for ESP32 dev board

---

## Typical Use in the Car

This board is intended for a custom in-vehicle display setup where the driver can monitor important readings such as:

- boost / manifold pressure
- AFR or lambda
- system voltage
- temperature data

It is designed to suit a **1993 Mitsubishi Pajero with 4D56T diesel**, but can definitely work for other turbo vehicles with the right changes to the ESP-32 code.

---

## Assembly Notes

- Most components are top-side SMT
- Several connectors are through-hole
- The ESP32 dev board is socketed through female headers
- Designed with serviceability and prototyping in mind

---

## Firmware Notes

Because the board uses a removable ESP32 dev board:

- firmware is uploaded directly to the ESP32 over USB
- no onboard USB programming hardware is needed
- the module can be removed for development or replacement

---

## Future Expansion

Included for flexibility:

- 1 spare OLED connector
- 1 spare expansion header for future sensors or IO
- removable ESP32 module
- room for additional firmware features and display layouts

Possible future additions:

- coolant temperature input
- extra analog or GPIO-based sensors
- additional display pages
- wireless updates through the ESP32 dev board