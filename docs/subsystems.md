# Subsystems and protocols

Each entry records the evidence and where in the shipped image it was found.
String addresses are the authoritative anchor for follow-up work.

## Boot self-test

`Hardware Checking...` at `0x08016013`, followed by the ordered checks:

| # | String | Address |
|---|---|---|
| 1 | `1. Relay: T1_On` | `0x08016020` |
| 2 | `2. EEPROM AT24C256 ok.` / `… error.` | `0x08016048` / `0x08016060` |
| 3 | `3. Norflash SST39VF1601 ok.` / `… error.` | `0x0801607C` / `0x08016098` |
| 4 | `4. RTC S35390 ok.` / `… error.` | `0x080160C3` / `0x080160BB` |
| 5 | `5. Humidity SI7021 fail` | `0x080160EB` |
| 6 | `5. WIFI ESP-07S ok, v%s` / `… error.` | `0x0801610B` / `0x08016124` |
| 7 | `7. On Board NTC: %d.%dc` | `0x0801613F` |
| 8 | `8. In Door NTC: %d.%dc` | `0x0801615B` |
| 9 | `9. Out Door NTC: %d.%dc` | `0x08016177` |
| 10 | `10. Light Sensor: %d` | `0x08016190` |

## WiFi / BLE (ESP-07S, USART1)

Espressif AT command set. The firmware is the *host* driving the module.

**Provisioning / setup**
`AT+CWJAP?`, `AT+CWJAP_CUR="%s","%s"`, `AT+CIPSTA_CUR="%s","%s","%s"`,
`AT+CIFSR`, `AT+CWLAPOPT=1,7`, `AT+CWAUTOCONN=1`.

**BLE**
`AT+BLEINIT=0` / `AT+BLEINIT=2`, `AT+BLEADDR?`, `AT+BLENAME="MC6_%s"`,
`AT+BLEADVDATAEX="%s","181A","0102030405",1`,
`AT+BLEADVDATA="02010611094D43365F%s"`, `AT+BLEADVSTART`,
`AT+BLEADVSTOP`, `AT+BLEADVPARAM=50,50,0,1,4,0,1,"12:34:45:78:66:88"`,
`AT+BLESCAN=1,5`, `AT+BLEDISCONN=%d`, `AT+BLEGATTSSRVCRE`,
`AT+BLEGATTSSRVSTART`, `AT+BLEGATTSSRV?`, `AT+BLEGATTSCHAR?`,
`AT+BLEGATTSNTFY=%d,1,6,%d`.

**BLE provisioning (BLUFI)**
`AT+BLUFINAME="energex_thermostat"`, `AT+BLUFI=1`, `AT+BLUFISEND=%d`.
Responses handled: `+BLUFIDATA:`, `+BLECONNPARAM:`, `+BLEDISCONN:`,
`+BLEADDR:`, `+BLESCAN:`.

**TCP/UDP sockets**
`AT+CIPSTART="TCP","%s",%d`, `AT+CIPSTART="UDP","%s",%d`, `AT+CIPSTART="UDP","%s",%d,%d,2`,
`AT+CIPSEND=%d`, `AT+CIPSEND=%d,%d`, `AT+CIPSEND=%d,"%d.%d.%d.%d",%d`,
`AT+CIPRECVMODE=1`, `AT+CIPRECVDATA=2048`, `AT+CIPCLOSE`, `AT+CIPMUX=1`,
`AT+CIPDINFO=1`, `AT+CIPSTO=300`. Responses: `+IPD,2920`, `CONNECTED`,
`CLOSED`, `SEND OK`, `OK`.

**Module status / reset**
`AT+GMR`, `AT+GSN?`, `wifi module reseting` (`0x0806B147`),
`@wifi module start` (`0x0806B1EF`), `ap lost, restart...` (`0x0806B080`).

## LoRaWAN modem

Separate UART, Dragino-style AT interface:

`AT+DTRX=1,2,%d,`, `AT+CDEVEUI=%s`, `AT+CAPPEUI=%s`, `AT+CAPPKEY=%s`,
`AT+CCLASS=2`, `AT+CFREQBANDMASK=0002`, `AT+CDATARATE=%d`, `AT+CTXP=%d`,
`AT+CADR=%d`, `AT+CULDLMODE=2`, `AT+CCONFIRM=0`, `AT+CNBTRIALS=0,1`,
`AT+CWORKMODE=2`, `AT+CJOIN=1,0,10,8`, `AT+IREBOOT=0`, `AT+CSTATUS?`,
`AT+CSAVE`, `AT+CGMR?`, `AT+CGSN?`.

Responses: `+CME ERROR:`, `+CGSN=`, `+CJOIN:`, `+CJOIN:OK`, `OK+RECV:`,
`OK+RECV`, `ERR+`, `FAIL`, `%04x`, `%02x`.

Configuration menus present in the firmware: `SF7`–`SF12` all at `BW125`,
`Enable ADR`/`Disable ADR`, `3dBm`–`17dBm`, `LoraWan`, `Tuya`, `User Server`.

## Cloud (MQTT) and HTTP

**MQTT** — the firmware speaks MQTT-SN (`MQIsdp`, `0x08045680`) to the
ESP-07S. Topic and payload configuration is exposed as menu items:

| Setting | String | Address |
|---|---|---|
| MQTT Server | `MQTT Server ` | `0x0801FCC7` |
| MQTT Server Port | `MQTT Server Port ` | `0x0801FCDF` |
| MQTT User Name | `MQTT User Name ` | `0x0801FCFC` |
| MQTT Password | `MQTT Password ` | `0x0801FD0C` |
| MQTT Publish | `MQTT Publish ` | `0x0801FD54` |
| MQTT Subscribe | `MQTT Subscribe ` | `0x0801FD77` |
| User Topic | `User Topic` | `0x08022597` |

JSON keys: `"EnableUserTopic"` (`0x0807B9D0`), `"PubTopic"` (`0x0807B9E7`),
`"SubTopic"` (`0x0807B9F7`).

**Endpoints and keys**

| String | Address |
|---|---|
| `www.brande.com` | `0x0807E654` |
| `www.mycomforthouse.com` | `0x08051793` |
| `www.brande.commc6mqtt_aes256ecb7` | `0x0807E66F` |
| `public1256` | `0x0807E69C` |

`www.brande.com` is immediately followed by `mc6mqtt_aes256ecb7` — a 16-byte
**AES-256 key** (or key material) used for the MQTT payloads, concatenated
with the host name in the string table.

**HTTP** — `Content-Type: application/x-www-form-urlencoded`, `Host: %s`,
`Content-Length: %d`, `Connection: close`, and the path
`/HEader/SerialNumber/RoomDevices/RoomTemperature1/225`
(`0x0807E6AB`) with the companion `Header/SerialNumber/RoomDevices`
(`0x0807E6E4`).

**JSON telemetry / command schema.** The firmware builds a JSON document
with these keys (`0x0807CDBF`–`0x0807D80B`):

```
"msgid"  "mac"  "version"  "temp"  "humi"
"heatsettemp"  "coolsettemp"  "settemp"  "mode"  "onoff"  "frost"
"delay"  "diff"  "holdheattemp"  "holdcooltemp"  "holdtime"
"kb"  "kbkey"  "cf"  "holiday"  "holiday_startime"  "holiday_endtime"
"standby"  "fan"  "opt"  "timezone"  "prog"
"temp_prog"  "timer_prog"  "fan_run_time_enable"  "fan_run_time"
```

with the envelope `{"p":"…","v":"…","m":…}` (`0x0807D8C8`–`0x0807D8F4`).

Schedules are nested:

```
,"temp_prog":{  "%s":[  {"t":%s,"s":%d}  ,…]
,"timer_prog":{  "%s":[  {"n":%s,"f":%s}  ,…]
```

**Device identity.** `Thermostat-%d` (`0x080775FF`), `Thermostat`
(`0x08077618`), `Room` (`0x08077610`), `MC6` (`0x08092D20`).

## Building automation

**Modbus TCP server** — `Modbus Tcp Server Start...` (`0x0806AC23`), with
`WM_AT_TCP_SEND_TEXT, Modbus Tcp Server Start...` (`0x0806B0BC`) and
`WM_AT_TCP_CLOSE, Modbus Tcp Server Start...` (`0x0806B0F0`). Menu entry
`WIFI Modbus TCP` (`0x080925F8`).

**Modbus RTU 485** — menu entry `Modbus RTU 485` (`0x0809274C`).

**BACnet** — `bacnetip servo start...` (`0x0806B098`), menu entries
`Bacnet IP` (`0x0809275C`) and `Bacnet MS/TP` (`0x08092768`).

The **point registry** is a table of four-word descriptors at
`0x08090528`–`0x08090990`:

```
{ name_ptr, label_ptr, value_or_variable, handler_ptr }
```

26 descriptors were recovered this way (see [hw-map.md](hw-map.md) for the
relay subset). The `value_or_variable` word is either a literal (relay
statuses, all-zero) or a **RAM address** — e.g. `SystemMode_Commend` →
`0x200020E0`, `TempFormat` → `0x20002188`. These RAM addresses locate the
live state variables, which is how the protocol layers reach the HVAC core.

## Over-the-air update

- FTP path `/ftp/Products/MC6-U-Lora/FalconHP_Mark/` (`0x0808AAC3`)
- Upload path `updData/%s` (`0x08011200`)
- Version format `U-LW-%d.%02d` (`0x0803431C`)
- `A new version %d.%02d is available!` (`0x0803434F`), `Upgrade`
- Progress UI: `Upgrading... please wait` (`0x0801608F` area), `0%`, `100%`
- Build stamp `20260812` (`0x0806565C`)
- Identifier `mc6_u_v` (`0x0808AAEE`)

## Human interface

Roughly 1,300 bitmap assets (see [assets.md](assets.md)), a digit-font set
(`digitbg.bmp`, `setnum0..9`, `mtnum0..9`, `mtlnum0..9`, `shnum0..9`,
`stnum0..9`, `tmnum0..9`, `wi0..4`), and an interaction model with a
**touchpad lock** (`Flag TouchPad Lock` / `FlagiSTouchPadLock`,
`0x08092CD0`/`0x08092CE4`).

Menu/settings strings recovered include sensor selection
(`Internal Sensor`, `Internal + Out Door Sensor`, `Out Door Sensor`),
error states (`Temperature Sensor Error`, `Internal Sensor Error`,
`Out Door Sensor Error`), occupancy handling (`Occupancy Input: Disable/N-O/N-C`),
and lock/unlock.

## Not yet traced

- The **exact** Modbus register map and BACnet object types — the point
  registry gives names and handlers but not register addresses or object
  identifiers.
- Whether MQTT payloads are AES-encrypted wholesale or field-wise, and the
  exact key derivation.
- The full JSON command grammar (which keys are settable vs read-only).
- The LoRaWAN uplink/downlink payload format.
