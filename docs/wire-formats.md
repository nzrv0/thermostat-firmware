# Wire formats

The byte-level formats the firmware speaks, so that an integrator can
interoperate with the device. Every entry records where it was recovered
from; the addresses are in the shipped application partition.

---

## Modbus

Both transports are served by one core (`firmware/src/net/modbus.c`).

### Point map

The firmware publishes its points in a table at `0x08090528`–`0x08090990`
as four word descriptors `{name, label, value, handler}`. The names and
labels are recovered; the register *addresses* are not (open question #5),
so the rebuild uses its own layout with the same names.

| Point | Label | Kind | Address | Scale |
|---|---|---|---|---|
| `Actual_Room_temperature` | Room temperature | input | 0 | 10 |
| `Humidity` | Room relative humidity | input | 1 | 1 |
| `W1relaystatus` | Heating relay W1 status | input | 2 | 1 |
| `W2relaystatus` | Heating relay W2 status | input | 3 | 1 |
| `Y1relaystatus` | Cooling relay Y1 status | input | 4 | 1 |
| `Y2relaystatus` | Cooling relay Y2 status | input | 5 | 1 |
| `Fanrelaystatus` | Fan relay G status | input | 6 | 1 |
| `Mode` | Mode | input | 7 | 1 |
| `HeatingStatus` | Heating Status | input | 8 | 1 |
| `FlagiSTouchPadLock` | Flag TouchPad Lock | input | 9 | 1 |
| `FlagiSHolidaying` | Flag Holidaying | input | 10 | 1 |
| `FlagiSHolding` | Flag Holding | input | 11 | 1 |
| `HeatSetTemp` | Heat Set Temp | holding | 0 | 10 |
| `CoolSetTemp` | Cool Set Temp | holding | 1 | 10 |
| `HoldEndtTime` | Hold End tTime | holding | 2 | 1 |
| `HoldSetTemp` | Hold Set Temp | holding | 3 | 10 |
| `HolidayStarttTime` | Holiday Start tTime | holding | 4 | 1 |
| `HolidayEndtTime` | Holiday End tTime | holding | 5 | 1 |
| `MaxSetTempLimit` | Max Set Temp Limit | holding | 6 | 10 |
| `MinSetTempLimit` | Min Set Temp Limit | holding | 7 | 10 |

The scale column is the rebuild's: a value of 210 in `HeatSetTemp` is
21.0 °C, matching the firmware's tenths of a degree.

### Function codes

| Code | Meaning |
|---|---|
| `0x03` | read holding registers |
| `0x04` | read input registers |
| `0x06` | write single register |
| `0x10` | write multiple registers |

### Framing

**TCP** — the MBAP header (transaction id, protocol id `0`, length,
unit id) followed by the PDU. The length field counts the unit id, the
function code and the data.

**RTU** — address byte, PDU, then the CRC-16 (poly `0xA001`, the
Modbus variant) transmitted low byte first.

---

## BACnet

The two BACnet servers share the point registry, so the object table is
the registry. The rebuild's encoding follows the standard:

### The BACnet/IP frame

```
0     BVLC type      0x81 (original unicast NPDU)
1     BVLC function  0x00 (result)
2-3   BVLC length
4     NPDU version   0x01
5     NPDU control   0x00 (expecting a reply)
6     APDU
```

The APDU of a ReadProperty answer is

```
0x30          complex ACK
0x0c          the service choice: ReadProperty
<tag> <id>    the object identifier (application tag 2)
<tag> <prop>  the property identifier
<value>       the property value
```

An application-tagged value is encoded with the tag number in the high
nibble of the tag octet and the **length** of the value in the low
nibble — `0x21` is an unsigned value of one byte, `0x22` two bytes — and
the value itself follows, big endian.

### The MS/TP frame

```
0     preamble       0x55
1     length         the destination address through the end of the data
2     destination    the device's master address
3     source
4     NPDU
...   APDU
```

The frame's CRC is the standard MS/TP CRC: the bytes are added
together, the result is complemented, and the two CRC bytes follow the
data, low byte first.

The **object identifiers** of the rebuild's table are the rebuild's own
(the original's are not recoverable — see
[open-questions.md](open-questions.md) §5); the names, the labels and
the published values come from the original's own registry.

## MQTT-SN

The firmware speaks MQTT-SN to the ESP-07S (`MQIsdp`, `0x08045680`).

```
offset 0   message type
offset 1   length (the bytes that follow)
offset 2   flags
offset 3   topic id
offset 4   message id (big endian)
offset 6   topic name (short topics) or payload
```

CONNECT carries the protocol id and the keep-alive duration before the
client id; PUBLISH carries the topic name and the payload; SUBSCRIBE
carries the topic name.

---

## HTTP

```
POST /HEader/SerialNumber/RoomDevices/RoomTemperature1/225 HTTP/1.1
Host: www.brande.com
Content-Type: application/x-www-form-urlencoded
Content-Length: <n>
Connection: close

<form body>
```

The path family is `/HEader/SerialNumber/RoomDevices/...` (the leading
`HEader` is spelled exactly like that in the firmware, `0x0807E6AB`).

---

## JSON telemetry

One document per report, built at `0x0807CDBF`–`0x0807D80B`:

```
{"p":"…","v":"…","m":…,
 "msgid":…,"mac":"…","version":"…",
 "temp":…,"humi":…,
 "heatsettemp":…,"coolsettemp":…,"settemp":…,
 "mode":…,"onoff":…,"frost":…,"delay":…,"diff":…,
 "holdheattemp":…,"holdcooltemp":…,"holdtime":…,
 "kb":…,"kbkey":…,"cf":…,
 "holiday":…,"holiday_startime":…,"holiday_endtime":…,
 "standby":…,"fan":…,"opt":…,"timezone":…,
 "prog":…,
 "temp_prog":{"<day>":[{"t":%s,"s":%d},…]},
 "timer_prog":{"<day>":[{"n":%s,"f":%s},…]},
 "fan_run_time_enable":…,"fan_run_time":…}
```

Temperatures are in tenths of a degree, humidity in tenths of a percent.

---

## AT command sets

### ESP-07S (Espressif)

```
AT+CWJAP_CUR="<ssid>","<key>"     join
AT+CWMODE_CUR=0 | 1                station | soft AP
AT+CIFSR                          read the address (+CIFSR:<ip>)
AT+CWLAP                           scan (+CWLAP:<ssid>,<rssi>)
AT+GMR                            firmware version (+GMR:<version>)
AT+BLEINIT=0 | 2                  off | on
AT+BLEADDR?  AT+BLENAME=  AT+BLEADVDATAEX=  AT+BLEADVPARAM=
AT+BLEGATTSSRVCRE  AT+BLEGATTSSRVSTART  AT+BLEGATTSCHAR?
AT+BLEGATTSNTFY=<n>,1,6,<len>
AT+BLEADVSTART  AT+BLEADVSTOP  AT+BLEDISCONN=<n>
AT+BLUFINAME="energex_thermostat"  AT+BLUFI=1  AT+BLUFISEND=<n>
AT+CIPSTART="TCP","<host>",<port>[,600]
AT+CIPSEND=<link>,<len>            AT+CIPRECVMODE=1
AT+CIPRECVDATA=2048  AT+CIPCLOSE
AT+CWSTARTSMART=1  AT+CWSTOPSMART
AT+CIPSTAMAC_CUR?  AT+CWLAPOPT=1,7  AT+RFPOWER=80
```

### LoRaWAN modem (Dragino style)

```
AT+CGMR?  AT+CGSN?
AT+CDEVEUI=<eui>  AT+CAPPEUI=<eui>  AT+CAPPKEY=<key>
AT+CCLASS=2  AT+CADR=1  AT+CJOIN=1,0,10,8
AT+CFREQ=<sf>,<bw>  AT+CTXP=<dbm>
AT+CDATARATE=<n>  AT+CFREQBANDMASK=0002
AT+CULDLMODE=2  AT+CCONFIRM=0  AT+CNBTRIALS=0,1
AT+CWORKMODE=2  AT+IREBOOT=0
```

---

## LoRaWAN uplink

The uplink is sent with the Dragino command `AT+CTXP=%d` (`0x08043E58`),
where the parameter is the payload length and the payload follows the
command as a hexadecimal string. The modem's answers are parsed with
`%02x` and `%04x`, and the downlink arrives as `OK+RECV` followed by
` OK+RECV:`.

The **field layout** of the original's payload is not recoverable — the
payload is assembled at run time and no static table describes it. The
rebuild defines its own sixteen-byte layout, which is a rebuild choice
and not a recovered constant:

```
offset  size  field
0       1     version
1       1     the operating mode
2       1     the published flags
3       1     the relay states (W1, W2, Y1, Y2, fan)
4       2     the room temperature, tenths of a degree, little endian
6       2     the humidity, tenths of a percent
8       2     the heat setpoint, tenths of a degree
10      2     the cool setpoint, tenths of a degree
12      4     the uptime in seconds, little endian
```

Sixteen bytes fits every data rate the firmware offers (the smallest is
SF12, 51 bytes).

## Firmware update

```
FTP: /ftp/Products/MC6-U-Lora/FalconHP_Mark/
     file name U-LW-%d.%02d
```
