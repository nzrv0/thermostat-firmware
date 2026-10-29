# HVAC control — recovered specification

This is the behavioural specification the rebuild implements. It is derived
from the shipped firmware's strings, its published point registry, and
decompiled control flow. **Implemented today** in
`firmware/src/app/hvac.c`, driven by `firmware/tests/test_main.c`.

## Modes

From the firmware's own enumeration (`0x08092E00` area, and the mode
menus):

| Value | Firmware string |
|---|---|
| `HVAC_MODE_OFF` | `Off` |
| `HVAC_MODE_HEAT` | `Heat` |
| `HVAC_MODE_COOL` | `Cool` |
| `HVAC_MODE_HEAT_COOL` | `Heat & Cool` |
| `HVAC_MODE_AUTO` | `Auto` |
| `HVAC_MODE_FAN` | `Fan` |
| `HVAC_MODE_VENTILATE` | `Ventilate` |
| `HVAC_MODE_EMERGENCY_HEAT` | `Emergency Heat` |
| `HVAC_MODE_DEHUMIDIFY` | `Dehumidify` |
| `HVAC_MODE_HUMIDIFY` | `Humidify` |
| `HVAC_MODE_DRY` | `Dry` |
| `HVAC_MODE_ECONOMIZER` | `Economizer` |

Published mode commands: `SystemMode_Commend` /
`Unit_SystemMode_Commend` (`0x08090940`), `System_Fan_Mode_Commend` /
`Unit_Fan_Mode_Commend` (`0x08090950`),
`System_FanSpeed_RunStatus` / `Unit Fan Speed Status` (`0x0809085C`).

Fan speeds: `Off`, `Low`, `Med`, `High`, `Auto`.

## Equipment types

The firmware's *Equipment* menu enumerates the hardware configurations the
controller can drive (`0x08092240`–`0x08092574`):

```
Standard Heat/Cool      Heat Pump            Cool & Heat
Cool Only               Heat Only            Dual Fuel
O(Cool Active Reversing Valve)                B(Heat Active Reversing Valve)
Heat & Cool             Aux Heat & Heatpump  Heatpump Only
1 Cool Stage            2 Cool Stage
1 Heat Stage & 1 Cool Stage                  2 Heat Stage & 2 Cool Stage
1 Compress Stage        2 Compress Stage
1 Compress Stage + Aux Heat                  2 Compress Stage + Aux Heat
1 Stage                 2 Stage              None
```

`O(...)`/`B(...)` distinguish which reversing-valve position means cooling
vs heating. The board's `O/B` terminal follows that convention.

## Relays

Six outputs, matching the terminal silkscreen (`W`, `W2`, `Y`, `Y2`, `G`,
`O/B`):

```
HVAC_RELAY_W1  heat stage 1        "Heating relay W1 status"
HVAC_RELAY_W2  heat stage 2 / aux  "Heating relay W2 status"
HVAC_RELAY_Y1  cool stage 1        "Cooling relay Y1 status"
HVAC_RELAY_Y2  cool stage 2        "Cooling relay Y2 status"
HVAC_RELAY_G   fan                 "Fan relay G status"
HVAC_RELAY_OB  reversing valve     (not in the point registry)
```

## Setpoints and thresholds

| Setting | Firmware string | Address |
|---|---|---|
| Heat setpoint | `Heat Set Temp` | `0x08092B24` |
| Cool setpoint | `Cool Set Temp` | `0x08092B34` |
| Hold setpoint | `Hold Set Temp` | `0x08092B24` area |
| Min setpoint | `Min SetTemp Limit` | `0x08092B78` |
| Max setpoint | `Max SetTemp Limit` | `0x08092B68` |
| Deadband | `Deadband: %d'C` | `0x080738AC` |
| Switch difference | `Switch Difference: %d.%d'C` | `0x080738BC` |
| Balance point | `%d. Aux-Heat/HP Balance Temperature` | `0x08014A13` |

Temperatures are carried in **tenths of a degree** (the firmware prints
`%d.%d` for a value and `%d` for the Celsius-only variant), and humidity in
tenths of a percent.

## Control rules as implemented

The state machine is pure: it reads a state struct and produces relay
outputs. No hardware access, so it is host-testable.

**Demand evaluation** (`hvac.c`, `evaluate_demand`):

1. Mode `OFF`, or hold active, or holiday active → no demand.
2. Occupancy input inactive → no demand. (The firmware reports
   `Occupancy in open-circuit` / `Occupancy in short-circuit` errors at
   `0x080926A4`/`0x080926C0`, confirming the input gates the call.)
3. Frost protect active → no demand.
4. Heating demand when mode permits heat, equipment supports heat, and
   `room_temp < heat_setpoint - switch_difference`.
5. Cooling demand when mode permits cool, equipment supports cool, and
   `room_temp > cool_setpoint + switch_difference` (symmetric with heating;
   the deadband governs the heat/cool pair in AUTO).

**Staging.** Stage 2 engages only after stage 1 has soaked for
`STAGE2_DELAY_SECONDS` (120 s). The firmware models stage 2 not as a second
setpoint but as an **aux-heat/heat-pump balance point**
(`%d. Aux-Heat/HP Balance Temperature`), so the exact threshold function
still needs tracing — see [open-questions.md](open-questions.md).

**Exclusivity.** Heat and cool are never energised together. When both
demands coincide, the side with the larger error from its setpoint wins.

**Reversing valve.** Driven to the heating position when heating, cooling
position when cooling, and must settle before the compressor contactor
closes (`VALVE_SWITCH_SECONDS`).

**Fan.** Follows the compressor, or runs on request when the fan mode is
Low/Med/High. In `Fan`/`Ventilate` modes the fan runs with no compressor.

**Defrost.** For heat-pump equipment, a periodic defrost (60 min) triggered
when the outdoor sensor is below 5 °C; the compressor is suspended for its
duration (10 min). The firmware has a `Heat pump out door temp. setting`
menu (`0x08027057`) and a `defrost.bmp` icon, confirming the feature.

**Humidity.** Humidify/dehumidify are separate modes, and auxiliary
humidifier/dehumidifier share the `W2` output on heat-pump equipment
(`Aux Humidifier--W2_AUX`, `Aux Dehumidifier--W2_AUX`).

**Restart delays.** Each output class has its own minimum off-time, matching
the firmware's separate menu entries: `Compressor/Aux Heat Restart Time
Delay`, `Compressor Restart Time Delay`, `Heat/Cool Restart Time Delay`,
`Heat Restart Time Delay`, `Cool Restart Time Delay`. The rebuild tracks
these in state; the enforcement point still needs to be traced to the exact
call site.

**Published flags.** `Flag Holding`/`FlagiSHolding`,
`Flag Holidaying`/`FlagiSHolidaying`, `Flag TouchPad Lock`/
`FlagiSTouchPadLock` (`0x08092C8C`–`0x08092CE4`), plus `frost` and
`Sys Error`/`SysError` (`0x08092E7C`).

## Schedules and timers

- `temp_prog` — temperature schedule: `{"t":%s,"s":%d}` per entry.
- `timer_prog` — timer schedule: `{"n":%s,"f":%s}` per entry.
- `1440` = minutes in a day (`0x08067214`, `0x0806778B`).
- `24hrs` / `7 days` / `24 hours` / `7 days` options (`0x0806724B`,
  `0x08092EE4`, `0x08092EF0`).
- Hold and holiday each have an end time:
  `Hold End tTime` / `Holiday Start tTime` / `Holiday End tTime`.

## Time handling

`UTC%s`, `GMT(UTC)+%d`, `GMT(UTC)%d` (`0x080155FC`), `Weekday-Weekend`,
timezone list (UTC+1 … UTC+8), and named zones (`ALASKA TIME`,
`PACIFIC TIME`, `HAWAII TIME`, `CENTRAL TIME`, `MOUNTAIN TIME`,
`EASTERN TIME`). NTP servers: `north-america.pool.ntp.org`,
`europe.pool.ntp.org`, `cn.pool.ntp.org`, `pool.ntp.org`,
`ntp.ubuntu.com` (`0x080929D4`–`0x08092A24`). DST is supported
(`dst.bmp`).

## Host test coverage

`firmware/tests/test_main.c` verifies:

- heat demand → `W1` on, `Y1` off
- satisfied room → all relays off
- cool demand → `Y1` on, `W1` off
- hold, holiday and occupancy each suppress heating
- stage 2 does not engage immediately, and does after the soak
- heat and cool are never both on
- mode and equipment names match the firmware strings exactly

The whole harness currently reports **43 checks, 0 failures** — the HVAC
checks above plus framebuffer, BMP and asset-store coverage.
