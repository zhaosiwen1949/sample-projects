// This file contains all the gameplay code associated with the first person gameplay sample. It adds an implementation
// of the interface `tm_simulate_entry_i`, which referenced by the asset `main.simulate_entry` in the root of of the
// first person sample project. When simulating `world.entity`, it will look in its folder, or any parent folder, until
// it finds a `.simulate_entry` file, and use the `tm_simulate_entry_i` interface referenced in there, which will make
// the code execution enter this file.

static struct tm_api_registry_api* tm_api_registry_api;
static struct tm_application_api* tm_application_api;
static struct tm_draw2d_api* tm_draw2d_api;
static struct tm_entity_api* tm_entity_api;
static struct tm_error_api* tm_error_api;
static struct tm_input_api* tm_input_api;
static struct tm_link_component_api *tm_link_component_api;
static struct tm_localizer_api* tm_localizer_api;
static struct tm_os_window_api* tm_os_window_api;
static struct tm_physx_scene_api* tm_physx_scene_api;
static struct tm_random_api* tm_random_api;
static struct tm_tag_component_api *tm_tag_component_api;
static struct tm_temp_allocator_api* tm_temp_allocator_api;
static struct tm_the_truth_api* tm_the_truth_api;
static struct tm_the_truth_assets_api* tm_the_truth_assets_api;
static struct tm_ui_api* tm_ui_api;

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/application.h>
#include <foundation/input.h>
#include <foundation/localizer.h>
#include <foundation/error.h>
#include <foundation/macros.h>
#include <foundation/random.h>
#include <foundation/the_truth.h>
#include <foundation/the_truth_assets.h>

#include <plugins/dcc_asset/dcc_asset_component.h>
#include <plugins/entity/entity.h>
#include <plugins/os_window/os_window.h>
#include <plugins/physics/physics_body_component.h>
#include <plugins/physics/physics_joint_component.h>
#include <plugins/physics/physics_shape_component.h>
#include <plugins/physx/physx_scene.h>
#include <plugins/ui/draw2d.h>
#include <plugins/ui/ui.h>
#include <plugins/simulate/simulate_entry.h>
#include <plugins/entity/tag_component.h>
#include <plugins/entity/transform_component.h>
#include <plugins/entity/link_component.h>

#include <foundation/hash.inl>
#include <foundation/carray.inl>
#include <foundation/rect.inl>
#include <foundation/math.inl>

#include <plugins/simulate/simulate_helpers.inl>

#include <stdio.h>

static const uint64_t red_tag = TM_STATIC_HASH("color_red", 0xb56d0d7b72d5e8f2ULL);
static const uint64_t green_tag = TM_STATIC_HASH("color_green", 0x3f94cb7d4091d93bULL);
static const uint64_t blue_tag = TM_STATIC_HASH("color_blue", 0xbe7fd3918560dcddULL);

typedef struct input_state_t {
    bool held_keys[TM_INPUT_KEYBOARD_ITEM_COUNT];
    bool left_mouse_held;
    bool left_mouse_pressed;
    TM_PAD(1);
    tm_vec2_t mouse_delta;
} input_state_t;

enum box_state {
    BOX_STATE_FREE,
    BOX_STATE_CARRIED,
    BOX_STATE_FLYING_UP,
    BOX_STATE_FLYING_BACK
};

struct tm_simulate_state_o {
    tm_allocator_i *allocator;
    tm_entity_context_o *entity_ctx;
    tm_simulate_helpers_context_t h;

    // Contains keyboard and mouse input state.
    input_state_t input;

    // Entities
    tm_entity_t player;
    tm_entity_t player_camera;
    tm_entity_t player_carry_anchor;
    tm_entity_t box;
    tm_vec3_t box_starting_point;
    tm_vec4_t box_starting_rot;

    // How we currently are/may interact with the box
    enum box_state box_state;

    // Used to decide when to move box from BOX_STATE_FLYING_UP to BOX_STATE_FLYING_BACK
    float box_fly_timer;

    // Current camera state
    float look_yaw;
    float look_pitch;

    TM_PAD(4);

    // Misc
    uint64_t processed_events;
    tm_tt_id_t player_collision_type;
    tm_tt_id_t box_collision_type;

    // Component indicies
    uint32_t dcc_asset_component;
    uint32_t mover_component;
    uint32_t physics_shape_component;
    uint32_t physics_joint_component;
    uint32_t physx_rigid_body_component;
    uint32_t physx_joint_component;
    float score;

    bool mouse_captured;
    TM_PAD(3);
};

static void change_box_to_random_color(tm_entity_t box, tm_simulate_state_o* state)
{
    // Chose a random color, but never re-use the current one;
    uint32_t color = UINT32_MAX;
    while (color == UINT32_MAX) {
        const uint32_t c = tm_random_api->next() % 3;
        const uint64_t c_tag = c == 0 ? red_tag : (c == 1 ? green_tag : blue_tag);

        if (!tm_entity_has_tag(box, c_tag, &state->h))
            color = c;
    }

    tm_tt_id_t dcc_asset = (tm_tt_id_t){ 0 };
    uint64_t tag = 0;

    switch (color) {
    case 0:
        dcc_asset = tm_get_asset("red_box.dcc_asset", &state->h);
        tag = red_tag;
        break;
    case 1:
        dcc_asset = tm_get_asset("green_box.dcc_asset", &state->h);
        tag = green_tag;
        break;
    case 2:
        dcc_asset = tm_get_asset("blue_box.dcc_asset", &state->h);
        tag = blue_tag;
        break;
    }

    tm_entity_remove_tag(box, red_tag, &state->h);
    tm_entity_remove_tag(box, green_tag, &state->h);
    tm_entity_remove_tag(box, blue_tag, &state->h);
    tm_entity_add_tag(box, tag, &state->h);

    void* dcc_comp = tm_entity_api->get_component(state->entity_ctx, box, state->dcc_asset_component);
    struct tm_dcc_asset_component_api* tm_dcc_asset_component_api = tm_api_registry_api->get(TM_DCC_ASSET_COMPONENT_API_NAME);
    tm_dcc_asset_component_api->set_component_dcc_asset(dcc_comp, dcc_asset);
}

static tm_simulate_state_o *start(struct tm_allocator_i *allocator, struct tm_entity_context_o *entity_ctx)
{
    tm_simulate_state_o *state = tm_alloc(allocator, sizeof(*state));
    *state = (tm_simulate_state_o) {
        .allocator = allocator,
        .entity_ctx = entity_ctx,
    };

    tm_simulate_helpers_context_t *h = &state->h;
    tm_simulate_helpers_init_context(h, entity_ctx);

    state->dcc_asset_component = tm_entity_api->lookup_component(state->entity_ctx, TM_TT_TYPE_HASH__DCC_ASSET_COMPONENT);
    state->mover_component = tm_entity_api->lookup_component(state->entity_ctx, TM_TT_TYPE_HASH__PHYSX_MOVER_COMPONENT);
    state->physics_joint_component = tm_entity_api->lookup_component(state->entity_ctx, TM_TT_TYPE_HASH__PHYSICS_JOINT_COMPONENT);
    state->physics_shape_component = tm_entity_api->lookup_component(state->entity_ctx, TM_TT_TYPE_HASH__PHYSICS_SHAPE_COMPONENT);
    state->physx_joint_component = tm_entity_api->lookup_component(state->entity_ctx, TM_TT_TYPE_HASH__PHYSX_JOINT_COMPONENT);
    state->physx_rigid_body_component = tm_entity_api->lookup_component(state->entity_ctx, TM_TT_TYPE_HASH__PHYSX_RIGID_BODY_COMPONENT);

    state->player = tm_entity_find_with_tag(TM_STATIC_HASH("player", 0xafff68de8a0598dfULL), h);
    state->player_camera = tm_entity_find_with_tag(TM_STATIC_HASH("player_camera", 0x689cd442a211fda4ULL), h);
    state->player_carry_anchor = tm_entity_find_with_tag(TM_STATIC_HASH("player_carry_anchor", 0xc3ff6c2ebc868f1fULL), h);

    state->box = tm_entity_find_with_tag(TM_STATIC_HASH("box", 0x9eef98b479cef090ULL), h);
    change_box_to_random_color(state->box, state);
    state->box_starting_point = tm_entity_get_position(state->box, h);
    state->box_starting_rot = tm_entity_get_rotation(state->box, h);
    TM_INIT_TEMP_ALLOCATOR_WITH_ADAPTER(ta, a);
    const tm_hash_u64_to_id_t collision_types = tm_physics_get_collison_type_lookup(a, h);
    state->player_collision_type = tm_hash_get_rv(&collision_types, TM_STATIC_HASH("player", 0xafff68de8a0598dfULL));
    state->box_collision_type = tm_hash_get_rv(&collision_types, TM_STATIC_HASH("box", 0x9eef98b479cef090ULL));
    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);

    return state;
}

static void stop(tm_simulate_state_o *state)
{
    tm_allocator_i a = *state->allocator;
    tm_free(&a, state, sizeof(*state));
}

static void update(tm_simulate_state_o *state, tm_simulate_frame_args_t *args)
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
                        if (down && !state->input.left_mouse_held) {
                            if (!args->running_in_editor || (tm_ui_api->is_hovering(args->ui, args->rect, 0))) {
                                state->mouse_captured = true;
                            }
                        }
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
    {
        if ((args->running_in_editor && state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_ESCAPE])/* || !tm_os_window_api->status(ctx->window).has_focus*/) {
            state->mouse_captured = false;
            struct tm_application_o* app = tm_application_api->application();
            tm_application_api->set_cursor_hidden(app, false);
        }

        if (state->mouse_captured) {
            struct tm_application_o* app = tm_application_api->application();
            tm_application_api->set_cursor_hidden(app, true);
        }
    }

    tm_physx_scene_o* physx_scene = args->physx_scene;
    tm_simulate_helpers_context_t *h = &state->h;
    const tm_vec3_t camera_pos = tm_entity_get_position(state->player_camera, h);
    const tm_vec4_t camera_rot = tm_entity_get_rotation(state->player_camera, h);
    struct tm_physx_mover_component_t* player_mover = tm_entity_api->get_component(state->entity_ctx, state->player, state->mover_component);

    // Process input if mouse is captured.
    if (state->mouse_captured) {
        // Exit on ESC
        if (!args->running_in_editor && state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_ESCAPE]) {
            struct tm_application_o* app = tm_application_api->application();
            tm_application_api->exit(app);
        }

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
        tm_entity_set_local_rotation(state->player_camera, tm_quaternion_mul(pitchq, yawq), h);

        // Jump
        if (state->input.held_keys[TM_INPUT_KEYBOARD_ITEM_SPACE] && player_mover->is_standing)
            player_mover->velocity.y = 5;
    }

    // Box carry anchor is kinematic physics body (so we can put joints on it), move it manually
    const tm_vec3_t camera_forward = tm_quaternion_rotate_vec3(camera_rot, (tm_vec3_t){ 0, 0, -1 });
    const tm_vec3_t anchor_pos = tm_vec3_add(tm_vec3_add(camera_pos, tm_vec3_mul(camera_forward, 1.5f)), tm_vec3_mul(player_mover->velocity, args->dt));

    tm_transform_t carry_transf = tm_entity_get_transform(state->player_carry_anchor, h);
    carry_transf.pos = anchor_pos;
    carry_transf.rot = camera_rot;
    tm_entity_set_transform(state->player_carry_anchor, &carry_transf, h);

    // Modified if the raycast below hits the box.
    tm_color_srgb_t crosshair_color = { 120, 120, 120, 255 };

    // Box state machine
    switch (state->box_state) {
    case BOX_STATE_FREE: {
        // Check if box is in a drop zone that has the same color as itself

        bool touching_correct_drop_zone = false;
        tm_physx_on_contact_t* contact_events = tm_physx_scene_api->on_contact(physx_scene);
        for (tm_physx_on_contact_t* t = contact_events; t != tm_carray_end(contact_events); ++t) {
            const tm_entity_t e0 = t->actor_0;
            const tm_entity_t e1 = t->actor_1;

            if (e0.u64 != state->box.u64 && e1.u64 != state->box.u64)
                continue;

            const bool correct_floor = (tm_entity_has_tag(e0, red_tag, h) && tm_entity_has_tag(e1, red_tag, h))
                || (tm_entity_has_tag(e0, green_tag, h) && tm_entity_has_tag(e1, green_tag, h))
                || (tm_entity_has_tag(e0, blue_tag, h) && tm_entity_has_tag(e1, blue_tag, h));

            if (!correct_floor)
                continue;

            touching_correct_drop_zone = true;
        }

        if (touching_correct_drop_zone) {
            // If box is in correct drop zone and has low velocity, send it flying upwards.

            const tm_vec3_t box_velocity = tm_physx_scene_api->velocity(physx_scene, state->box);

            if (tm_vec3_length(box_velocity) < 0.01) {
                tm_physx_scene_api->add_force(physx_scene, state->box, (tm_vec3_t){ 0, 10, 0 }, TM_PHYSX_FORCE_FLAGS__VELOCITY_CHANGE);
                state->box_fly_timer = 0.7f;
                state->box_state = BOX_STATE_FLYING_UP;
            }
        } else {
            // If box is not in correct drop zone and player clicks left mouse button, try picking it up using raycast.

            const tm_physx_raycast_t r = tm_physx_scene_api->raycast(physx_scene, camera_pos, camera_forward, 2.5f, state->player_collision_type, (tm_physx_raycast_flags_t){ 0 }, 0, 0);

            if (r.has_block) {
                const tm_entity_t hit = r.block.body;

                if (state->box.u64 == hit.u64) {
                    crosshair_color = (tm_color_srgb_t){ 255, 255, 255, 255 };
                    if (state->input.left_mouse_pressed) {
                        tm_physics_shape_component_t* shape = tm_entity_api->get_component(state->entity_ctx, state->box, state->physics_shape_component);
                        shape->collision_id = state->player_collision_type;

                        // Forces re-mirroring of physx rigid body, so the physx shape gets correct collision type.
                        tm_entity_api->remove_component(state->entity_ctx, state->box, state->physx_rigid_body_component);

                        tm_entity_set_position(state->box, anchor_pos, h);
                        tm_physics_joint_component_t* j = tm_entity_api->add_component(state->entity_ctx, state->box, state->physics_joint_component);
                        j->joint_type = TM_PHYSICS_JOINT__FIXED;
                        j->body_0 = state->box;
                        j->body_1 = state->player_carry_anchor;
                        state->box_state = BOX_STATE_CARRIED;
                    }
                }
            }
        }
    } break;

    case BOX_STATE_CARRIED: {
        if (state->input.left_mouse_pressed) {
            // Drop box

            tm_entity_api->remove_component(state->entity_ctx, state->box, state->physics_joint_component);
            tm_entity_api->remove_component(state->entity_ctx, state->box, state->physx_joint_component);
            tm_physics_shape_component_t* shape = tm_entity_api->get_component(state->entity_ctx, state->box, state->physics_shape_component);
            shape->collision_id = state->box_collision_type;

            // Forces re-mirroring of physx rigid body, so the physx shape gets correct collision type.
            tm_entity_api->remove_component(state->entity_ctx, state->box, state->physx_rigid_body_component);

            tm_physx_scene_api->set_kinematic(physx_scene, state->box, false);
            tm_physx_scene_api->add_force(physx_scene, state->box, tm_vec3_mul(camera_forward, 1500 * args->dt), TM_PHYSX_FORCE_FLAGS__IMPULSE);
            state->box_state = BOX_STATE_FREE;
        }
    } break;

    case BOX_STATE_FLYING_UP: {
        state->box_fly_timer -= args->dt;

        if (state->box_fly_timer <= 0.0001f) {
            tm_physx_scene_api->set_kinematic(physx_scene, state->box, true);
            state->box_state = BOX_STATE_FLYING_BACK;
        }
    } break;

    case BOX_STATE_FLYING_BACK: {
        // This state interpolates the box back to its initial position and changes the
        // color once it reaches it.

        const tm_vec3_t box_pos = tm_entity_get_position(state->box, h);
        const tm_vec3_t box_to_spawn = tm_vec3_sub(state->box_starting_point, box_pos);
        const tm_vec3_t spawn_point_dir = tm_vec3_normalize(box_to_spawn);

        if (tm_vec3_length(box_to_spawn) < 0.1f) {
            tm_entity_set_position(state->box, state->box_starting_point, h);
            tm_physx_scene_api->set_kinematic(physx_scene, state->box, false);
            tm_physx_scene_api->set_velocity(physx_scene, state->box, (tm_vec3_t){ 0, 0, 0 });
            change_box_to_random_color(state->box, state);
            state->box_state = BOX_STATE_FREE;
            state->score += 1.0f;
        } else {
            const tm_vec3_t interpolate_to_start_pos = tm_vec3_add(box_pos, tm_vec3_mul(spawn_point_dir, args->dt * 10));
            tm_entity_set_position(state->box, interpolate_to_start_pos, h);
        }
    } break;
    }

    // UI: Score
    char label_text[128];
    snprintf(label_text, 128, "The box has been correctly placed %.0f times", state->score);
    tm_rect_t rect = { 5, 5, 20, 20 };
    tm_ui_api->label(args->ui, args->uistyle, &(tm_ui_label_t){ .rect = rect, .text = label_text });

    // UI: Crosshair
    tm_ui_buffers_t uib = tm_ui_api->buffers(args->ui);
    tm_vec2_t crosshair_pos = { args->rect.w / 2, args->rect.h / 2 };
    tm_draw2d_style_t style[1] = { 0 };
    tm_ui_api->to_draw_style(args->ui, style, args->uistyle);
    style->color = crosshair_color;
    tm_draw2d_api->fill_circle(uib.vbuffer, uib.ibuffers[TM_UI_BUFFER_MAIN], style, crosshair_pos, 3);
}

static tm_simulate_entry_i simulate_entry_i = {
    .id = TM_STATIC_HASH("tm_gameplay_sample_first_person_simulate_entry_i", 0x5661a6a1bf704391ULL),
    .display_name = TM_LOCALIZE_LATER("Gameplay Sample First Person"),
    .start = start,
    .stop = stop,
    .update = update,
};

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    tm_api_registry_api = reg;
    tm_application_api = reg->get(TM_APPLICATION_API_NAME);
    tm_draw2d_api = reg->get(TM_DRAW2D_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_error_api = reg->get(TM_ERROR_API_NAME);
    tm_input_api = reg->get(TM_INPUT_API_NAME);
    tm_link_component_api = reg->get(TM_LINK_COMPONENT_API_NAME);
    tm_localizer_api = reg->get(TM_LOCALIZER_API_NAME);
    tm_os_window_api = reg->get(TM_OS_WINDOW_API_NAME);
    tm_physx_scene_api = reg->get(TM_PHYSX_SCENE_API_NAME);
    tm_random_api = reg->get(TM_RANDOM_API_NAME);
    tm_tag_component_api = reg->get(TM_TAG_COMPONENT_API_NAME);
    tm_temp_allocator_api = reg->get(TM_TEMP_ALLOCATOR_API_NAME);
    tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);
    tm_the_truth_assets_api = reg->get(TM_THE_TRUTH_ASSETS_API_NAME);
    tm_ui_api = reg->get(TM_UI_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_SIMULATE_ENTRY_INTERFACE_NAME, &simulate_entry_i);
}