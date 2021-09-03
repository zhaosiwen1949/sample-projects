static struct tm_entity_api* tm_entity_api;
static struct tm_transform_component_api* tm_transform_component_api;
static struct tm_temp_allocator_api* tm_temp_allocator_api;
static struct tm_the_truth_api* tm_the_truth_api;
static struct tm_localizer_api* tm_localizer_api;

#include <plugins/entity/entity.h>
#include <plugins/entity/transform_component.h>
#include <plugins/the_machinery_shared/component_interfaces/editor_ui_interface.h>

#include <foundation/api_registry.h>
#include <foundation/carray.inl>
#include <foundation/localizer.h>
#include <foundation/math.inl>
#include <foundation/the_truth.h>

#define TM_TT_TYPE__CUSTOM_COMPONENT "tm_custom_component"
#define TM_TT_TYPE_HASH__CUSTOM_COMPONENT TM_STATIC_HASH("tm_custom_component", 0x355309758b21930cULL)

enum {
    TM_TT_PROP__CUSTOM_COMPONENT__FREQUENCY, // float
    TM_TT_PROP__CUSTOM_COMPONENT__AMPLITUDE, // float
};

struct tm_custom_component_t {
    float y0;
    float frequency;
    float amplitude;
};

static const char* component__category(void)
{
    return TM_LOCALIZE("Samples");
}

static tm_ci_editor_ui_i* editor_aspect = &(tm_ci_editor_ui_i){
    .category = component__category
};

static void truth__create_types(struct tm_the_truth_o* tt)
{
    tm_the_truth_property_definition_t custom_component_properties[] = {
        [TM_TT_PROP__CUSTOM_COMPONENT__FREQUENCY] = { "frequency", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__CUSTOM_COMPONENT__AMPLITUDE] = { "amplitude", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
    };

    const tm_tt_type_t custom_component_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__CUSTOM_COMPONENT, custom_component_properties, TM_ARRAY_COUNT(custom_component_properties));
    const tm_tt_id_t default_object = tm_the_truth_api->quick_create_object(tt, TM_TT_NO_UNDO_SCOPE, TM_TT_TYPE_HASH__CUSTOM_COMPONENT, TM_TT_PROP__CUSTOM_COMPONENT__FREQUENCY, 1.0f, TM_TT_PROP__CUSTOM_COMPONENT__AMPLITUDE, 1.0f, -1);
    tm_the_truth_api->set_default_object(tt, custom_component_type, default_object);

    tm_the_truth_api->set_aspect(tt, custom_component_type, TM_CI_EDITOR_UI, editor_aspect);
}

static bool component__load_asset(tm_component_manager_o* man, tm_entity_t e, void* c_vp, const tm_the_truth_o* tt, tm_tt_id_t asset)
{
    struct tm_custom_component_t* c = c_vp;
    const tm_the_truth_object_o* asset_r = tm_tt_read(tt, asset);
    c->y0 = 0;
    c->frequency = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__CUSTOM_COMPONENT__FREQUENCY);
    c->amplitude = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__CUSTOM_COMPONENT__AMPLITUDE);
    return true;
}

static void component__create(struct tm_entity_context_o* ctx)
{
    tm_component_i component = {
        .name = TM_TT_TYPE__CUSTOM_COMPONENT,
        .bytes = sizeof(struct tm_custom_component_t),
        .load_asset = component__load_asset,
    };

    tm_entity_api->register_component(ctx, &component);
}

// Runs on (custom_component, transform_component)
static void engine_update__custom(tm_engine_o* inst, tm_engine_update_set_t* data)
{
    TM_INIT_TEMP_ALLOCATOR(ta);

    tm_entity_t* mod_transform = 0;

    struct tm_entity_context_o* ctx = (struct tm_entity_context_o*)inst;

    double t = 0;
    for (const tm_entity_blackboard_value_t* bb = data->blackboard_start; bb != data->blackboard_end; ++bb) {
        if (TM_STRHASH_U64(bb->id) == TM_STRHASH_U64(TM_ENTITY_BB__TIME))
            t = bb->double_value;
    }

    for (tm_engine_update_array_t* a = data->arrays; a < data->arrays + data->num_arrays; ++a) {
        struct tm_custom_component_t* custom = a->components[0];
        tm_transform_component_t* transform = a->components[1];

        for (uint32_t i = 0; i < a->n; ++i) {
            if (!custom[i].y0)
                custom[i].y0 = transform[i].world.pos.y;
            const float y = custom[i].y0 + custom[i].amplitude * sinf((float)t * custom[i].frequency);

            transform[i].world.pos.y = y;
            ++transform[i].version;
            tm_carray_temp_push(mod_transform, a->entities[i], ta);
        }
    }

    tm_entity_api->notify(ctx, data->engine->components[1], mod_transform, (uint32_t)tm_carray_size(mod_transform));

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

static bool engine_filter__custom(tm_engine_o* inst, const tm_component_type_t* components, uint32_t num_components, const tm_component_mask_t* mask)
{
    return tm_entity_mask_has_component(mask, components[0]) && tm_entity_mask_has_component(mask, components[1]);
}

static void component__register_engine(struct tm_entity_context_o* ctx)
{
    const tm_component_type_t custom_component = tm_entity_api->lookup_component_type(ctx, TM_TT_TYPE_HASH__CUSTOM_COMPONENT);
    const tm_component_type_t transform_component = tm_entity_api->lookup_component_type(ctx, TM_TT_TYPE_HASH__TRANSFORM_COMPONENT);

    const tm_engine_i custom_engine = {
        .ui_name = "Custom Component",
        .hash = TM_STATIC_HASH("TM_ENGINE__CUSTOM_COMPONENT", 0x8e8316d05d37167eULL),
        .num_components = 2,
        .components = { custom_component, transform_component },
        .writes = { false, true },
        .update = engine_update__custom,
        .filter = engine_filter__custom,
        .inst = (tm_engine_o*)ctx,
    };
    tm_entity_api->register_engine(ctx, &custom_engine);
}

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    tm_entity_api = tm_get_api(reg, tm_entity_api);
    tm_transform_component_api = tm_get_api(reg, tm_transform_component_api);
    tm_the_truth_api = tm_get_api(reg, tm_the_truth_api);
    tm_temp_allocator_api = tm_get_api(reg, tm_temp_allocator_api);
    tm_localizer_api = tm_get_api(reg, tm_localizer_api);

    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, truth__create_types);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, component__create);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_SIMULATION_REGISTER_ENGINES_INTERFACE_NAME, component__register_engine);
}
