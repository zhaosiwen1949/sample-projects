// This is a skeleton for writing gameplay code in C. This file introduces the component 'Gameplay
// Sample Empty', which when added to an entity will set-up a system that runs once a frame.
// `tm_gameplay_context_t` is passed to start and update, it is used for interfacing with the apis
// defined in `gameplay.h`.

#include <plugins/gameplay/gameplay.h>

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/localizer.h>
#include <foundation/the_truth.h>
#include <plugins/entity/entity.h>
#include <the_machinery/component_interfaces/editor_ui_interface.h>

// short-hand for tm_gameplay_api
static struct tm_gameplay_api* g;
static struct tm_entity_api* tm_entity_api;
static struct tm_the_truth_api* tm_the_truth_api;

typedef struct tm_gameplay_state_o {
    uint32_t some_state;
} tm_gameplay_state_o;

static void start(tm_gameplay_context_t* ctx)
{
    tm_gameplay_state_o* state = ctx->state;
    state->some_state = 7;
}

static void update(tm_gameplay_context_t* ctx)
{
    tm_gameplay_state_o* state = ctx->state;
    ++state->some_state;
}

// Remainder of file is component set-up.

#define TYPE__GAMEPLAY_SAMPLE_EMPTY_COMPONENT "gameplay_sample_empty_component"
#define TYPE_HASH__GAMEPLAY_SAMPLE_EMPTY_COMPONENT TM_STATIC_HASH("gameplay_sample_empty_component", 0x41d4e21b430a8ae4ULL)
#define GAMEPLAY_SYSTEM_NAME TM_LOCALIZE_LATER("Gameplay Sample Empty")
#define GAMEPLAY_SYSTEM_NAME_HASH TM_STATIC_HASH("Gameplay Sample Empty", 0x11436e6de100d723ULL)

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
        .name = GAMEPLAY_SYSTEM_NAME,
        .update = (void (*)(tm_entity_context_o*, tm_entity_system_o*))system_update,
        .inst = (tm_entity_system_o*)ctx
    };

    tm_entity_api->register_system(ctx->entity_ctx, &gameplay_system);
}

static void system_hot_reload(tm_entity_context_o *entity_ctx, tm_entity_system_i *system)
{
    system->update = (void (*)(tm_entity_context_o*, tm_entity_system_o*))system_update;
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
    tm_entity_context_o *ctx = manager->entity_ctx;
    tm_entity_api->call_remove_on_all_entities(ctx, tm_entity_api->lookup_component(ctx, TYPE_HASH__GAMEPLAY_SAMPLE_EMPTY_COMPONENT));
    tm_allocator_i a = manager->allocator;
    tm_free(&a, manager, sizeof(*manager));
    tm_entity_api->destroy_child_allocator(ctx, &a);
}

static void create(tm_entity_context_o* entity_ctx)
{
    tm_allocator_i a;
    tm_entity_api->create_child_allocator(entity_ctx, TYPE__GAMEPLAY_SAMPLE_EMPTY_COMPONENT, &a);
    gameplay_component_manager_t* manager = tm_alloc(&a, sizeof(*manager));

    *manager = (gameplay_component_manager_t){
        .allocator = a,
        .entity_ctx = entity_ctx
    };

    const tm_component_i component = {
        .name = TYPE__GAMEPLAY_SAMPLE_EMPTY_COMPONENT,
        .bytes = sizeof(tm_gameplay_context_t),
        .manager = (tm_component_manager_o*)manager,
        .add = (void (*)(tm_component_manager_o*, tm_entity_t, void*))component_added,
        .remove = (void (*)(tm_component_manager_o*, tm_entity_t, void*))component_removed,
        .destroy = (void (*)(tm_component_manager_o*))destroy,
    };

    tm_entity_api->register_component(entity_ctx, &component);
}

static void component_hot_reload(tm_entity_context_o *entity_ctx, tm_component_i *component)
{
    component->add = (void (*)(tm_component_manager_o*, tm_entity_t, void*))component_added;
    component->remove = (void (*)(tm_component_manager_o*, tm_entity_t, void*))component_removed;
    component->destroy = (void (*)(tm_component_manager_o*))destroy;
}

static tm_ci_editor_ui_i editor_aspect = { 0 };

static void create_truth_types(struct tm_the_truth_o* tt)
{
    const uint64_t object_type = tm_the_truth_api->create_object_type(tt, TYPE__GAMEPLAY_SAMPLE_EMPTY_COMPONENT, 0, 0);
    const uint64_t component = tm_the_truth_api->create_object_of_type(tt, tm_the_truth_api->object_type_from_name_hash(tt, TYPE_HASH__GAMEPLAY_SAMPLE_EMPTY_COMPONENT), TM_TT_NO_UNDO_SCOPE);
    (void)component;

    // This is needed in order for the component to show up in the editor.
    tm_the_truth_api->set_aspect(tt, object_type, TM_CI_EDITOR_UI, &editor_aspect);
}

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    g = reg->get(TM_GAMEPLAY_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);
    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, create_truth_types);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, create);

    static tm_entity_hot_reload_component_i hot_reload_component_i = {
        .name_hash = TYPE_HASH__GAMEPLAY_SAMPLE_EMPTY_COMPONENT,
        .reload = component_hot_reload,
    };
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_HOT_RELOAD_COMPONENT_INTERFACE_NAME, &hot_reload_component_i);

    static tm_entity_hot_reload_system_i hot_reload_system_i = {
        .name_hash = GAMEPLAY_SYSTEM_NAME_HASH,
        .reload = system_hot_reload,
    };
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_HOT_RELOAD_SYSTEM_INTERFACE_NAME, &hot_reload_system_i);
}