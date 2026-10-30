/*
 * differential.c - the differential test against the recovered control
 * rules.
 *
 * This is the software-only half of open question 12. The original's
 * control function (0x0807f9bc, 1249 instructions) cannot be run
 * without a physical unit, so the comparison is made against a
 * *reference model*: an independent transcription of the control rules
 * in docs/hvac-spec.md, written without reference to the
 * implementation in src/app/hvac.c.
 *
 * Both models are then driven with the same stimulus matrix and their
 * six relay outputs are compared. A divergence means either the
 * implementation or the specification is wrong, and the harness prints
 * the case so it can be reproduced.
 *
 * The matrix is deterministic: every case is a tuple of mode,
 * equipment, temperatures, setpoints and flags, walked in a fixed
 * order, so a failure is always reproducible.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "hvac.h"

extern int  g_failures;
extern int  g_checks;

/* ------------------------------------------------------------------ */
/* The reference model                                                  */
/* ------------------------------------------------------------------ */

/* One stimulus. */
typedef struct
{
    hvac_mode_t       mode;
    hvac_fan_mode_t   fan_mode;
    hvac_equipment_t equipment;
    int16_t  room_temp;
    int16_t  outdoor_temp;
    int16_t  heat_setpoint;
    int16_t  cool_setpoint;
    int16_t  switch_diff;
    uint8_t  occupancy;
    uint8_t  hold_active;
    uint8_t  holiday_active;
    uint8_t  frost_protect;
    uint8_t  soaked;      /* the stage-1 soak has elapsed */
    uint8_t  defrosting;  /* a defrost is in progress */
} ref_case_t;

/* The modes that permit a call for heat, as the specification's mode
 * table implies: Heat, Heat & Cool, Auto and Emergency Heat. */
static int ref_mode_heat(hvac_mode_t m)
{
    return m == HVAC_MODE_HEAT || m == HVAC_MODE_HEAT_COOL ||
           m == HVAC_MODE_AUTO || m == HVAC_MODE_EMERGENCY_HEAT;
}

/* The modes that permit a call for cooling: Cool, Heat & Cool, Auto,
 * and the two modes that exist to cool the coil, Dehumidify and Dry. */
static int ref_mode_cool(hvac_mode_t m)
{
    return m == HVAC_MODE_COOL || m == HVAC_MODE_HEAT_COOL ||
           m == HVAC_MODE_AUTO || m == HVAC_MODE_DEHUMIDIFY ||
           m == HVAC_MODE_DRY;
}

/* The equipment that can heat, from the specification's equipment
 * list. */
static int ref_equip_heat(hvac_equipment_t e)
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

/* The equipment that can cool. "1 Cool Stage" is a cooling-only
 * configuration, so it belongs here. */
static int ref_equip_cool(hvac_equipment_t e)
{
    switch (e)
    {
    case HVAC_EQUIP_STANDARD:
    case HVAC_EQUIP_HEAT_PUMP:
    case HVAC_EQUIP_COOL_HEAT:
    case HVAC_EQUIP_COOL_ONLY:
    case HVAC_EQUIP_1_COOL_STAGE:
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

static int ref_equip_2_heat(hvac_equipment_t e)
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

static int ref_equip_2_cool(hvac_equipment_t e)
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

/* The reference's relay outputs, from the specification's rules. */
static void ref_output(const ref_case_t *c, uint8_t out[6])
{
    int want_heat = 0;
    int want_cool = 0;
    int heat_stage = 0;
    int cool_stage = 0;
    int fan = 0;

    memset(out, 0, 6);

    /* A hold or a holiday switches the system off, and so does Off. */
    if (c->mode == HVAC_MODE_OFF || c->hold_active || c->holiday_active)
    {
        return;
    }

    /* In the fan and ventilate modes the fan runs and nothing else. */
    if (c->mode == HVAC_MODE_FAN || c->mode == HVAC_MODE_VENTILATE)
    {
        out[HVAC_RELAY_G] = 1;
        return;
    }

    /* The occupancy input gates the call for heat or cooling, not the
     * fan. */
    if (!c->occupancy)
    {
        if (c->fan_mode == HVAC_FAN_LOW || c->fan_mode == HVAC_FAN_MED ||
            c->fan_mode == HVAC_FAN_HIGH)
        {
            out[HVAC_RELAY_G] = 1;
        }
        return;
    }

    if (!c->frost_protect)
    {
        if (ref_mode_heat(c->mode) && ref_equip_heat(c->equipment) &&
            c->room_temp < c->heat_setpoint - c->switch_diff)
        {
            want_heat = 1;
        }
        if (ref_mode_cool(c->mode) && ref_equip_cool(c->equipment) &&
            c->room_temp > c->cool_setpoint + c->switch_diff)
        {
            want_cool = 1;
        }
    }

    if (want_heat)
    {
        heat_stage = ref_equip_2_heat(c->equipment) && c->soaked ? 2 : 1;
    }
    if (want_cool)
    {
        cool_stage = ref_equip_2_cool(c->equipment) && c->soaked ? 2 : 1;
    }

    /* Heat and cool are never energised together; the side with the
     * larger error from its setpoint wins, and heat wins a tie. */
    if (heat_stage && cool_stage)
    {
        int heat_err = c->heat_setpoint - c->room_temp;
        int cool_err = c->room_temp - c->cool_setpoint;

        if (heat_err >= cool_err)
        {
            cool_stage = 0;
        }
        else
        {
            heat_stage = 0;
        }
    }

    if (heat_stage || cool_stage)
    {
        fan = 1;
    }
    else if (c->fan_mode == HVAC_FAN_LOW || c->fan_mode == HVAC_FAN_MED ||
             c->fan_mode == HVAC_FAN_HIGH)
    {
        fan = 1;
    }

    /* A defrost suspends the compressor of a heat pump. */
    if (c->defrosting && c->equipment == HVAC_EQUIP_HEAT_PUMP)
    {
        heat_stage = 0;
        cool_stage = 0;
    }

    out[HVAC_RELAY_W1] = (uint8_t)(heat_stage >= 1);
    out[HVAC_RELAY_W2] = (uint8_t)(heat_stage >= 2);
    out[HVAC_RELAY_Y1] = (uint8_t)(cool_stage >= 1);
    out[HVAC_RELAY_Y2] = (uint8_t)(cool_stage >= 2);
    out[HVAC_RELAY_G] = (uint8_t)fan;
    /* The reversing valve sits in the heating position when heating. */
    out[HVAC_RELAY_OB] = (uint8_t)(heat_stage != 0);
}

/* ------------------------------------------------------------------ */
/* The driver                                                           */
/* ------------------------------------------------------------------ */

static const char *relay_names[6] = { "W1", "W2", "Y1", "Y2", "G", "OB" };

static void drive_implementation(const ref_case_t *c, uint32_t tick,
                                 uint8_t out[6])
{
    hvac_output_t got;
    int i;

    hvac_init();
    hvac_set_mode(c->mode);
    hvac_set_fan_mode(c->fan_mode);
    hvac_set_equipment(c->equipment);
    hvac_set_room_temp(c->room_temp);
    hvac_set_outdoor_temp(c->outdoor_temp);
    hvac_set_heat_setpoint(c->heat_setpoint);
    hvac_set_cool_setpoint(c->cool_setpoint);
    hvac_set_occupancy(c->occupancy);
    hvac_set_hold(c->hold_active);
    hvac_set_holiday(c->holiday_active);
    hvac_set_switch_diff(c->switch_diff);

    hvac_tick(tick);

    hvac_output(&got);
    for (i = 0; i < 6; i++)
    {
        out[i] = got.relay[i];
    }
}

static void describe(const ref_case_t *c, char *buf, uint32_t size)
{
    snprintf(buf, size,
             "mode=%s equip=%s room=%d heat_sp=%d cool_sp=%d diff=%d "
             "occ=%d hold=%d hol=%d frost=%d",
             hvac_mode_name(c->mode), hvac_equipment_name(c->equipment),
             c->room_temp, c->heat_setpoint, c->cool_setpoint,
             c->switch_diff, c->occupancy, c->hold_active,
             c->holiday_active, c->frost_protect);
}

void run_differential_tests(void)
{
    static const int16_t temps[] = { 50, 180, 200, 210, 215, 220, 230, 250,
                                     300 };
    static const struct { int16_t heat; int16_t cool; } sps[] = {
        { 210, 240 }, { 240, 210 }, { 220, 220 }, { 200, 260 }
    };
    static const uint8_t flags[] = { 0, 1 };
    uint32_t mode, equip, ti, si, fi, gi;
    uint32_t cases = 0;
    uint32_t divergences = 0;
    char text[256];

    printf("Differential: the rebuild against the recovered control rules\n");

    for (mode = 0; mode < HVAC_MODE_COUNT; mode++)
    {
        for (equip = 0; equip < HVAC_EQUIP_NONE; equip++)
        {
            for (ti = 0; ti < sizeof(temps) / sizeof(temps[0]); ti++)
            {
                for (si = 0; si < sizeof(sps) / sizeof(sps[0]); si++)
                {
                    for (fi = 0; fi < 2; fi++)   /* occupancy */
                    {
                        for (gi = 0; gi < 2; gi++)   /* hold and holiday */
                        {
                            ref_case_t c;
                            uint8_t got[6];
                            uint8_t want[6];
                            uint32_t scenario;

                            memset(&c, 0, sizeof(c));
                            c.mode = (hvac_mode_t)mode;
                            c.equipment = (hvac_equipment_t)equip;
                            c.room_temp = temps[ti];
                            c.outdoor_temp = 20;
                            c.heat_setpoint = sps[si].heat;
                            c.cool_setpoint = sps[si].cool;
                            c.switch_diff = 10;
                            c.occupancy = (uint8_t)fi;
                            c.hold_active = (uint8_t)gi;
                            c.holiday_active = 0;
                            c.frost_protect = 0;

                            /* Three scenarios: fresh, soaked and
                             * defrosting. */
                            for (scenario = 0; scenario < 3; scenario++)
                            {
                                uint32_t tick = (scenario == 0) ? 0 :
                                                (scenario == 1) ? 200 : 4000;
                                int i;

                                c.soaked = (scenario >= 1) ? 1 : 0;
                                c.defrosting = (scenario == 2) ? 1 : 0;

                                drive_implementation(&c, tick, got);
                                ref_output(&c, want);

                                cases++;
                                for (i = 0; i < 6; i++)
                                {
                                    if (got[i] != want[i])
                                    {
                                        divergences++;
                                        describe(&c, text, sizeof(text));
                                        printf("  DIVERGENCE %s: %s "
                                               "implementation=%d "
                                               "reference=%d\n",
                                               relay_names[i], text, got[i],
                                               want[i]);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    g_checks++;
    if (divergences == 0)
    {
        printf("  %u cases, no divergence\n", cases);
    }
    else
    {
        g_failures++;
        printf("  %u cases, %u divergences\n", cases, divergences);
    }
}
