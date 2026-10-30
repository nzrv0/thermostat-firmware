# Vendor cloud contract

What the firmware sends to and expects from the vendor's cloud, as
recovered from the shipped image. Every item carries the address or the
string it was read from, so it can be re-checked with
`tools/analyze.py`.

## Identity

| Item | Value | Address |
|---|---|---|
| Board | `MC6-U-2-L` | PCB silkscreen |
| Product line | `FalconHP` | OTA path |
| Variant markers | `MC6_E`, `MC6_TIMER`, `MC6_2FCU`, `MC6_4FCU`, `MC6_HW` | `0x08092D6C`–`0x08092D98` |
| Firmware version format | `U-LW-%d.%02d` | `0x0803431C` |
| Build stamp | `20260812` | `0x0806565C` |
| JSON version | `5.0.4` | `0x0807D8EC` |
| Device name | `Thermostat-%d`, `Thermostat` | `0x080775FF` |

## MQTT

The firmware speaks **MQTT-SN** (`MQIsdp`, `0x08045680`) to the ESP-07S
module, which carries it over the air.

| Setting | String | Address |
|---|---|---|
| MQTT Server | `MQTT Server ` | `0x0801FCC7` |
| MQTT Server Port | `MQTT Server Port ` | `0x0801FCDF` |
| MQTT User Name | `MQTT User Name ` | `0x0801FCFC` |
| MQTT Password | `MQTT Password ` | `0x0801FD0C` |
| MQTT Publish topic | `MQTT Publish ` | `0x0801FD54` |
| MQTT Subscribe topic | `MQTT Subscribe ` | `0x0801FD77` |
| User Topic | `User Topic` | `0x08022597` |

JSON keys that select the topic behaviour: `"EnableUserTopic"`
(`0x0807B9D0`), `"PubTopic"` (`0x0807B9E7`), `"SubTopic"` (`0x0807B9F7`).

### Hosts and keys

| String | Address |
|---|---|
| `www.brande.com` | `0x0807E654` |
| `www.mycomforthouse.com` | `0x08051793` |
| `www.brande.commc6mqtt_aes256ecb7` | `0x0807E66F` |
| `public1256` | `0x0807E69C` |

`www.brande.com` is immediately followed by `mc6mqtt_aes256ecb7` — a
16-byte **AES-256 key** concatenated with the host name in the string
table. `public1256` sits next to it and is a second key candidate.

**Open:** whether the payload is encrypted as a whole or field-wise, and
which of the two strings is the live key, is not yet established (see
[`open-questions.md`](open-questions.md) §6).

## HTTP

The HTTP client posts form-encoded bodies:

```
Content-Type: application/x-www-form-urlencoded
Host: %s
Content-Length: %d
Connection: close
```

with the path family

```
/HEader/SerialNumber/RoomDevices/RoomTemperature1/225   (0x0807E6AB)
Header/SerialNumber/RoomDevices                        (0x0807E6E4)
```

so the device reports one value per request under a serial-number keyed
path.

## Telemetry document

The firmware builds one JSON document (`0x0807CDBF`–`0x0807D80B`) with
these keys:

```
msgid  mac  version  temp  humi
heatsettemp  coolsettemp  settemp  mode  onoff  frost
delay  diff  holdheattemp  holdcooltemp  holdtime
kb  kbkey  cf  holiday  holiday_startime  holiday_endtime
standby  fan  opt  timezone  prog
temp_prog  timer_prog  fan_run_time_enable  fan_run_time
```

wrapped in the envelope `{"p":"…","v":"…","m":…}` (`0x0807D8C8`–
`0x0807D8F4`). Schedules are nested:

```
,"temp_prog":{  "%s":[  {"t":%s,"s":%d}  ,…]
,"timer_prog":{  "%s":[  {"n":%s,"f":%s}  ,…]
```

`t` is a time, `s` a setpoint in tenths of a degree, `n` a name and `f`
a flag.

## Over-the-air update

| Item | Value | Address |
|---|---|---|
| Transport | FTP | — |
| Directory | `/ftp/Products/MC6-U-Lora/FalconHP_Mark/` | `0x0808AAC3` |
| File name format | `U-LW-%d.%02d` | `0x0803431C` |

## LoRaWAN uplink

The module is a Dragino-style LoRaWAN modem (see
[`subsystems.md`](subsystems.md)). The uplink payload layout is **not**
recovered — open question #13.

## What an integrator needs from the vendor

1. The broker address, port, user name, password and the publish and
   subscribe topics for the product's MQTT-SN gateway.
2. Whether the payload is AES-256 encrypted, with which of the two
   candidate keys, and in which mode.
3. The meaning of the JSON envelope fields `p`, `v` and `m`.
4. The exact path template of the HTTP reporting endpoint.
5. The OTA file naming for the FTP directory.
