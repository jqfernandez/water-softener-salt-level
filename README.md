# Water Softener Salt Level Monitor

An ESP32-based IoT device that monitors the salt level in a water softener tank using an ultrasonic sensor and integrates with Home Assistant via MQTT.

## Features

- **Wi-Fi Connectivity**: Connects to your home network
- **MQTT Integration**: Publishes sensor data to an MQTT broker
- **Home Assistant Auto-Discovery**: Automatically appears in Home Assistant with no manual configuration
- **Real-time Monitoring**: Tracks both distance (cm) and salt level percentage
- **Configurable**: Easy configuration via menuconfig for Wi-Fi, MQTT, and sensor settings
- **Mock Sensor Mode**: Built-in test mode for development without physical sensor

## Hardware Requirements

### Required Components
- ESP32 development board (ESP32-DevKitC or similar)
- HC-SR04 ultrasonic distance sensor
- USB cable for programming and power
- Jumper wires

### Wiring Diagram

```
HC-SR04        ESP32
--------       -----
VCC     ----->  5V
GND     ----->  GND
TRIG    ----->  GPIO 4 (configurable)
ECHO    ----->  GPIO 5 (configurable)
```

## Software Requirements

- [ESP-IDF v5.5.2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
- MQTT Broker (e.g., Mosquitto)
- Home Assistant with MQTT integration (optional but recommended)

## Installation

### 1. Clone and Navigate to Project

```bash
git clone <your-repo-url>
cd water-softener-salt-level
```

### 2. Set Up ESP-IDF Environment

```bash
# Source ESP-IDF (adjust path to your installation)
source ~/esp/v5.5.2/esp-idf/export.sh

# Or use your alias if configured
get_idf
```

### 3. Configure the Project

```bash
idf.py menuconfig
```

Navigate to **"Water Softener Salt Level Configuration"** and configure:

#### Wi-Fi Settings
- **WiFi SSID**: Your network name
- **WiFi Password**: Your network password
- **Maximum retry**: Connection retry attempts (default: 5)

#### MQTT Settings
- **MQTT Broker URL**: e.g., `mqtt://192.168.1.100:1883`
- **MQTT Username**: Your MQTT broker username
- **MQTT Password**: Your MQTT broker password
- **MQTT Client ID**: Unique identifier (default: `water_softener_salt_level`)

#### Sensor Settings
- **Tank height in centimeters**: Total height of your salt tank (default: 100cm)
- **HC-SR04 TRIG GPIO**: Trigger pin (default: GPIO 4)
- **HC-SR04 ECHO GPIO**: Echo pin (default: GPIO 5)
- **Reading interval in seconds**: How often to read sensor (default: 30s)

Save configuration (press `S`, then `Q` to exit).

### 4. Build and Flash

```bash
# Build the project
idf.py build

# Flash to ESP32 and open serial monitor
idf.py -p /dev/cu.SLAB_USBtoUART flash monitor

# Or let it auto-detect the port
idf.py flash monitor
```

Press `Ctrl-]` to exit the serial monitor.

## Home Assistant Integration

### MQTT Configuration

1. In Home Assistant, go to **Settings → Devices & Services**
2. Click **+ ADD INTEGRATION**
3. Search for and select **MQTT**
4. Enter your MQTT broker details:
   - **Broker**: Your MQTT server IP
   - **Port**: 1883 (default)
   - **Username**: Same as ESP32 configuration
   - **Password**: Same as ESP32 configuration

### Auto-Discovery

Once the ESP32 connects to MQTT, it will automatically create:
- **Device**: "Water Softener Salt Level"
- **Sensors**:
  - Salt Level Distance (cm)
  - Salt Level Percentage (%)

The sensors will appear in Home Assistant automatically within a few seconds.

### Creating Dashboards

Add the sensors to your dashboard:
1. Go to any dashboard and click **Edit**
2. Click **+ ADD CARD**
3. Choose a card type:
   - **Gauge Card**: Shows percentage as a visual gauge
   - **Entity Card**: Simple text display
   - **History Graph**: Shows trends over time

### Automation Example

Create an automation to notify when salt is low:

```yaml
automation:
  - alias: "Low Salt Alert"
    trigger:
      - platform: numeric_state
        entity_id: sensor.salt_level_percentage
        below: 20
    action:
      - service: notify.mobile_app
        data:
          title: "Water Softener Alert"
          message: "Salt level is below 20%. Time to refill!"
```

## Project Structure

```
water-softener-salt-level/
├── main/
│   ├── salt_level_monitor.c    # Main application code
│   ├── CMakeLists.txt           # Component build config
│   ├── Kconfig.projbuild        # Configuration menu
│   └── idf_component.yml        # Component dependencies
├── build/                       # Build output (auto-generated)
├── .vscode/                     # VSCode configuration
├── .devcontainer/               # Dev container config
├── CMakeLists.txt               # Project build config
└── README.md                    # This file
```

## Development

### Mock Sensor Mode

The project includes a mock sensor function that generates test data. This is useful for:
- Testing MQTT connectivity
- Setting up Home Assistant dashboards
- Verifying the system without physical hardware

The mock sensor simulates salt level gradually decreasing over time.

### Adding Real HC-SR04 Support

When your sensor arrives, the `read_distance_cm()` function in [salt_level_monitor.c](main/salt_level_monitor.c) will be replaced with actual HC-SR04 driver code. All other functionality remains the same.

### Reflashing During Development

```bash
# If monitor is running, press Ctrl-]
# Then rebuild and flash
idf.py build flash monitor
```

## Troubleshooting

### Wi-Fi Connection Issues
- Check SSID and password in menuconfig
- Ensure ESP32 is within range of your router
- Check serial monitor for connection errors

### MQTT Connection Issues
- Verify MQTT broker is running: `systemctl status mosquitto`
- Test credentials: `mosquitto_sub -h localhost -u USER -P PASS -t test`
- Check firewall settings on MQTT broker
- Verify broker URL includes protocol: `mqtt://` not just IP

### Home Assistant Not Discovering Device
- Ensure MQTT integration is installed and configured
- Check MQTT broker logs for incoming messages
- Restart ESP32 to resend discovery messages
- In HA, go to Developer Tools → MQTT and listen to `homeassistant/#`

### Sensor Reading Issues
- Verify wiring connections (especially TRIG and ECHO pins)
- Check that sensor has stable 5V power
- Ensure GPIO pins match configuration in menuconfig
- Test sensor with multimeter to verify it's working

## MQTT Topics

The device publishes to these topics:

### State Topic
```
homeassistant/sensor/water_softener_salt_level/state
```

Payload example:
```json
{
  "distance": 43.5,
  "percentage": 56.5
}
```

### Discovery Topics
```
homeassistant/sensor/water_softener_salt_level/distance/config
homeassistant/sensor/water_softener_salt_level/percentage/config
```

## Configuration Reference

All configuration is done via `idf.py menuconfig` and stored in `sdkconfig`. Key settings:

| Setting | Default | Description |
|---------|---------|-------------|
| `CONFIG_WIFI_SSID` | "myssid" | Wi-Fi network name |
| `CONFIG_WIFI_PASSWORD` | "mypassword" | Wi-Fi password |
| `CONFIG_MQTT_BROKER_URL` | "mqtt://192.168.1.100" | MQTT broker address |
| `CONFIG_MQTT_CLIENT_ID` | "water_softener_salt_level" | Unique device ID |
| `CONFIG_TANK_HEIGHT_CM` | 100 | Tank height for % calculation |
| `CONFIG_SENSOR_TRIG_GPIO` | 4 | HC-SR04 trigger pin |
| `CONFIG_SENSOR_ECHO_GPIO` | 5 | HC-SR04 echo pin |
| `CONFIG_READING_INTERVAL_SEC` | 30 | Seconds between readings |

## License

This project is provided as-is for personal use.

## Contributing

Contributions welcome! Feel free to open issues or submit pull requests.

## Acknowledgments

- Built with [ESP-IDF](https://github.com/espressif/esp-idf)
- Integrates with [Home Assistant](https://www.home-assistant.io/)
- Uses MQTT protocol via [esp-mqtt](https://github.com/espressif/esp-mqtt)
