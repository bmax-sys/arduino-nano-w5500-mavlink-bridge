# Testing

The Arduino Nano + W5500 MAVLink Bridge was tested with real hardware.

## Test Hardware

- Arduino Nano
- W5500 Ethernet module
- ArduPilot-compatible flight controller
- Ethernet network
- QGroundControl

## Test Configuration

Default bridge configuration:

| Parameter | Value |
|---|---|
| Device IP | `192.168.88.50` |
| Subnet Mask | `255.255.255.0` |
| Gateway | `192.168.88.1` |
| Target IP | `192.168.88.11` |
| UDP Port | `14550` |
| UART Baud Rate | `115200` |

## Data Path

The complete tested communication path:

`Flight Controller ↔ UART ↔ Arduino Nano ↔ SPI ↔ W5500 ↔ Ethernet/UDP ↔ QGroundControl`

## UART Test

The flight controller was connected directly to the Arduino Nano UART:

`FC TX → Nano RX0`

`FC RX → Nano TX0`

A common ground was used between the flight controller and Arduino Nano.

MAVLink data was successfully transferred through the UART connection.

## Ethernet Test

The Arduino Nano communicates with the W5500 Ethernet controller over SPI.

The W5500 successfully provides Ethernet connectivity for the bridge using the configured static IP address.

## MAVLink / UDP Test

MAVLink telemetry was forwarded from UART to Ethernet using UDP.

Default destination:

`192.168.88.11:14550`

QGroundControl successfully received MAVLink telemetry from the flight controller.

Communication was also verified in the opposite direction:

`QGroundControl → UDP → W5500 → Arduino Nano → UART → Flight Controller`

This confirms bidirectional UART ↔ UDP communication.

## Web Interface Test

The built-in configuration interface was accessed through:

`http://192.168.88.50`

The following parameters can be changed from the web interface:

- Device IP
- Target IP
- Subnet Mask
- Gateway
- UDP Port
- UART Baud Rate

The configuration is stored in EEPROM.

After pressing **SAVE & REBOOT**, the device saves the new settings and automatically restarts.

## Result

The prototype successfully provides a bidirectional MAVLink bridge between a flight controller UART and an Ethernet network.

Confirmed:

- Arduino Nano ↔ W5500 SPI communication
- Flight Controller ↔ Arduino Nano UART communication
- UART → UDP forwarding
- UDP → UART forwarding
- QGroundControl MAVLink telemetry
- Bidirectional MAVLink communication
- Built-in web configuration
- EEPROM configuration storage
- Automatic reboot after configuration changes

The project has been verified as a working hardware prototype.
