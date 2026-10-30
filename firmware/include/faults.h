/*
 * faults.h - error and sensor fault handling.
 *
 * The firmware publishes the strings "Sys Error", "Buildin sensor
 * error", "Remote air sensor error", "Floor sensor error" and
 * "Humidity sensor error" (0x08092E7C-0x08092ECC).
 */

#ifndef FAULTS_H
#define FAULTS_H

#include <stdint.h>

typedef enum
{
    FAULT_NONE = 0,
    FAULT_SYSTEM = 1u << 0,
    FAULT_BUILTIN_SENSOR = 1u << 1,
    FAULT_REMOTE_SENSOR = 1u << 2,
    FAULT_FLOOR_SENSOR = 1u << 3,
    FAULT_HUMIDITY_SENSOR = 1u << 4
} fault_bit_t;

typedef struct
{
    uint32_t active;   /* bitwise or of fault_bit_t */
    uint32_t latched;  /* faults that have not been cleared yet */
} fault_state_t;

void fault_init(fault_state_t *state);

void fault_set(fault_state_t *state, uint32_t fault);
void fault_clear(fault_state_t *state, uint32_t fault);
void fault_clear_all(fault_state_t *state);

int  fault_active(const fault_state_t *state, uint32_t fault);
int  fault_any(const fault_state_t *state);

/* The firmware's wording for a fault, or NULL when the bit is unknown. */
const char *fault_name(uint32_t fault);

#endif /* FAULTS_H */
