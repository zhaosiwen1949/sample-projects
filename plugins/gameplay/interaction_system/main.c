// Main entry point file for Interaction System sample. This sample exposes two things to the editor:
// * An Interaction Component that lets you define buttons, levers and rotating doors from within
// the editor.
// * An implementation of [[tm_simulation_entry_i]], which is how the engine gives control to
// the [[start()]], [[stop()]] and [[tick()]] functions. This implementation is referenced by the
// Simulation Entry Component in `world.entity` in the project.

static struct tm_application_api* tm_application_api;
static struct tm_draw2d_api* tm_draw2d_api;
static struct tm_entity_api* tm_entity_api;
static struct tm_error_api* tm_error_api;
static struct tm_input_api* tm_input_api;
static struct tm_physics_collision_api* tm_physics_collision_api;
static struct tm_physx_scene_api* tm_physx_scene_api;
static struct tm_simulation_api* tm_simulation_api;
static struct tm_tag_component_api* tm_tag_component_api;
static struct tm_temp_allocator_api* tm_temp_allocator_api;
static struct tm_temp_allocator_api* tm_temp_allocator_api;
static struct tm_transform_component_api* tm_transform_component_api;
static struct tm_ui_api* tm_ui_api;
static struct tm_interactable_component_api* tm_interactable_component_api;
static struct tm_gamestate_api* tm_gamestate_api;

#include "interactable_component.h"

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/application.h>
#include <foundation/error.h>
#include <foundation/input.h>
#include <foundation/murmurhash64a.inl>
#include <foundation/temp_allocator.h>
#include <foundation/the_truth.h>

#include <plugins/entity/entity.h>
#include <plugins/entity/tag_component.h>
#include <plugins/entity/transform_component.h>
#include <plugins/gamestate/gamestate.h>
#include <plugins/physics/physics_collision.h>
#include <plugins/physics/physics_mover_component.h>
#include <plugins/physx/physx_scene.h>
#include <plugins/simulation/simulation.h>
#include <plugins/simulation/simulation_entry.h>
#include <plugins/ui/draw2d.h>
#include <plugins/ui/ui.h>

#include <foundation/carray.inl>
#include <foundation/math.inl>

typedef struct input_state_t {
    bool held_keys[TM_INPUT_KEYBOARD_ITEM_COUNT];
    bool left_mouse_held;
    bool left_mouse_pressed;
    TM_PAD(1);
    tm_vec2_t mouse_delta;
} input_state_t;

struct tm_simulation_state_o {
    input_state_t input;
    tm_entity_t player;
    tm_entity_t player_camera;

    tm_tt_id_t player_collision_type;

    bool mouse_captured;
    TM_PAD(3);

    float look_yaw;
    float look_pitch;
    TM_PAD(4);

    double last_standing_time;

    uint64_t processed_events;
    tm_entity_context_o* entity_ctx;
    tm_the_truth_o* tt;
    tm_simulation_o* simulation_ctx;
    tm_allocator_i* allocator;

    tm_transform_component_manager_o* trans_mgr;
    tm_tag_component_manager_o* tag_mgr;
    tm_interactable_component_manager_o* interactable_mgr;

    tm_component_type_t transform_comp;
    tm_component_type_t tag_comp;
    tm_component_type_t interact_comp;
    TM_PAD(4);
};

typedef struct simulate_persistent_state {
    tm_gamestate_object_id_t player;
    tm_gamestate_object_id_t player_camera;

    float look_yaw;
    float look_pitch;

    double last_standing_time;
} simulate_persistent_state;

static void serialize(void* s, void* d)
{
    tm_simulation_state_o* source = (tm_simulation_state_o*) s;
    simulate_persistent_state* dest = (simulate_persistent_state*) d;
    
    tm_entity_api->get_entity_persistent_id(source->entity_ctx, source->player, &dest->player);
    tm_entity_api->get_entity_persistent_id(source->entity_ctx, source->player_camera, &dest->player_camera);

    dest->look_yaw = source->look_yaw;
    dest->look_pitch = source->look_pitch;

    dest->last_standing_time = source->last_standing_time;
}

static void deserialize(void* d, void* s)
{
    tm_simulation_state_o* dest = (tm_simulation_state_o*) d;
    simulate_persistent_state* source = (simulate_persistent_state*) s;
    
    dest->player = tm_entity_api->lookup_entity_from_gamestate_id(dest->entity_ctx, &source->player);
    dest->player_camera = tm_entity_api->lookup_entity_from_gamestate_id(dest->entity_ctx, &source->player_camera);

    dest->look_yaw = source->look_yaw;
    dest->look_pitch = source->look_pitch;

    dest->last_standing_time = source->last_standing_time;

    tm_simulation_api->set_camera(dest->simulation_ctx, dest->player_camera);
}

static tm_simulation_state_o* start(tm_simulation_start_args_t* args)
{
    tm_simulation_state_o* state = tm_alloc(args->allocator, sizeof(*state));
    *state = (tm_simulation_state_o){
        .allocator = args->allocator,
        .entity_ctx = args->entity_ctx,
        .simulation_ctx = args->simulation_ctx,
        .tt = args->tt,
    };

    state->interact_comp = tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__INTERACTABLE_COMPONENT);
    state->tag_comp = tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__TAG_COMPONENT);
    state->transform_comp = tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);

    state->tag_mgr = (tm_tag_component_manager_o*)tm_entity_api->component_manager(state->entity_ctx, state->tag_comp);
    state->trans_mgr = (tm_transform_component_manager_o*)tm_entity_api->component_manager(state->entity_ctx, state->transform_comp);
    state->interactable_mgr = (tm_interactable_component_manager_o*)tm_entity_api->component_manager(state->entity_ctx, state->interact_comp);

    state->player = tm_tag_component_api->find_first(state->tag_mgr, TM_STATIC_HASH("player", 0xafff68de8a0598dfULL));
    state->player_camera = tm_tag_component_api->find_first(state->tag_mgr, TM_STATIC_HASH("player_camera", 0x689cd442a211fda4ULL));
    tm_simulation_api->set_camera(state->simulation_ctx, state->player_camera);

    TM_INIT_TEMP_ALLOCATOR(ta);
    tm_physics_collision_t* all_collision_types = tm_physics_collision_api->find_all(state->tt, ta);
    const tm_strhash_t player_coll_type = TM_STATIC_HASH("player", 0xafff68de8a0598dfULL);
    for (uint32_t coll_type = 0; coll_type < tm_carray_size(all_collision_types); ++coll_type) {
        if (TM_STRHASH_U64(all_collision_types[coll_type].name) == TM_STRHASH_U64(player_coll_type)) {
            state->player_collision_type = all_collision_types[coll_type].collision;
            break;
        }
    }
    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
    
    const char* singleton_name = "interation_sample_simulation_state";
    tm_gamestate_o* gamestate = tm_entity_api->gamestate(state->entity_ctx);
    tm_gamestate_singleton_t s = {
        .name = singleton_name,
        .size = sizeof(simulate_persistent_state),
        .serialize = serialize,
        .deserialize = deserialize,
    };
    
    tm_gamestate_api->add_singleton(gamestate, s, state);
    tm_gamestate_api->deserialize_singleton(gamestate, singleton_name, state);
    
    return state;
}

static void stop(tm_simulation_state_o* state,struct tm_entity_commands_o *commands)
{
    tm_allocator_i a = *state->allocator;
    tm_free(&a, state, sizeof(*state));
}

static void tick(tm_simulation_state_o* state, tm_simulation_frame_args_t* args)
{
    // Reset per-frame-input
    state->input.mouse_delta.x = state->input.mouse_delta.y = 0;
    state->input.left_mouse_pressed = false;

    // Read input
    tm_input_event_t events[32];
    bool mouse_captured_this_frame = state->mouse_captured;
    while (true) {
        uint64_t n = tm_input_api->events(state->processed_events, events, 32);

        for (uint64_t i = 0; i < n; ++i) {
            const tm_input_event_t* e = events + i;
            if (mouse_captured_this_frame) {
                if (e->source && e->source->controller_type == TM_INPUT_CONTROLLER_TYPE_MOUSE) {
                    if (e->item_id == TM_INPUT_MOUSE_ITEM_BUTTON_LEFT) {
                        const bool down = e->data.f.x > 0.5f;
                        state->input.left_mouse_pressed = down && !state->input.left_mouse_held;
                        state->input.left_mouse_held = down;
                    } else if (e->item_id == TM_INPUT_MOUSE_ITEM_MOVE) {
                        state->input.mouse_delta.x += e->data.f.x;
                        state->input.mouse_delta.y += e->data.f.y;
                    }
                }
                if (e->source && e->source->controller_type == TM_INPUT_CONTROLLER_TYPE_KEYBOARD) {
                    if (e->type == TM_INPUT_EVENT_TYPE_DATA_CHANGE) {
                        state->input.held_keys[e->item_id] = e->data.f.x == 1.0f;
                    }
                }
            } else {
                if (e->source && e->source->controller_type == TM_INPUT_CONTROLLER_TYPE_MOUSE) {
                    if (e->item_id == TM_INPUT_MOUSE_ITEM_BUTTON_LEFT) {
                        const bool down = e->data.f.x > 0.5f;
                        state->input.left_mouse_pressed = down && !state->input.left_mouse_held;
                        state->input.left_mouse_held = down;
                    }
                }

                if (e->source && e->source->controller_type == TM_INPUT_CONTROLLER_TYPE_KEYBOARD) {
                    if (e->item_id == TM_INPUT_KEYBOARD_ITEM_ESCAPE && e->type == TM_INPUT_EVENT_TYPE_DATA_CHANGE) {
                        state->input.held_keys[e->item_id] = e->data.f.x == 1.0f;
                    }
                }
            }
        }

        state->processed_events += n;
        if (n < 32)
            break;
    }

    // Capture mouse
    if (args->ui) {
        if (!args->running_in_editor || (tm_ui_api->is_hovering(args->ui, args->rect, 0) && state->input.left_mouse_pressed)) {
            state->mouse_captured = true;
        }

        if ((args->running_in_editor && state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_ESCAPE]) || !tm_ui_api->window_has_focus(args->ui)) {
            state->mouse_captured = false;
            struct tm_application_o* app = tm_application_api->application();
            tm_application_api->set_cursor_hidden(app, false);
        }

        if (state->mouse_captured) {
            struct tm_application_o* app = tm_application_api->application();
            tm_application_api->set_cursor_hidden(app, true);
        }
    }

    // Updates any inteactables that are moving etc.
    tm_interactable_component_api->update_active_interactables(state->interactable_mgr, args->dt, args->time);

    const tm_vec3_t camera_pos = tm_get_position(state->trans_mgr, state->player_camera);
    const tm_vec4_t camera_rot = tm_get_rotation(state->trans_mgr, state->player_camera);
    struct tm_physx_mover_component_t* player_mover = tm_entity_api->get_component_by_hash(state->entity_ctx, state->player, TM_TT_TYPE_HASH__PHYSX_MOVER_COMPONENT);

    if (!TM_ASSERT(player_mover, "Invalid player"))
        return;

    if (player_mover->is_standing)
        state->last_standing_time = args->time;

    // Process input if mouse is captured.
    if (state->mouse_captured) {
        // Exit on ESC
        if (!args->running_in_editor && state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_ESCAPE])
            tm_application_api->exit(tm_application_api->application());

        tm_vec3_t local_movement = { 0 };
        if (state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_A])
            local_movement.x -= 1.0f;
        if (state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_D])
            local_movement.x += 1.0f;
        if (state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_W])
            local_movement.z -= 1.0f;
        if (state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_S])
            local_movement.z += 1.0f;

        // Move
        if (tm_vec3_length(local_movement) != 0) {
            tm_vec3_t rotated_movement = tm_quaternion_rotate_vec3(camera_rot, local_movement);
            rotated_movement.y = 0;
            const tm_vec3_t normalized_rotated_movement = tm_vec3_normalize(rotated_movement);
            const tm_vec3_t final_movement = tm_vec3_mul(normalized_rotated_movement, 5);
            player_mover->velocity.x = final_movement.x;
            player_mover->velocity.z = final_movement.z;
        } else {
            player_mover->velocity.x = 0;
            player_mover->velocity.z = 0;
        }

        // Look
        const float mouse_sens = 0.1f * args->dt;
        state->look_yaw -= state->input.mouse_delta.x * mouse_sens;
        state->look_pitch -= state->input.mouse_delta.y * mouse_sens;
        state->look_pitch = tm_clamp(state->look_pitch, -TM_PI / 3, TM_PI / 3);
        const tm_vec4_t yawq = tm_quaternion_from_rotation((tm_vec3_t){ 0, 1, 0 }, state->look_yaw);
        const tm_vec3_t local_sideways = tm_quaternion_rotate_vec3(yawq, (tm_vec3_t){ 1, 0, 0 });
        const tm_vec4_t pitchq = tm_quaternion_from_rotation(local_sideways, state->look_pitch);
        tm_set_local_rotation(state->trans_mgr, state->player_camera, tm_quaternion_mul(pitchq, yawq));

        // Jump
        const bool can_jump = args->time < state->last_standing_time + 0.2f;
        if (can_jump && state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_SPACE]) {
            player_mover->velocity.y = 3.5;
            state->last_standing_time = 0;
        }
    }

    // Modified if the raycast below hits something that can be interacted with.
    tm_color_srgb_t crosshair_color = { 70, 80, 70, 255 };

    const tm_vec3_t camera_forward = tm_quaternion_rotate_vec3(camera_rot, (tm_vec3_t){ 0, 0, -1 });
    const tm_physx_raycast_t r = tm_physx_scene_api->raycast(args->physx_scene, camera_pos, camera_forward, 2.5f, state->player_collision_type, (tm_physx_raycast_flags_t){ 0 }, 0, 0);

    if (r.has_block) {
        const tm_entity_t hit = r.block.body;
        const tm_component_mask_t* hit_mask = tm_entity_api->component_mask(state->entity_ctx, hit);
        if (tm_entity_mask_has_component(hit_mask, state->interact_comp) && tm_interactable_component_api->can_interact(state->interactable_mgr, hit, true)) {
            crosshair_color = (tm_color_srgb_t){ 255, 255, 255, 255 };
            if (state->input.left_mouse_pressed) {
                // This is what starts the interaction!
                tm_interactable_component_api->interact(state->interactable_mgr, hit);
            }
        }
    }

    // UI: Crosshair
    if (args->ui) {
        tm_ui_buffers_t uib = tm_ui_api->buffers(args->ui);
        tm_vec2_t crosshair_pos = { args->rect.w / 2, args->rect.h / 2 };
        tm_draw2d_style_t style[1] = { 0 };
        tm_ui_api->to_draw_style(args->ui, style, args->uistyle);
        style->color = crosshair_color;
        tm_draw2d_api->fill_circle(uib.vbuffer, uib.ibuffers[TM_UI_BUFFER_MAIN], style, crosshair_pos, 3);
    }
}

static tm_simulation_entry_i simulation_entry_i = {
    .id = TM_STATIC_HASH("Gameplay Interaction System", 0xca35947276977f52ULL),
    .display_name = "Gameplay Interaction System",
    .start = start,
    .stop = stop,
    .tick = tick,
};

extern void load_interactable_component(struct tm_api_registry_api* reg, bool load);

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    tm_application_api = tm_get_api(reg, tm_application_api);
    tm_draw2d_api = tm_get_api(reg, tm_draw2d_api);
    tm_entity_api = tm_get_api(reg, tm_entity_api);
    tm_error_api = tm_get_api(reg, tm_error_api);
    tm_input_api = tm_get_api(reg, tm_input_api);
    tm_physics_collision_api = tm_get_api(reg, tm_physics_collision_api);
    tm_physx_scene_api = tm_get_api(reg, tm_physx_scene_api);
    tm_simulation_api = tm_get_api(reg, tm_simulation_api);
    tm_tag_component_api = tm_get_api(reg, tm_tag_component_api);
    tm_temp_allocator_api = tm_get_api(reg, tm_temp_allocator_api);
    tm_temp_allocator_api = tm_get_api(reg, tm_temp_allocator_api);
    tm_transform_component_api = tm_get_api(reg, tm_transform_component_api);
    tm_ui_api = tm_get_api(reg, tm_ui_api);
    tm_interactable_component_api = tm_get_api(reg, tm_interactable_component_api);
    tm_gamestate_api = tm_get_api(reg, tm_gamestate_api);

    tm_add_or_remove_implementation(reg, load, tm_simulation_entry_i, &simulation_entry_i);
    load_interactable_component(reg, load);
}
