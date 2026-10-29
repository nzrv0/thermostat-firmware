/*
 * hvac.h - HVAC control state machine.
 *
 * Reconstructed from the shipped firmware. Every mode, setpoint and stage
 * name here is present in the image; the state machine's structure and
 * the sequencing rules are derived from the decompiled logic and are
 * documented in docs/hvac-spec.md.
 *
 * Modes (from the firmware's own enumerated strings):
 *   "Off", "Heat", "Cool", "Heat & Cool", "Auto", "Fan", "Ventilate",
 *   "Emergency Heat", "Dehumidify", "Humidify", "Dry", "Economizer"
 * System modes seen in the point table:
 *   "MC6_E", "MC6_TIMER", "MC6_2FCU", "MC6_4FCU", "MC6_HW" (0x08092D6C)
 * and the BACnet command objects:
 *   "SystemMode_Commend" | "Unit_SystemMode_Commend" (0x08090940)
 *   "System_Fan_Mode_Commend" | "Unit_Fan_Mode_Commend" (0x08090950)
 *   "System_FanSpeed_RunStatus" | "Unit Fan Speed Status" (0x0809085C)
 *
 * Equipment types (firmware menu strings, 0x08092E00 area):
 *   "Standard Heat/Cool", "Heat Pump", "Cool & Heat", "Cool Only",
 *   "Heat Only", "Dual Fuel", "O(Cool Active Reversing Valve)",
 *   "B(Heat Active Reversing Valve)", "Heat & Cool", "Aux Heat & Heatpump",
 *   "Heatpump Only", "1 Cool Stage", "2 Cool Stage",
 *   "1 Heat Stage & 1 Cool Stage", "2 Heat Stage & 2 Cool Stage",
 *   "1 Compress Stage", "2 Compress Stage", "1 Compress Stage + Aux Heat",
 *   "2 Compress Stage + Aux Heat", "1 Stage", "2 Stage", "None"
 *
 * Terminal semantics (PCB silkscreen + firmware strings):
 *   W  heat stage 1      W2 aux heat stage 2
 *   Y  cool stage 1      Y2 cool stage 2      G  fan      O/B valve
 */

#ifndef HVAC_H
#define HVAC_H

#include <stdint.h>

typedef enum
{
    HVAC_MODE_OFF = 0,
    HVAC_MODE_HEAT,
    HVAC_MODE_COOL,
    HVAC_MODE_HEAT_COOL,
    HVAC_MODE_AUTO,
    HVAC_MODE_FAN,
    HVAC_MODE_VENTILATE,
    HVAC_MODE_EMERGENCY_HEAT,
    HVAC_MODE_DEHUMIDIFY,
    HVAC_MODE_HUMIDIFY,
    HVAC_MODE_DRY,
    HVAC_MODE_ECONOMIZER,
    HVAC_MODE_COUNT
} hvac_mode_t;

typedef enum
{
    HVAC_FAN_OFF = 0,
    HVAC_FAN_LOW,
    HVAC_FAN_MED,
    HVAC_FAN_HIGH,
    HVAC_FAN_AUTO
} hvac_fan_mode_t;

typedef enum
{
    HVAC_EQUIP_STANDARD = 0,   /* Standard Heat/Cool */
    HVAC_EQUIP_HEAT_PUMP,       /* Heat Pump */
    HVAC_EQUIP_COOL_HEAT,       /* Cool & Heat */
    HVAC_EQUIP_COOL_ONLY,
    HVAC_EQUIP_HEAT_ONLY,
    HVAC_EQUIP_DUAL_FUEL,
    HVAC_EQUIP_1_COOL_STAGE,
    HVAC_EQUIP_2_COOL_STAGE,
    HVAC_EQUIP_1_HEAT_1_COOL,
    HVAC_EQUIP_2_HEAT_2_COOL,
    HVAC_EQUIP_1_COMPRESS,
    HVAC_EQUIP_2_COMPRESS,
    HVAC_EQUIP_1_COMPRESS_AUX,
    HVAC_EQUIP_2_COMPRESS_AUX,
    HVAC_EQUIP_1_STAGE,
    HVAC_EQUIP_2_STAGE,
    HVAC_EQUIP_NONE
} hvac_equipment_t;

/* Relay indices, in the order the Modbus/BACnet tables list them. */
typedef enum
{
    HVAC_RELAY_W1 = 0,   /* Heating relay W1 status */
    HVAC_RELAY_W2,       /* Heating relay W2 status */
    HVAC_RELAY_Y1,       /* Cooling relay Y1 status */
    HVAC_RELAY_Y2,       /* Cooling relay Y2 status */
    HVAC_RELAY_G,        /* Fan relay G status */
    HVAC_RELAY_OB        /* O/B reversing valve */
} hvac_relay_t;

typedef struct
{
    hvac_mode_t        mode;
    hvac_fan_mode_t    fan_mode;
    hvac_equipment_t  equipment;

    int16_t  room_temp;        /* 0.1 degC units, as the firmware reports */
    int16_t  outdoor_temp;
    int16_t  humidity;         /* 0.1 %RH units */
    int16_t  setpoint;         /* 0.1 degC */
    int16_t  heat_setpoint;
    int16_t  cool_setpoint;
    int16_t  deadband;         /* 0.1 degC - "Deadband: %d'C" */
    int16_t  switch_diff;      /* 0.1 degC - "Switch Difference: %d.%d'C" */
    int16_t  min_setpoint_limit;
    int16_t  max_setpoint_limit;

    uint8_t  heat_stage;       /* 0 = off, 1 = W, 2 = W+W2 */
    uint8_t  cool_stage;       /* 0 = off, 1 = Y, 2 = Y+Y2 */

    uint8_t  occupancy;        /* occupancy input state */
    uint8_t  hold_active;
    uint8_t  holiday_active;
    uint8_t  touchpad_locked;
    uint8_t  frost_protect;    /* "frost" */
    uint8_t  defrost_active;
    uint8_t  error_code;       /* "SysError" */

    /* Restart delays, in minutes - "Restart Delay: %d Mins" */
    uint16_t compressor_restart_delay;
    uint16_t aux_heat_restart_delay;
    uint16_t heat_restart_delay;
    uint16_t cool_restart_delay;
} hvac_state_t;

/* Output of the state machine: what the relays should be doing. */
typedef struct
{
    uint8_t relay[6];       /* indexed by hvac_relay_t */
} hvac_output_t;

void hvac_init(void);

/* Feed one sensor sample. Units are the same as hvac_state_t. */
void hvac_set_room_temp(int16_t tenth_c);
void hvac_set_outdoor_temp(int16_t tenth_c);
void hvac_set_humidity(int16_t tenth_pct);
void hvac_set_occupancy(int occupied);

/* User-facing configuration. */
void hvac_set_mode(hvac_mode_t mode);
void hvac_set_fan_mode(hvac_fan_mode_t fan);
void hvac_set_equipment(hvac_equipment_t equipment);
void hvac_set_setpoint(int16_t tenth_c);
void hvac_set_heat_setpoint(int16_t tenth_c);
void hvac_set_cool_setpoint(int16_t tenth_c);
void hvac_set_deadband(int16_t tenth_c);
void hvac_set_hold(int active);
void hvac_set_holiday(int active);
void hvac_set_touchpad_lock(int locked);
void hvac_set_restart_delays(uint16_t compressor, uint16_t aux_heat,
                             uint16_t heat, uint16_t cool);

/* Advance the state machine. Call periodically (the scheduler calls it on
 * the control tick). */
void hvac_tick(uint32_t elapsed_seconds);

/* Current state, for the protocol layers. */
const hvac_state_t *hvac_state(void);

/* Compute the desired relay outputs for the current state. */
void hvac_output(hvac_output_t *out);

const char *hvac_mode_name(hvac_mode_t mode);
const char *hvac_equipment_name(hvac_equipment_t equipment);
const char *hvac_fan_mode_name(hvac_fan_mode_t fan);

#endif /* HVAC_H */
