# DCCMQTTGateway

## Setup

Create file `include/wificredentials.h`:
```cpp
#define WIFI_SSID "your wifi ssid"
#define WIFI_PASS "your wifi password"
```

Create file `include/config.h`:
```cpp
#define MQTT_SERVER "your mqtt server hostname or ip address"
#define DCC_PIN 23
```

## Usage

The Gateway adapts all DCC messages into MQTT PUBLISH messages.

DCC Message | MQTT topic | Description
-|-|-


## Advanced Usage

You can change the top-level MQTT topic
(default `dcc`)
by setting `#define DCC_TOPIC "dcc"` in `include/config.h`.

If you operate your MQTT broker on a port different that `1883`,
you can set `#define MQTT_PORT 1883` in `include/config.h`
to overwrite it to any port number.
