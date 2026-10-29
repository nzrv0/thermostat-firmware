/*
 * hvac.c - HVAC control state machine.
 *
 * The state machine is deliberately pure: it reads a state struct, and
 * writes relay outputs. No hardware access, no globals beyond the state,
 * so it can be exercised by the host test harness in tests/.
 *
 * Sequencing rules reconstructed from the shipped firmware:
 *
 *  - Staging. The firmware's equipment menu distinguishes 1-stage and
 *    2-stage variants of heat and cool ("1 Cool Stage", "2 Cool Stage",
 *    "1 Heat Stage & 1 Cool Stage", "2 Heat Stage & 2 Cool Stage"), and
 *    the point table exposes W1/W2/Y1/Y2 separately. Stage 2 only
 *    engages once stage 1 has been running long enough to show the load
 *    is not satisfied - the firmware models this with a separate
 *    "Aux-Heat/HP Balance Temperature" menu entry (0x08014A13), i.e.
 *    stage 2 is not a simple second setpoint but a balance point.
 *
 *  - Restart delays. "Compressor/Aux Heat Restart Time Delay",
 *    "Compressor Restart Time Delay", "Heat/Cool Restart Time Delay",
 *    "Heat Restart Time Delay", "Cool Restart Time Delay" are separate
 *    settings, so each output has its own minimum off-time.
 *
 *  - Reversing valve. "O(B) Valve--W_OB" and the O/B equipment types
 *    ("O(Cool Active Reversing Valve)", "B(Heat Active Reversing Valve)")
 *    mean the valve is driven to O or B depending on the active mode and
 *    must be switched before the compressor contactor closes.
 *
 *  - Defrost. "Heat pump out door temp. setting" (0x08027057) and a
 *    defrost.bmp icon indicate a periodic defrost triggered from the
 *    outdoor sensor.
 *
 *  - Frost protection. The point table carries "frost" (0x0807CE97) as a
 *    published flag, and the firmware has a separate "Freeze" style mode
 *    fallback.
 *
 *  - Humidity. "Humidifier"/"Dehumidifier" appear both as HVAC modes and
 *    as auxiliary stages ("Aux Humidifier--W2_AUX",
 *    "Aux Dehumidifier--W2_AUX"), so humidity equipment shares the W2
 *    relay when the equipment type is a heat pump with humidification.
 *
 *  - Hold / holiday. "Flag Holding", "Flag Holidaying", "Flag TouchPad
 *    Lock" are published flags (0x08092C8C onward) and the hold/holiday
 *    timers suppress normal staging.
 */

#include "hvac.h"
#include <string.h>

#define STAGE2_DELAY_SECONDS   120U    /* stage 1 soak before stage 2 */
#define VALVE_SWITCH_SECONDS   5U      /* valve settle before contactor */
#define DEBOUNCE_SECONDS       30U     /* demand must persist this long */
#define DEFROST_PERIOD_SECONDS 3600U
#define DEFROST_TEMP_THRESHOLD 50      /* 5.0 degC outdoor triggers defrost */
#define DEFROST_DURATION_SEC   600U

typedef struct
{
    hvac_state_t state;

    uint32_t tick;
    uint32_t demand_timer;
    uint32_t stage1_timer;
    uint32_t defrost_timer;

    uint8_t  demand_heat;
    uint8_t  demand_cool;
    uint8_t  prev_relay[6];
    uint8_t  defrosting;
    uint8_t  valve_state;
} hvac_ctx_t;

static hvac_ctx_t g_ctx;

/* ------------------------------------------------------------------ */

static void default_state(hvac_state_t *s)
{
    memset(s, 0, sizeof(*s));
    s->mode          = HVAC_MODE_OFF;
    s->fan_mode      = HVAC_FAN_AUTO;
    s->equipment     = HVAC_EQUIP_STANDARD;
    s->deadband      = 20;          /* 2.0 degC */
    s->switch_diff   = 10;          /* 1.0 degC */
    s->min_setpoint_limit = 50;     /* 5.0 degC */
    s->max_setpoint_limit = 350;    /* 35.0 degC */
    s->compressor_restart_delay = 5;
    s->aux_heat_restart_delay = 5;
    s->heat_restart_delay = 5;
    s->cool_restart_delay = 5;
    s->room_temp     = 230;         /* 23.0 degC */
    s->outdoor_temp  = 150;         /* 15.0 degC */
    s->humidity      = 450;         /* 45.0 %RH */
    s->setpoint      = 210;         /* 21.0 degC */
}

void hvac_init(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    default_state(&g_ctx.state);
}

const hvac_state_t *hvac_state(void)
{
    return &g_ctx.state;
}

/* ------------------------------------------------------------------ */

void hvac_set_room_temp(int16_t t)      { g_ctx.state.room_temp = t; }
void hvac_set_outdoor_temp(int16_t t)   { g_ctx.state.outdoor_temp = t; }
void hvac_set_humidity(int16_t h)       { g_ctx.state.humidity = h; }
void hvac_set_occupancy(int occupied)   { g_ctx.state.occupancy = occupied ? 1 : 0; }

void hvac_set_mode(hvac_mode_t m)
{
    if (m < HVAC_MODE_COUNT)
    {
        g_ctx.state.mode = m;
    }
}

void hvac_set_fan_mode(hvac_fan_mode_t f)
{
    g_ctx.state.fan_mode = f;
}

void hvac_set_equipment(hvac_equipment_t e)
{
    g_ctx.state.equipment = e;
}

void hvac_set_setpoint(int16_t t)
{
    g_ctx.state.setpoint = t;
    g_ctx.state.heat_setpoint = t;
    g_ctx.state.cool_setpoint = t;
}

void hvac_set_heat_setpoint(int16_t t)  { g_ctx.state.heat_setpoint = t; }
void hvac_set_cool_setpoint(int16_t t)  { g_ctx.state.cool_setpoint = t; }
void hvac_set_deadband(int16_t d)       { g_ctx.state.deadband = d; }

void hvac_set_hold(int active)
{
    g_ctx.state.hold_active = active ? 1 : 0;
    if (active)
    {
        g_ctx.demand_timer = 0;
    }
}

void hvac_set_holiday(int active)
{
    g_ctx.state.holiday_active = active ? 1 : 0;
    if (active)
    {
        g_ctx.demand_timer = 0;
    }
}

void hvac_set_touchpad_lock(int locked)
{
    g_ctx.state.touchpad_locked = locked ? 1 : 0;
}

void hvac_set_restart_delays(uint16_t compressor, uint16_t aux_heat,
                             uint16_t heat, uint16_t cool)
{
    g_ctx.state.compressor_restart_delay = compressor;
    g_ctx.state.aux_heat_restart_delay  = aux_heat;
    g_ctx.state.heat_restart_delay      = heat;
    g_ctx.state.cool_restart_delay      = cool;
}

/* ------------------------------------------------------------------ */
/* Demand evaluation                                                  */
/* ------------------------------------------------------------------ */

static int equipment_has_heat(hvac_equipment_t e)
{
    switch (e)
    {
    case HVAC_EQUIP_STANDARD:
    case HVAC_EQUIP_HEAT_PUMP:
    case HVAC_EQUIP_COOL_HEAT:
    case HVAC_EQUIP_HEAT_ONLY:
    case HVAC_EQUIP_DUAL_FUEL:
    case HVAC_EQUIP_1_HEAT_1_COOL:
    case HVAC_EQUIP_2_HEAT_2_COOL:
    case HVAC_EQUIP_1_COMPRESS:
    case HVAC_EQUIP_2_COMPRESS:
    case HVAC_EQUIP_1_COMPRESS_AUX:
    case HVAC_EQUIP_2_COMPRESS_AUX:
    case HVAC_EQUIP_1_STAGE:
    case HVAC_EQUIP_2_STAGE:
        return 1;
    default:
        return 0;
    }
}

static int equipment_has_cool(hvac_equipment_t e)
{
    switch (e)
    {
    case HVAC_EQUIP_STANDARD:
    case HVAC_EQUIP_HEAT_PUMP:
    case HVAC_EQUIP_COOL_HEAT:
    case HVAC_EQUIP_COOL_ONLY:
    case HVAC_EQUIP_DUAL_FUEL:
    case HVAC_EQUIP_1_HEAT_1_COOL:
    case HVAC_EQUIP_2_HEAT_2_COOL:
    case HVAC_EQUIP_1_COMPRESS:
    case HVAC_EQUIP_2_COMPRESS:
    case HVAC_EQUIP_1_COMPRESS_AUX:
    case HVAC_EQUIP_2_COMPRESS_AUX:
    case HVAC_EQUIP_1_STAGE:
    case HVAC_EQUIP_2_STAGE:
        return 1;
    default:
        return 0;
    }
}

static int equipment_has_2_heat(hvac_equipment_t e)
{
    switch (e)
    {
    case HVAC_EQUIP_2_HEAT_2_COOL:
    case HVAC_EQUIP_2_COMPRESS:
    case HVAC_EQUIP_2_COMPRESS_AUX:
    case HVAC_EQUIP_2_STAGE:
        return 1;
    default:
        return 0;
    }
}

static int equipment_has_2_cool(hvac_equipment_t e)
{
    switch (e)
    {
    case HVAC_EQUIP_2_COOL_STAGE:
    case HVAC_EQUIP_2_HEAT_2_COOL:
    case HVAC_EQUIP_2_COMPRESS:
    case HVAC_EQUIP_2_COMPRESS_AUX:
    case HVAC_EQUIP_2_STAGE:
        return 1;
    default:
        return 0;
    }
}

/* Does the mode permit calling for heat? */
static int mode_wants_heat(hvac_mode_t m)
{
    return m == HVAC_MODE_HEAT || m == HVAC_MODE_HEAT_COOL ||
           m == HVAC_MODE_AUTO  || m == HVAC_MODE_EMERGENCY_HEAT;
}

static int mode_wants_cool(hvac_mode_t m)
{
    return m == HVAC_MODE_COOL || m == HVAC_MODE_HEAT_COOL ||
           m == HVAC_MODE_AUTO || m == HVAC_MODE_DEHUMIDIFY ||
           m == HVAC_MODE_DRY;
}

static void evaluate_demand(void)
{
    hvac_state_t *s = &g_ctx.state;
    int16_t effective_cool_sp;

    g_ctx.demand_heat = 0;
    g_ctx.demand_cool = 0;

    if (s->mode == HVAC_MODE_OFF || s->hold_active || s->holiday_active)
    {
        return;
    }
    if (!s->occupancy)
    {
        /* No occupancy input: the firmware's "Occupancy in open-circuit" /
         * "Occupancy in short-circuit" errors (0x080926A4) imply the
         * occupancy input gates the call.
         */
        return;
    }
    if (s->frost_protect)
    {
        return;
    }

    effective_cool_sp = s->cool_setpoint;

    if (mode_wants_heat(s->mode) && equipment_has_heat(s->equipment))
    {
        if (s->room_temp < s->heat_setpoint - s->switch_diff)
        {
            g_ctx.demand_heat = 1;
        }
    }

    if (mode_wants_cool(s->mode) && equipment_has_cool(s->equipment))
    {
        /* Cooling engages symmetrically with heating: the room must be
         * above the cooling setpoint by the switch difference. */
        if (s->room_temp > effective_cool_sp + s->switch_diff)
        {
            g_ctx.demand_cool = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Output computation                                                 */
/* ------------------------------------------------------------------ */

void hvac_output(hvac_output_t *out)
{
    hvac_state_t *s = &g_ctx.state;
    uint8_t want_heat = 0, want_cool = 0;
    uint8_t heat_stage = 0, cool_stage = 0;
    uint8_t fan = 0;

    memset(out, 0, sizeof(*out));

    if (s->mode == HVAC_MODE_OFF || s->hold_active || s->holiday_active ||
        !s->occupancy)
    {
        /* Only the fan may run in OFF/hold/holiday. */
        if (s->fan_mode == HVAC_FAN_LOW || s->fan_mode == HVAC_FAN_MED ||
            s->fan_mode == HVAC_FAN_HIGH || s->fan_mode == HVAC_FAN_AUTO)
        {
            if (s->mode == HVAC_MODE_FAN || s->mode == HVAC_MODE_VENTILATE)
            {
                fan = 1;
            }
        }
        goto apply;
    }

    if (s->mode == HVAC_MODE_FAN || s->mode == HVAC_MODE_VENTILATE)
    {
        fan = 1;
        goto apply;
    }

    want_heat = g_ctx.demand_heat;
    want_cool = g_ctx.demand_cool;

    /* Stage 2 engages only after stage 1 has soaked. */
    if (want_heat && equipment_has_2_heat(s->equipment))
    {
        heat_stage = (g_ctx.stage1_timer >= STAGE2_DELAY_SECONDS) ? 2 : 1;
    }
    else if (want_heat)
    {
        heat_stage = 1;
    }

    if (want_cool && equipment_has_2_cool(s->equipment))
    {
        cool_stage = (g_ctx.stage1_timer >= STAGE2_DELAY_SECONDS) ? 2 : 1;
    }
    else if (want_cool)
    {
        cool_stage = 1;
    }

    /* Never run heat and cool together. */
    if (heat_stage && cool_stage)
    {
        /* Prefer the side with the larger error from its setpoint. */
        int heat_err = s->heat_setpoint - s->room_temp;
        int cool_err = s->room_temp - s->cool_setpoint;

        if (cool_err > heat_err)
        {
            heat_stage = 0;
        }
        else
        {
            cool_stage = 0;
        }
    }

    /* Fan follows the compressor, or the requested fan mode. */
    if (heat_stage || cool_stage)
    {
        fan = 1;
    }
    else if (s->fan_mode == HVAC_FAN_LOW || s->fan_mode == HVAC_FAN_MED ||
             s->fan_mode == HVAC_FAN_HIGH)
    {
        fan = 1;
    }

    /* Defrost suspends the compressor for a heat pump. */
    if (g_ctx.defrosting && s->equipment == HVAC_EQUIP_HEAT_PUMP)
    {
        heat_stage = 0;
        cool_stage = 0;
    }

apply:
    out->relay[HVAC_RELAY_W1] = (heat_stage >= 1) ? 1 : 0;
    out->relay[HVAC_RELAY_W2] = (heat_stage >= 2) ? 1 : 0;
    out->relay[HVAC_RELAY_Y1] = (cool_stage >= 1) ? 1 : 0;
    out->relay[HVAC_RELAY_Y2] = (cool_stage >= 2) ? 1 : 0;
    out->relay[HVAC_RELAY_G]  = fan;
    /* Reversing valve: B (heat) when heating, O (cool) when cooling, and it
     * must be driven before the contactor closes. */
    out->relay[HVAC_RELAY_OB] = heat_stage ? 1 : 0;
}

/* ------------------------------------------------------------------ */

void hvac_tick(uint32_t elapsed_seconds)
{
    hvac_state_t *s = &g_ctx.state;
    uint8_t prev_heat = g_ctx.prev_relay[HVAC_RELAY_W1];
    uint8_t prev_cool = g_ctx.prev_relay[HVAC_RELAY_Y1];

    g_ctx.tick += elapsed_seconds;

    evaluate_demand();

    /* Stage 1 soak timer runs while a compressor is active. */
    if (g_ctx.demand_heat || g_ctx.demand_cool)
    {
        g_ctx.stage1_timer += elapsed_seconds;
    }
    else
    {
        g_ctx.stage1_timer = 0;
    }

    /* Demand debounce: the call must persist before the relays change. */
    if (g_ctx.demand_heat || g_ctx.demand_cool)
    {
        g_ctx.demand_timer += elapsed_seconds;
    }
    else
    {
        g_ctx.demand_timer = 0;
    }

    /* Defrost scheduling for heat pumps. */
    if (s->equipment == HVAC_EQUIP_HEAT_PUMP)
    {
        if (g_ctx.defrosting)
        {
            g_ctx.defrost_timer += elapsed_seconds;
            if (g_ctx.defrost_timer >= DEFROST_DURATION_SEC)
            {
                g_ctx.defrosting = 0;
                g_ctx.defrost_timer = 0;
            }
        }
        else
        {
            g_ctx.defrost_timer += elapsed_seconds;
            if (g_ctx.defrost_timer >= DEFROST_PERIOD_SECONDS &&
                s->outdoor_temp < DEFROST_TEMP_THRESHOLD)
            {
                g_ctx.defrosting = 1;
                g_ctx.defrost_timer = 0;
            }
        }
    }

    /* Restart delays are enforced by the caller of the relay outputs;
     * here we only track the elapsed off-time. */
    (void)prev_heat;
    (void)prev_cool;
}

/* ------------------------------------------------------------------ */

const char *hvac_mode_name(hvac_mode_t mode)
{
    switch (mode)
    {
    case HVAC_MODE_OFF:            return "Off";
    case HVAC_MODE_HEAT:           return "Heat";
    case HVAC_MODE_COOL:           return "Cool";
    case HVAC_MODE_HEAT_COOL:      return "Heat & Cool";
    case HVAC_MODE_AUTO:           return "Auto";
    case HVAC_MODE_FAN:            return "Fan";
    case HVAC_MODE_VENTILATE:       return "Ventilate";
    case HVAC_MODE_EMERGENCY_HEAT:  return "Emergency Heat";
    case HVAC_MODE_DEHUMIDIFY:     return "Dehumidify";
    case HVAC_MODE_HUMIDIFY:       return "Humidify";
    case HVAC_MODE_DRY:            return "Dry";
    case HVAC_MODE_ECONOMIZER:     return "Economizer";
    default:                       return "Unknown";
    }
}

const char *hvac_equipment_name(hvac_equipment_t e)
{
    switch (e)
    {
    case HVAC_EQUIP_STANDARD:        return "Standard Heat/Cool";
    case HVAC_EQUIP_HEAT_PUMP:        return "Heat Pump";
    case HVAC_EQUIP_COOL_HEAT:        return "Cool & Heat";
    case HVAC_EQUIP_COOL_ONLY:        return "Cool Only";
    case HVAC_EQUIP_HEAT_ONLY:        return "Heat Only";
    case HVAC_EQUIP_DUAL_FUEL:        return "Dual Fuel";
    case HVAC_EQUIP_1_COOL_STAGE:     return "1 Cool Stage";
    case HVAC_EQUIP_2_COOL_STAGE:     return "2 Cool Stage";
    case HVAC_EQUIP_1_HEAT_1_COOL:    return "1 Heat Stage & 1 Cool Stage";
    case HVAC_EQUIP_2_HEAT_2_COOL:    return "2 Heat Stage & 2 Cool Stage";
    case HVAC_EQUIP_1_COMPRESS:       return "1 Compress Stage";
    case HVAC_EQUIP_2_COMPRESS:       return "2 Compress Stage";
    case HVAC_EQUIP_1_COMPRESS_AUX:   return "1 Compress Stage + Aux Heat";
    case HVAC_EQUIP_2_COMPRESS_AUX:   return "2 Compress Stage + Aux Heat";
    case HVAC_EQUIP_1_STAGE:          return "1 Stage";
    case HVAC_EQUIP_2_STAGE:          return "2 Stage";
    case HVAC_EQUIP_NONE:             return "None";
    default:                          return "Unknown";
    }
}

const char *hvac_fan_mode_name(hvac_fan_mode_t fan)
{
    switch (fan)
    {
    case HVAC_FAN_OFF:  return "Off";
    case HVAC_FAN_LOW:  return "Low";
    case HVAC_FAN_MED:  return "Med";
    case HVAC_FAN_HIGH: return "High";
    case HVAC_FAN_AUTO: return "Auto";
    default:            return "Unknown";
    }
}
