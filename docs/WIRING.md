# Wiring

This project uses an Arduino Nano and a W5500 Ethernet module as a bidirectional UART-to-Ethernet bridge for MAVLink telemetry.

## W5500 to Arduino Nano

| W5500 | Arduino Nano |
|---|---|
| G | GND |
| MO | D11 |
| SCK | D13 |
| CS | D10 |
| V | 3.3V |
| RST | D9 |
| MI | D12 |

SPI mapping:

- `D11` — MOSI
- `D12` — MISO
- `D13` — SCK
- `D10` — CS
- `D9` — W5500 Reset

## Arduino Nano to Flight Controller

| Arduino Nano | Flight Controller |
|---|---|
| RX0 | TX |
| TX0 | RX |
| 5V | 5V |
| GND | GND |

UART must be crossed:

`FC TX → Nano RX0`

`FC RX → Nano TX0`

## Data path

`Flight Controller → UART → Arduino Nano → W5500 → Ethernet → UDP → QGroundControl`

The bridge is bidirectional, so MAVLink data can travel in both directions between the flight controller and the network.

## Default network settings

- Device IP: `192.168.88.50`
- Subnet mask: `255.255.255.0`
- Gateway: `192.168.88.1`
- Target IP: `192.168.88.11`
- UDP port: `14550`
- UART baud rate: `115200`

These parameters can be changed through the built-in web interface.

## Important

The W5500 module in this tested setup is powered from the Arduino Nano `3.3V` pin.

Always verify the voltage requirements of your specific W5500 module before powering it.
