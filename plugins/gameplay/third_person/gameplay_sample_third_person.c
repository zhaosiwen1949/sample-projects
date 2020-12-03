// This file contains all the gameplay code associated with the third person gameplay sample. It adds an implementation
// of the interface `tm_simulate_entry_i`, which is referenced by the asset `main.simulate_entry` in the root of of the
// first person sample project. When simulating `world.entity`, it will look in its folder, or any parent folder, until
// it finds a `.simulate_entry` file. It will use the `tm_simulate_entry_i` interface referenced in there in order to
// enter this file.

static struct tm_animation_state_machine_api* tm_animation_state_machine_api;
static struct tm_api_registry_api* tm_global_api_registry;
static struct tm_application_api* tm_application_api;
static struct tm_entity_api* tm_entity_api;
static struct tm_error_api* tm_error_api;
static struct tm_input_api* tm_input_api;
static struct tm_localizer_api* tm_localizer_api;
static struct tm_render_component_api* tm_render_component_api;
static struct tm_shader_api* tm_shader_api;
static struct tm_simulate_context_api* tm_simulate_context_api;
static struct tm_ui_api* tm_ui_api;

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/application.h>
#include <foundation/input.h>
#include <foundation/localizer.h>
#include <foundation/the_truth.h>
#include <foundation/the_truth_assets.h>

#include <plugins/animation/animation_state_machine.h>
#include <plugins/animation/animation_state_machine_component.h>
#include <plugins/creation_graph/creation_graph.h>
#include <plugins/entity/entity.h>
#include <plugins/physx/physx_scene.h>
#include <plugins/render_utilities/render_component.h>
#include <plugins/renderer/commands.h>
#include <plugins/renderer/render_backend.h>
#include <plugins/renderer/render_command_buffer.h>
#include <plugins/renderer/resources.h>
#include <plugins/shader_system/shader_system.h>
#include <plugins/ui/ui.h>
#include <plugins/simulate/simulate_entry.h>
#include <plugins/simulate_common/simulate_context.h>

#include <plugins/creation_graph/creation_graph_output.inl>
#include <foundation/math.inl>
#include <plugins/simulate/simulate_helpers.inl>

#include <stdio.h>

typedef struct input_state_t {
    tm_vec2_t mouse_delta;
    bool held_keys[TM_INPUT_KEYBOARD_ITEM_COUNT];
    bool left_mouse_held;
    bool left_mouse_pressed;
    TM_PAD(1);
} input_state_t;

struct tm_simulate_state_o {
    tm_allocator_i *allocator;

    // For interfacing with `tm_entity_api`.
    tm_entity_context_o *entity_ctx;

    // For interfacing with `tm_simulate_context_api`.
    tm_simulate_context_o *simulate_ctx;

    // For interfacing with functions in `simulate_helpers.inl`
    tm_simulate_helpers_context_t h;

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

    // Component indices
    uint32_t asm_component;
    uint32_t mover_component;
    uint32_t render_component;
    uint32_t score_component;

    bool mouse_captured;
    TM_PAD(7);
};

static tm_entity_t find_root_entity(tm_entity_context_o *entity_ctx, tm_entity_t e)
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

static tm_simulate_state_o *start(struct tm_allocator_i *allocator, struct tm_entity_context_o *entity_ctx,
    struct tm_simulate_context_o *simulate_ctx)
{
    tm_simulate_state_o *state = tm_alloc(allocator, sizeof(*state));
    *state = (tm_simulate_state_o) {
        .allocator = allocator,
        .entity_ctx = entity_ctx,
        .simulate_ctx = simulate_ctx,
    };

    tm_simulate_helpers_context_t *h = &state->h;
    tm_simulate_helpers_init_context(tm_global_api_registry, h, entity_ctx);

    state->mover_component = tm_entity_api->lookup_component(entity_ctx, TM_TT_TYPE_HASH__PHYSX_MOVER_COMPONENT);
    state->asm_component = tm_entity_api->lookup_component(entity_ctx, TM_TT_TYPE_HASH__ANIMATION_STATE_MACHINE_COMPONENT);
    state->render_component = tm_entity_api->lookup_component(entity_ctx, TM_TT_TYPE_HASH__RENDER_COMPONENT);

    state->player = tm_entity_find_with_tag(TM_STATIC_HASH("player", 0xafff68de8a0598dfULL), h);

    state->player_camera_pivot = tm_entity_find_with_tag(TM_STATIC_HASH("camera_pivot", 0x37610e33774a5b13ULL), h);
    state->checkpoint_sphere = tm_entity_find_with_tag(TM_STATIC_HASH("checkpoint", 0x76169e4aa68e805dULL), h);
    state->camera_tilt = 3.18f;
    state->particle_entity = tm_get_asset("vfx/particles.entity", h);

    const tm_entity_t camera = tm_entity_find_with_tag(TM_STATIC_HASH("camera", 0x60ed8c3931822dc7ULL), h);
    tm_simulate_context_api->set_camera(simulate_ctx, camera);

    const tm_entity_t root_entity = find_root_entity(entity_ctx, state->player);
    char checkpoint_path[30];
    for (uint32_t i = 0; i < 8; ++i) {
        snprintf(checkpoint_path, 30, "Checkpoints/checkpoint-%u", (i + 1));
        const tm_entity_t c = tm_entity_api->resolve_path(entity_ctx, root_entity, checkpoint_path);

        if (!TM_ASSERT(tm_entity_api->is_alive(entity_ctx, c), tm_error_api->def, "Failed to find checkpoint entity"))
            continue;

        state->checkpoints_positions[i] = tm_entity_get_position(c, h);
    }

    uint32_t num_backends;
    state->rb = (tm_renderer_backend_i*)(*tm_global_api_registry->implementations(TM_RENDER_BACKEND_INTERFACE_NAME, &num_backends));
    return state;
}

static void stop(tm_simulate_state_o *state)
{
    tm_allocator_i a = *state->allocator;
    tm_free(&a, state, sizeof(*state));
}

static void private__set_shader_constant(tm_shader_io_o* io, tm_renderer_resource_command_buffer_o* res_buf, const tm_shader_constant_buffer_instance_t* instance, uint64_t name, const void* data, uint32_t data_size)
{
    tm_shader_constant_t constant;
    uint32_t constant_offset;

    if (tm_shader_api->lookup_constant(io, name, &constant, &constant_offset))
        tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = instance->instance_id, .constant_offset = constant_offset, .num_bytes = data_size, .data = data }, 1);
}

static void private__adjust_effect_start_color(tm_simulate_state_o *state, tm_entity_t p, tm_vec3_t color)
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

static void update(tm_simulate_state_o *state, tm_simulate_frame_args_t *args)
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

    struct tm_physx_mover_component_t* player_mover = tm_entity_api->get_component(state->entity_ctx, state->player, state->mover_component);

    if (!TM_ASSERT(player_mover, tm_error_api->def, "Invalid player"))
        return;

    // For fudging jump timing
    if (player_mover->is_standing)
        state->last_standing_time = args->time;

    // For using functions from `simulate_helpers.inl`
    tm_simulate_helpers_context_t *h = &state->h;

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
        const tm_vec4_t player_rot = tm_entity_get_rotation(state->player, h);
        tm_entity_set_rotation(state->player, tm_quaternion_mul(pan_rot, player_rot), h);

        // Camera tilt control
        const float camera_tilt_delta = -state->input.mouse_delta.y * mouse_sens;
        state->camera_tilt += camera_tilt_delta;
        state->camera_tilt = tm_clamp(state->camera_tilt, 1.5f, 3.8f);
        const tm_vec4_t camera_pivot_rot = tm_euler_to_quaternion((tm_vec3_t){ state->camera_tilt, 0, -TM_PI });
        tm_entity_set_local_rotation(state->player_camera_pivot, camera_pivot_rot, h);

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
    const tm_vec3_t sphere_pos = tm_entity_get_position(state->checkpoint_sphere, h);
    const tm_vec3_t player_pos = tm_entity_get_position(state->player, h);

    if (tm_vec3_length(tm_vec3_sub(sphere_pos, player_pos)) < 1.5f) {
        ++state->current_checkpoint;
        state->score += 1.0f;

        if (state->current_checkpoint == 8)
            state->current_checkpoint = 0;

        if (state->particle_entity.u64) {
            // Spawn particle effect at position of next checkpoint.
            tm_entity_t p = tm_entity_api->create_entity_from_asset(state->entity_ctx, state->particle_entity);
            // Set particle spawn location to next check point.
            tm_entity_set_position(p, state->checkpoints_positions[state->current_checkpoint], h);

            // Make up an arbitrary color based on the direction the player entered the last check point.
            tm_vec3_t color = tm_vec3_normalize(tm_vec3_sub(sphere_pos, player_pos));
            color = (tm_vec3_t){ fabsf(color.x), fabsf(color.y), fabsf(color.z) };
            private__adjust_effect_start_color(state, p, color);
        }

        tm_entity_set_position(state->checkpoint_sphere, state->checkpoints_positions[state->current_checkpoint], h);
    }

    // Rendering
    char label_text[30];
    snprintf(label_text, 30, "You reached: %.0f checkpoints", state->score);

    tm_rect_t rect = { 5, 5, 20, 20 };
    tm_ui_api->label(args->ui, args->uistyle, &(tm_ui_label_t){ .rect = rect, .text = label_text });
}

static void hot_reload(tm_simulate_state_o *state)
{
    // This reinit updates the APIs cached inside the context.
    tm_simulate_helpers_init_context(tm_global_api_registry, &state->h, state->entity_ctx);
}

static tm_simulate_entry_i simulate_entry_i = {
    .id = TM_STATIC_HASH("tm_gameplay_sample_third_person_simulate_entry_i", 0xacfa6f07020cdff9ULL),
    .display_name = TM_LOCALIZE_LATER("Gameplay Sample Third Person"),
    .start = start,
    .stop = stop,
    .update = update,
    .hot_reload = hot_reload,
};

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    tm_global_api_registry = reg;
    tm_animation_state_machine_api = reg->get(TM_ANIMATION_STATE_MACHINE_API_NAME);
    tm_application_api = reg->get(TM_APPLICATION_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_error_api = reg->get(TM_ERROR_API_NAME);
    tm_input_api = reg->get(TM_INPUT_API_NAME);
    tm_localizer_api = reg->get(TM_LOCALIZER_API_NAME);
    tm_render_component_api = reg->get(TM_RENDER_COMPONENT_API_NAME);
    tm_shader_api = reg->get(TM_SHADER_API_NAME);
    tm_simulate_context_api = reg->get(TM_SIMULATE_CONTEXT_API_NAME);
    tm_ui_api = reg->get(TM_UI_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_SIMULATE_ENTRY_INTERFACE_NAME, &simulate_entry_i);
}