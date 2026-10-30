/*
 * faults.c - error and sensor fault handling.
 */

#include <string.h>
#include "faults.h"

void fault_init(fault_state_t *state)
{
    memset(state, 0, sizeof(*state));
}

void fault_set(fault_state_t *state, uint32_t fault)
{
    state->active |= fault;
    state->latched |= fault;
}

void fault_clear(fault_state_t *state, uint32_t fault)
{
    state->active &= ~fault;
}

void fault_clear_all(fault_state_t *state)
{
    state->active = 0;
    state->latched = 0;
}

int fault_active(const fault_state_t *state, uint32_t fault)
{
    return (state->active & fault) ? 1 : 0;
}

int fault_any(const fault_state_t *state)
{
    return state->active ? 1 : 0;
}

const char *fault_name(uint32_t fault)
{
    switch (fault)
    {
    case FAULT_SYSTEM:          return "Sys Error";
    case FAULT_BUILTIN_SENSOR:   return "Buildin sensor error";
    case FAULT_REMOTE_SENSOR:    return "Remote air sensor error";
    case FAULT_FLOOR_SENSOR:     return "Floor sensor error";
    case FAULT_HUMIDITY_SENSOR:  return "Humidity sensor error";
    default:                     return 0;
    }
}
