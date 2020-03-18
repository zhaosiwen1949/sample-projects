// This file contains all the gameplay code associated with the third person gameplay sample. The code also exposes a
// componenet which is meant to be added to a single entity in a scene. This component will setup a system that is run
// once per frame, in which it will first update the gameplay context and then run the gameplay code.

#include <plugins/gameplay/gameplay.h>

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/carray.inl>
#include <foundation/error.h>
#include <foundation/input.h>
#include <foundation/localizer.h>
#include <foundation/math.inl>
#include <foundation/the_truth.h>
#include <plugins/animation/animation_state_machine.h>
#include <plugins/animation/animation_state_machine_component.h>
#include <plugins/entity/entity.h>
#include <plugins/physx/physx_scene.h>
#include <stdio.h>
#include <the_machinery/component_interfaces/editor_ui_interface.h>

static struct tm_animation_state_machine_api* tm_animation_state_machine_api;
static struct tm_entity_api* tm_entity_api;
static struct tm_error_api* tm_error_api;
static struct tm_gameplay_api* g;
static struct tm_input_api* tm_input_api;
static struct tm_physx_scene_api* tm_physx_scene_api;
static struct tm_the_truth_api* tm_the_truth_api;

typedef struct input_state_t {
    bool held_keys[TM_INPUT_KEYBOARD_ITEM_COUNT];
    tm_vec2_t mouse_delta;
} input_state_t;

typedef struct tm_gameplay_state_o {
    // Contains keyboard and mouse input state.
    input_state_t input;

    tm_entity_t player;
    tm_entity_t player_camera_pivot;
    tm_entity_t checkpoint_sphere;
    tm_vec3_t checkpoints_positions[8];
    uint32_t current_checkpoint;
    uint64_t processed_events;

    float camera_tilt;

    // For giving some extra time to press jump.
    float last_standing_time;

    // Component indices
    uint32_t asm_component;
    uint32_t mover_component;
} tm_gameplay_state_o;

static void start(tm_gameplay_context_t* ctx)
{
    tm_gameplay_state_o* state = ctx->state;
    state->player = g->entity->find_entity_with_tag(ctx, TM_STATIC_HASH("player", 0xafff68de8a0598dfULL));
    state->player_camera_pivot = tm_entity_api->resolve_path(ctx->entity_ctx, state->player, "CameraPivot");
    state->checkpoint_sphere = g->entity->find_entity_with_tag(ctx, TM_STATIC_HASH("checkpoint", 0x76169e4aa68e805dULL));
    state->camera_tilt = 3.18f;

    const tm_entity_t root_entity = g->entity->root_entity(ctx, state->player);
    char checkpoint_path[30];
    for (uint32_t i = 0; i < 8; ++i) {
        snprintf(checkpoint_path, 30, "Checkpoints/checkpoint-%u", (i + 1));
        const tm_entity_t c = tm_entity_api->resolve_path(ctx->entity_ctx, root_entity, checkpoint_path);

        if (!TM_ASSERT(tm_entity_api->is_alive(ctx->entity_ctx, c), tm_error_api->def, "Failed to find checkpoint entity"))
            continue;

        state->checkpoints_positions[i] = g->entity->get_position(ctx, c);
    }

    state->mover_component = tm_entity_api->lookup_component(ctx->entity_ctx, TM_TT_TYPE_HASH__PHYSX_MOVER_COMPONENT);
    state->asm_component = tm_entity_api->lookup_component(ctx->entity_ctx, TM_TT_TYPE_HASH__ANIMATION_STATE_MACHINE_COMPONENT);
}

static void update(tm_gameplay_context_t* ctx)
{
    tm_gameplay_state_o* state = ctx->state;
    state->input.mouse_delta.x = state->input.mouse_delta.y = 0;

    // Read input
    tm_input_event_t events[32];
    while (true) {
        uint64_t n = tm_input_api->events(state->processed_events, events, 32);
        for (uint64_t i = 0; i < n; ++i) {
            const tm_input_event_t* e = events + i;
            if (e->source && e->source->controller_type == TM_INPUT_CONTROLLER_TYPE_MOUSE) {
                if (e->item_id == TM_INPUT_MOUSE_ITEM_MOVE) {
                    state->input.mouse_delta.x += e->data.f.x;
                    state->input.mouse_delta.y += e->data.f.y;
                }
            }
            if (e->source && e->source->controller_type == TM_INPUT_CONTROLLER_TYPE_KEYBOARD) {
                if (e->type == TM_INPUT_EVENT_TYPE_DATA_CHANGE) {
                    state->input.held_keys[e->item_id] = e->data.f.x == 1.0f;
                }
            }
        }
        state->processed_events += n;
        if (n < 32)
            break;
    }

    struct tm_physx_mover_component_t* player_mover = tm_entity_api->get_component(ctx->entity_ctx, state->player, state->mover_component);

    // May not exist in first frame because physx hasn't created it yet.
    if (!player_mover)
        return;

    // For fudging jump timing->
    if (player_mover->is_standing)
        state->last_standing_time = ctx->time;

    // Camera pan control
    const float mouse_sens = 0.5f * ctx->dt;
    const float camera_pan_delta = -state->input.mouse_delta.x * mouse_sens;
    const tm_vec4_t pan_rot = tm_quaternion_from_rotation((tm_vec3_t){ 0, 1, 0 }, camera_pan_delta);
    const tm_vec4_t player_rot = g->entity->get_rotation(ctx, state->player);
    g->entity->set_rotation(ctx, state->player, tm_quaternion_mul(pan_rot, player_rot));

    // Camera tilt control
    const float camera_tilt_delta = -state->input.mouse_delta.y * mouse_sens;
    state->camera_tilt += camera_tilt_delta;
    state->camera_tilt = tm_clamp(state->camera_tilt, 1.5f, 3.8f);
    const tm_vec4_t camera_pivot_rot = tm_euler_to_quaternion((tm_vec3_t){ state->camera_tilt, 0, -TM_PI });
    g->entity->set_local_rotation(ctx, state->player_camera_pivot, camera_pivot_rot);

    // Control animation state machine using input
    tm_animation_state_machine_component_t* smc = tm_entity_api->get_component(ctx->entity_ctx, state->player, state->asm_component);
    tm_animation_state_machine_o* sm = smc->state_machine;
    tm_animation_state_machine_api->set_variable(sm, TM_STATIC_HASH("w", 0x22727cb14c3bb41dULL), (float)state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_W]);
    tm_animation_state_machine_api->set_variable(sm, TM_STATIC_HASH("a", 0x71717d2d36b6b11ULL), (float)state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_A]);
    tm_animation_state_machine_api->set_variable(sm, TM_STATIC_HASH("s", 0xe5db19474a903141ULL), (float)state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_S]);
    tm_animation_state_machine_api->set_variable(sm, TM_STATIC_HASH("d", 0x17dffbc5a8f17839ULL), (float)state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_D]);
    tm_animation_state_machine_api->set_variable(sm, TM_STATIC_HASH("run", 0xb8961af5ed6912f5ULL), (float)state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_LEFTSHIFT]);

    const bool can_jump = ctx->time < state->last_standing_time + 0.2f;
    if (can_jump && state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_SPACE]) {
        tm_animation_state_machine_api->event(sm, TM_STATIC_HASH("jump", 0x7b98bf53d1dceae8ULL));
        player_mover->velocity.y += 6;
        state->last_standing_time = 0;
    }

    // Check player against checkpoint
    const tm_vec3_t sphere_pos = g->entity->get_position(ctx, state->checkpoint_sphere);
    const tm_vec3_t player_pos = g->entity->get_position(ctx, state->player);

    if (tm_vec3_length(tm_vec3_sub(sphere_pos, player_pos)) < 1.5f) {
        ++state->current_checkpoint;

        if (state->current_checkpoint == 8)
            state->current_checkpoint = 0;

        g->entity->set_position(ctx, state->checkpoint_sphere, state->checkpoints_positions[state->current_checkpoint]);
    }
}

// Remainder of file is component set-up.

#define TYPE__GAMEPLAY_SAMPLE_THIRD_PERSON_COMPONENT "gameplay_sample_third_person"
#define TYPE_HASH__GAMEPLAY_SAMPLE_THIRD_PERSON_COMPONENT TM_STATIC_HASH("gameplay_sample_third_person", 0xc41214cbb6c0a92eULL)

typedef struct
{
    tm_entity_context_o* entity_ctx;
    tm_allocator_i allocator;
} gameplay_component_manager_t;

static void system_update(tm_entity_context_o* entity_ctx, tm_gameplay_context_t* ctx)
{
    g->context->update(ctx);

    if (!ctx->started) {
        ctx->state = tm_alloc(ctx->allocator, sizeof(*ctx->state));
        *ctx->state = (tm_gameplay_state_o){ 0 };
        start(ctx);
        ctx->started = true;
    }

    update(ctx);
}

static void component_added(gameplay_component_manager_t* manager, tm_entity_t e, tm_gameplay_context_t* ctx)
{
    const bool editor = tm_entity_api->get_blackboard_double(manager->entity_ctx, TM_ENTITY_BB__EDITOR, 0);
    if (editor)
        return;

    tm_allocator_i* a = &manager->allocator;
    g->context->init(ctx, a, manager->entity_ctx);

    const tm_entity_system_i gameplay_system = {
        .name = TM_LOCALIZE_LATER("Gameplay Sample Third Person"),
        .update = (void (*)(tm_entity_context_o*, tm_entity_system_o*))system_update,
        .inst = (tm_entity_system_o*)ctx
    };

    tm_entity_api->register_system(ctx->entity_ctx, &gameplay_system);
}

static void component_removed(gameplay_component_manager_t* manager, tm_entity_t e, tm_gameplay_context_t* ctx)
{
    const bool editor = tm_entity_api->get_blackboard_double(manager->entity_ctx, TM_ENTITY_BB__EDITOR, 0);
    if (editor)
        return;

    tm_free(ctx->allocator, ctx->state, sizeof(*ctx->state));
    g->context->shutdown(ctx);
}

static void destroy(gameplay_component_manager_t* manager)
{
    tm_entity_api->call_remove_on_all_entities(manager->entity_ctx, tm_entity_api->lookup_component(manager->entity_ctx, TYPE_HASH__GAMEPLAY_SAMPLE_THIRD_PERSON_COMPONENT));
    tm_allocator_i a = manager->allocator;
    tm_free(&a, manager, sizeof(*manager));
    tm_entity_api->destroy_child_allocator(manager->entity_ctx, &a);
}

static void create(tm_entity_context_o* entity_ctx)
{
    tm_allocator_i a;
    tm_entity_api->create_child_allocator(entity_ctx, TYPE__GAMEPLAY_SAMPLE_THIRD_PERSON_COMPONENT, &a);
    gameplay_component_manager_t* manager = tm_alloc(&a, sizeof(*manager));

    *manager = (gameplay_component_manager_t){
        .allocator = a,
        .entity_ctx = entity_ctx
    };

    const tm_component_i component = {
        .name = TYPE__GAMEPLAY_SAMPLE_THIRD_PERSON_COMPONENT,
        .bytes = sizeof(tm_gameplay_context_t),
        .manager = (tm_component_manager_o*)manager,
        .add = (void (*)(tm_component_manager_o*, tm_entity_t, void*))component_added,
        .remove = (void (*)(tm_component_manager_o*, tm_entity_t, void*))component_removed,
        .destroy = (void (*)(tm_component_manager_o*))destroy,
    };

    tm_entity_api->register_component(entity_ctx, &component);
}

static tm_ci_editor_ui_i editor_aspect = { 0 };

static void create_truth_types(struct tm_the_truth_o* tt)
{
    const uint64_t object_type = tm_the_truth_api->create_object_type(tt, TYPE__GAMEPLAY_SAMPLE_THIRD_PERSON_COMPONENT, 0, 0);
    const uint64_t component = tm_the_truth_api->create_object_of_type(tt, tm_the_truth_api->object_type_from_name_hash(tt, TYPE_HASH__GAMEPLAY_SAMPLE_THIRD_PERSON_COMPONENT), TM_TT_NO_UNDO_SCOPE);
    (void)component;

    // This is needed in order for the component to show up in the editor.
    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_EDITOR_UI, &editor_aspect);
}

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    g = reg->get(TM_GAMEPLAY_API_NAME);
    tm_animation_state_machine_api = reg->get(TM_ANIMATION_STATE_MACHINE_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_error_api = reg->get(TM_ERROR_API_NAME);
    tm_input_api = reg->get(TM_INPUT_API_NAME);
    tm_physx_scene_api = reg->get(TM_PHYSX_SCENE_API_NAME);
    tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, create_truth_types);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, create);
}