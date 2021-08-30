// This is a skeleton for writing gameplay code in C. This file adds an implementation of the interface
// `tm_simulate_entry_i`, which can then be referecned from a `.simulate_entry` asset in within a The Machinery project.
// When the Simulate Tab (or Runner) is active, it will look in the folder where the currently simulated entity lives,
// or any parent folder. If it finds a `.simulate_entry` asset, it will use the `tm_simulate_entry_i` interface
// referenced in there in order to enter the `start`, `stop` and `update` functions of this file.

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/localizer.h>
#include <foundation/log.h>

#include <plugins/simulation/simulate_entry.h>

static struct tm_logger_api *tm_logger_api;
static struct tm_localizer_api *tm_localizer_api;

struct tm_simulate_state_o {
    tm_allocator_i *allocator;
    uint64_t some_state;
} tm_gameplay_state_o;

static tm_simulate_state_o *start(tm_simulate_start_args_t *args)
{
    TM_LOG("Empty Sample Start");
 
    tm_simulate_state_o *state = tm_alloc(args->allocator, sizeof(*state));
    *state = (tm_simulate_state_o) {
        .allocator = args->allocator,
    };

    return state;
}

static void stop(tm_simulate_state_o *state)
{
    TM_LOG("Empty Sample Stop");
    tm_allocator_i a = *state->allocator;
    tm_free(&a, state, sizeof(*state));
}

static void tick(tm_simulate_state_o *state, tm_simulate_frame_args_t *args)
{
    TM_LOG("Empty Sample Update. Counter: %u. Frame time: %f", state->some_state, args->dt);
    ++state->some_state;
}

static tm_simulate_entry_i simulate_entry_i = {
    .id = TM_STATIC_HASH("tm_gameplay_sample_empty_simulate_entry_i", 0xf6606c11f7ebaf95ULL),
    .display_name = TM_LOCALIZE_LATER("Gameplay Sample Empty"),
    .start = start,
    .stop = stop,
    .tick = tick,
};

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    tm_localizer_api = reg->get(TM_LOCALIZER_API_NAME);
    tm_logger_api = reg->get(TM_LOGGER_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_SIMULATE_ENTRY_INTERFACE_NAME, &simulate_entry_i);
}