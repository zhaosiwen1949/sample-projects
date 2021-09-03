// This file contains all the gameplay code associated with the third person gameplay sample. It adds an implementation
// of the interface `tm_simulation_entry_i`, which is referenced by the asset `levels/main.simulate_entry` in the third
// person sample project. When simulating `levels/world.entity`, it will look in its folder, or any parent folder, until
// it finds a `.simulate_entry` file. It will use the `tm_simulation_entry_i` interface referenced in there in order to
// enter this file.

static struct tm_animation_state_machine_api* tm_animation_state_machine_api;
static struct tm_api_registry_api* tm_global_api_registry;
static struct tm_application_api* tm_application_api;
static struct tm_entity_api* tm_entity_api;
static struct tm_error_api* tm_error_api;
static struct tm_input_api* tm_input_api;
static struct tm_localizer_api* tm_localizer_api;
static struct tm_render_component_api* tm_render_component_api;
static struct tm_transform_component_api* tm_transform_component_api;
static struct tm_shader_api* tm_shader_api;
static struct tm_simulation_api* tm_simulation_api;
static struct tm_ui_api* tm_ui_api;
static struct tm_tag_component_api* tm_tag_component_api;
static struct tm_the_truth_assets_api* tm_the_truth_assets_api;
static struct tm_gamestate_api* tm_gamestate_api;

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/application.h>
#include <foundation/error.h>
#include <foundation/input.h>
#include <foundation/localizer.h>
#include <foundation/murmurhash64a.inl>
#include <foundation/the_truth.h>
#include <foundation/the_truth_assets.h>

#include <plugins/animation/animation_state_machine.h>
#include <plugins/animation/animation_state_machine_component.h>
#include <plugins/creation_graph/creation_graph.h>
#include <plugins/entity/entity.h>
#include <plugins/entity/tag_component.h>
#include <plugins/entity/transform_component.h>
#include <plugins/gamestate/gamestate.h>
#include <plugins/physx/physx_scene.h>
#include <plugins/render_utilities/render_component.h>
#include <plugins/renderer/commands.h>
#include <plugins/renderer/render_backend.h>
#include <plugins/renderer/render_command_buffer.h>
#include <plugins/renderer/resources.h>
#include <plugins/shader_system/shader_system.h>
#include <plugins/simulation/simulation.h>
#include <plugins/simulation/simulation_entry.h>
#include <plugins/ui/ui.h>

#include <foundation/math.inl>
#include <plugins/creation_graph/creation_graph_output.inl>

#include <stddef.h>
#include <stdio.h>

typedef struct input_state_t {
    tm_vec2_t mouse_delta;
    bool held_keys[TM_INPUT_KEYBOARD_ITEM_COUNT];
    bool left_mouse_held;
    bool left_mouse_pressed;
    TM_PAD(1);
} input_state_t;

struct tm_simulation_state_o {
    tm_allocator_i* allocator;

    // For interacing with `tm_the_truth_api`.
    tm_the_truth_o* tt;

    // For interfacing with `tm_entity_api`.
    tm_entity_context_o* entity_ctx;

    // For interfacing with `tm_simulation_api`.
    tm_simulation_o* simulation_ctx;

    // For interfacing with many functions in `tm_the_truth_assets_api`.
    tm_tt_id_t asset_root;

    // Contains keyboard and mouse input state.
    input_state_t input;

    tm_entity_t player;
    tm_entity_t player_camera_pivot;
    tm_entity_t checkpoint_sphere;
    tm_vec3_t checkpoints_positions[8];
    uint64_t processed_events;

    tm_tt_id_t particle_entity;
    tm_renderer_backend_i* rb;

    uint32_t current_checkpoint;
    float camera_tilt;

    // Current score
    float score;
    TM_PAD(4);

    // For giving some extra time to press jump.
    double last_standing_time;

    // Component types
    tm_component_type_t asm_component;
    tm_component_type_t mover_component;
    tm_component_type_t render_component;
    tm_component_type_t tag_component;
    tm_component_type_t transform_component;
    TM_PAD(4);

    // Component managers
    tm_transform_component_manager_o* trans_mgr;
    tm_tag_component_manager_o* tag_mgr;

    bool mouse_captured;
    TM_PAD(7);

    tm_gamestate_struct_id_t persistent_state_id;
};

typedef struct simulate_persistent_state {
    uint32_t current_checkpoint;
    float camera_tilt;
    float score;
    TM_PAD(4);
    double last_standing_time;
} simulate_persistent_state;

static void serialize(tm_simulation_state_o* source, simulate_persistent_state* dest)
{
    dest->current_checkpoint = source->current_checkpoint;
    dest->camera_tilt = source->camera_tilt;
    dest->score = source->score;
    dest->last_standing_time = source->last_standing_time;
}

static tm_entity_t find_root_entity(tm_entity_context_o* entity_ctx, tm_entity_t e)
{
    tm_entity_t p = e;
    while (true) {
        tm_entity_t par = tm_entity_api->parent(entity_ctx, p);

        if (!par.u64)
            break;

        p = par;
    }
    return p;
}

static void private__load_game(tm_simulation_state_o* state)
{
    state->player = tm_tag_component_api->find_first(state->tag_mgr, TM_STATIC_HASH("player", 0xafff68de8a0598dfULL));

    state->player_camera_pivot = tm_tag_component_api->find_first(state->tag_mgr, TM_STATIC_HASH("camera_pivot", 0x37610e33774a5b13ULL));
    state->checkpoint_sphere = tm_tag_component_api->find_first(state->tag_mgr, TM_STATIC_HASH("checkpoint", 0x76169e4aa68e805dULL));
    state->camera_tilt = 3.18f;
    state->particle_entity = tm_the_truth_assets_api->asset_object_from_path(state->tt, state->asset_root, "vfx/particles.entity");

    const tm_entity_t camera = tm_tag_component_api->find_first(state->tag_mgr, TM_STATIC_HASH("camera", 0x60ed8c3931822dc7ULL));
    tm_simulation_api->set_camera(state->simulation_ctx, camera);

    const tm_entity_t root_entity = find_root_entity(state->entity_ctx, state->player);
    char checkpoint_path[30];
    for (uint32_t i = 0; i < 8; ++i) {
        snprintf(checkpoint_path, 30, "Checkpoints/checkpoint-%u", (i + 1));
        const tm_entity_t c = tm_entity_api->resolve_path(state->entity_ctx, root_entity, checkpoint_path);

        if (!TM_ASSERT(tm_entity_api->is_alive(state->entity_ctx, c), tm_error_api->def, "Failed to find checkpoint entity"))
            continue;

        state->checkpoints_positions[i] = tm_get_position(state->trans_mgr, c);
    }
}

static void deserialize(tm_simulation_state_o* dest, simulate_persistent_state* source)
{
    dest->current_checkpoint = source->current_checkpoint;
    dest->camera_tilt = source->camera_tilt;
    dest->score = source->score;
    dest->last_standing_time = source->last_standing_time;
}

void private__state_loaded_from_gamestate(struct tm_gamestate_o* gamestate, void* user_data, tm_gamestate_struct_id_t s, void* data, uint32_t data_size)
{
    tm_simulation_state_o* state = user_data;
    private__load_game(state);
    deserialize(state, data);
    state->persistent_state_id = s;
}

static tm_simulation_state_o* start(tm_simulation_start_args_t* args)
{
    tm_simulation_state_o* state = tm_alloc(args->allocator, sizeof(*state));
    *state = (tm_simulation_state_o){
        .allocator = args->allocator,
        .tt = args->tt,
        .entity_ctx = args->entity_ctx,
        .simulation_ctx = args->simulation_ctx,
        .asset_root = args->asset_root,
    };

    state->mover_component = tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__PHYSX_MOVER_COMPONENT);
    state->asm_component = tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__ANIMATION_STATE_MACHINE_COMPONENT);
    state->render_component = tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__RENDER_COMPONENT);
    state->tag_component = tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__TAG_COMPONENT);
    state->transform_component = tm_entity_api->lookup_component_type(state->entity_ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);

    state->trans_mgr = (tm_transform_component_manager_o*)tm_entity_api->component_manager(state->entity_ctx, state->transform_component);
    state->tag_mgr = (tm_tag_component_manager_o*)tm_entity_api->component_manager(state->entity_ctx, state->tag_component);

    private__load_game(state);

    const char* name = "simulation_state";
    tm_strhash_t name_hash = tm_murmur_hash_string(name);

    tm_gamestate_struct_t s = {
        .name = name,
        .user_data = state,
        .size = sizeof(simulate_persistent_state),
        .created = private__state_loaded_from_gamestate,
    };

    tm_gamestate_o* gamestate = tm_entity_api->gamestate(state->entity_ctx);

    tm_gamestate_member_t score_member = { .name = "score", .type = TM_GAMESTATE_MEMBER_TYPE__FLOAT, .offset = offsetof(simulate_persistent_state, score) };
    tm_gamestate_api->add_struct_type(gamestate, s);
    tm_gamestate_api->configure_struct_global_members(gamestate, name_hash, &score_member, 1);

    simulate_persistent_state dest = { 0 };
    serialize(state, &dest);
    state->persistent_state_id = tm_gamestate_api->create_struct(gamestate, tm_gamestate_api->reserve_object_id(gamestate), name_hash, &dest, sizeof(simulate_persistent_state));

    state->rb = (tm_renderer_backend_i*)(*tm_global_api_registry->implementations(TM_RENDER_BACKEND_INTERFACE_NAME));
    return state;
}

static void stop(tm_simulation_state_o* state)
{
    tm_allocator_i a = *state->allocator;
    tm_free(&a, state, sizeof(*state));
}

static void private__set_shader_constant(tm_shader_io_o* io, tm_renderer_resource_command_buffer_o* res_buf, const tm_shader_constant_buffer_instance_t* instance, tm_strhash_t name, const void* data, uint32_t data_size)
{
    tm_shader_constant_t constant;
    uint32_t constant_offset;

    if (tm_shader_api->lookup_constant(io, name, &constant, &constant_offset))
        tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = instance->instance_id, .constant_offset = constant_offset, .num_bytes = data_size, .data = data }, 1);
}

static void private__adjust_effect_start_color(tm_simulation_state_o* state, tm_entity_t p, tm_vec3_t color)
{
    // Show how to poke at a custom shader variable (`start_color`) exposed in a creation graph bound to a specific draw call (`vfx`)
    tm_render_component_public_t* rc = tm_entity_api->get_component(state->entity_ctx, p, state->render_component);
    const tm_creation_graph_draw_call_data_t* draw = tm_render_component_api->draw_call(rc, TM_STATIC_HASH("vfx", 0xfc741b5732202063ULL));

    if (draw && draw->shader) {
        tm_renderer_resource_command_buffer_o* res_buf;
        state->rb->create_resource_command_buffers(state->rb->inst, &res_buf, 1);

        private__set_shader_constant(tm_shader_api->shader_io(draw->shader), res_buf, &draw->cbuffer, TM_STATIC_HASH("start_color", 0x78037e459ae53b07ULL), &color, sizeof(color));

        state->rb->submit_resource_command_buffers(state->rb->inst, &res_buf, 1);
        state->rb->destroy_resource_command_buffers(state->rb->inst, &res_buf, 1);
    }
}

static void ui(tm_simulation_state_o* state, tm_simulation_ui_args_t* args)
{
    // Capture mouse
    {
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

    // Rendering
    char label_text[30];
    snprintf(label_text, 30, "You reached: %.0f checkpoints", state->score);

    tm_rect_t rect = { 5, 5, 20, 20 };
    tm_ui_api->label(args->ui, args->uistyle, &(tm_ui_label_t){ .rect = rect, .text = label_text });
}

static void tick(tm_simulation_state_o* state, tm_simulation_frame_args_t* args)
{
    // Reset per-frame input
    state->input.mouse_delta.x = state->input.mouse_delta.y = 0;
    state->input.left_mouse_pressed = false;

    // Read input
    tm_input_event_t events[32];
    while (true) {
        uint64_t n = tm_input_api->events(state->processed_events, events, 32);
        for (uint64_t i = 0; i < n; ++i) {
            const tm_input_event_t* e = events + i;
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
        }
        state->processed_events += n;
        if (n < 32)
            break;
    }

    struct tm_physx_mover_component_t* player_mover = tm_entity_api->get_component(state->entity_ctx, state->player, state->mover_component);

    if (!TM_ASSERT(player_mover, tm_error_api->def, "Invalid player"))
        return;

    // For fudging jump timing
    if (player_mover->is_standing)
        state->last_standing_time = args->time;

    // Only allow input when mouse is captured.
    if (state->mouse_captured) {
        // Exit on ESC
        if (!args->running_in_editor && state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_ESCAPE]) {
            struct tm_application_o* app = tm_application_api->application();
            tm_application_api->exit(app);
        }

        // Camera pan control
        const float mouse_sens = 0.5f * args->dt;
        const float camera_pan_delta = -state->input.mouse_delta.x * mouse_sens;
        const tm_vec4_t pan_rot = tm_quaternion_from_rotation((tm_vec3_t){ 0, 1, 0 }, camera_pan_delta);
        tm_set_rotation(state->trans_mgr, state->player, tm_quaternion_mul(pan_rot, tm_get_rotation(state->trans_mgr, state->player)));

        // Camera tilt control
        const float camera_tilt_delta = -state->input.mouse_delta.y * mouse_sens;
        state->camera_tilt += camera_tilt_delta;
        state->camera_tilt = tm_clamp(state->camera_tilt, 1.5f, 3.8f);
        const tm_vec4_t camera_pivot_rot = tm_quaternion_from_euler((tm_vec3_t){ state->camera_tilt, 0, -TM_PI });
        tm_set_local_rotation(state->trans_mgr, state->player_camera_pivot, camera_pivot_rot);

        // Control animation state machine using input
        tm_animation_state_machine_component_t* smc = tm_entity_api->get_component(state->entity_ctx, state->player, state->asm_component);
        tm_animation_state_machine_o* sm = smc->state_machine;
        tm_animation_state_machine_api->set_variable(sm, TM_STATIC_HASH("w", 0x22727cb14c3bb41dULL), (float)state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_W]);
        tm_animation_state_machine_api->set_variable(sm, TM_STATIC_HASH("a", 0x71717d2d36b6b11ULL), (float)state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_A]);
        tm_animation_state_machine_api->set_variable(sm, TM_STATIC_HASH("s", 0xe5db19474a903141ULL), (float)state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_S]);
        tm_animation_state_machine_api->set_variable(sm, TM_STATIC_HASH("d", 0x17dffbc5a8f17839ULL), (float)state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_D]);
        tm_animation_state_machine_api->set_variable(sm, TM_STATIC_HASH("run", 0xb8961af5ed6912f5ULL), (float)state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_LEFTSHIFT]);

        const bool can_jump = args->time < state->last_standing_time + 0.2f;
        if (can_jump && state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_SPACE]) {
            tm_animation_state_machine_api->event(sm, TM_STATIC_HASH("jump", 0x7b98bf53d1dceae8ULL));
            player_mover->velocity.y += 6;
            state->last_standing_time = 0;
        }
    }

    // Check player against checkpoint
    const tm_vec3_t sphere_pos = tm_get_position(state->trans_mgr, state->checkpoint_sphere);
    const tm_vec3_t player_pos = tm_get_position(state->trans_mgr, state->player);

    if (tm_vec3_length(tm_vec3_sub(sphere_pos, player_pos)) < 1.5f) {
        ++state->current_checkpoint;
        state->score += 1.0f;

        if (state->current_checkpoint == 8)
            state->current_checkpoint = 0;

        if (state->particle_entity.u64) {
            // Spawn particle effect at position of next checkpoint.
            tm_entity_t p = tm_entity_api->create_entity_from_asset(state->entity_ctx, state->particle_entity);
            // Set particle spawn location to next check point.
            tm_set_position(state->trans_mgr, p, state->checkpoints_positions[state->current_checkpoint]);

            // Make up an arbitrary color based on the direction the player entered the last check point.
            tm_vec3_t color = tm_vec3_normalize(tm_vec3_sub(sphere_pos, player_pos));
            color = (tm_vec3_t){ fabsf(color.x), fabsf(color.y), fabsf(color.z) };
            private__adjust_effect_start_color(state, p, color);
        }

        tm_set_position(state->trans_mgr, state->checkpoint_sphere, state->checkpoints_positions[state->current_checkpoint]);
    }

    // Save Persistent State.
    simulate_persistent_state persistent = { 0 };
    serialize(state, &persistent);
    tm_gamestate_api->set_struct_raw(tm_entity_api->gamestate(state->entity_ctx), state->persistent_state_id, &persistent, sizeof(simulate_persistent_state));
}

static tm_simulation_entry_i simulation_entry_i = {
    .id = TM_STATIC_HASH("tm_gameplay_sample_third_person_simulation_entry_i", 0xcbe37997706b78d5ULL),
    .display_name = TM_LOCALIZE_LATER("Gameplay Sample Third Person"),
    .start = start,
    .stop = stop,
    .tick = tick,
    .ui = ui,
};

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    tm_global_api_registry = reg;
    tm_animation_state_machine_api = tm_get_api(reg, tm_animation_state_machine_api);
    tm_application_api = tm_get_api(reg, tm_application_api);
    tm_entity_api = tm_get_api(reg, tm_entity_api);
    tm_error_api = tm_get_api(reg, tm_error_api);
    tm_input_api = tm_get_api(reg, tm_input_api);
    tm_localizer_api = tm_get_api(reg, tm_localizer_api);
    tm_render_component_api = tm_get_api(reg, tm_render_component_api);
    tm_shader_api = tm_get_api(reg, tm_shader_api);
    tm_simulation_api = tm_get_api(reg, tm_simulation_api);
    tm_ui_api = tm_get_api(reg, tm_ui_api);
    tm_tag_component_api = tm_get_api(reg, tm_tag_component_api);
    tm_the_truth_assets_api = tm_get_api(reg, tm_the_truth_assets_api);
    tm_transform_component_api = tm_get_api(reg, tm_transform_component_api);
    tm_gamestate_api = tm_get_api(reg, tm_gamestate_api);

    tm_add_or_remove_implementation(reg, load, TM_SIMULATION_ENTRY_INTERFACE_NAME, &simulation_entry_i);
}