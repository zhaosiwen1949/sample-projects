struct tm_api_registry_api *tm_global_api_registry;

static struct tm_entity_api* tm_entity_api;
static struct tm_allocator_api *tm_allocator_api;
static struct tm_the_truth_api* tm_the_truth_api;
static struct tm_the_truth_common_types_api *tm_the_truth_common_types_api;
static struct tm_localizer_api* tm_localizer_api;
static struct tm_render_graph_module_api *tm_render_graph_module_api;
static struct tm_render_graph_toolbox_api *tm_render_graph_toolbox_api;
static struct tm_properties_view_api *tm_properties_view_api;
static struct tm_render_graph_setup_api *tm_render_graph_setup_api;
static struct tm_render_graph_execute_api *tm_render_graph_execute_api;
static struct tm_buffer_format_api *tm_buffer_format_api;
static struct tm_shader_repository_api *tm_shader_repository_api;
static struct tm_renderer_api *tm_renderer_api;
static struct tm_temp_allocator_api *tm_temp_allocator_api;
static struct tm_shader_api *tm_shader_api;
static struct tm_logger_api *tm_logger_api;

#include "atmospheric_sky_component.h"
#include <foundation/allocator.h>
#include <foundation/temp_allocator.h>
#include <foundation/api_registry.h>
#include <foundation/api_type_hashes.h>
#include <foundation/buffer_format.h>
#include <foundation/camera.h>
#include <foundation/localizer.h>
#include <foundation/murmurhash64a.inl>
#include <foundation/the_truth.h>
#include <foundation/the_truth_types.h>
#include <foundation/unicode_symbols.h>
#include <foundation/visibility_flags.h>
#include <foundation/macros.h>
#include <foundation/log.h>
#include <foundation/math.inl>
#include <foundation/carray.inl>
#include <foundation/rect.inl>

#include <plugins/creation_graph/creation_graph.h>
#include <plugins/creation_graph/creation_graph_interpreter.h>
#include <plugins/creation_graph/creation_graph_node_type.h>
#include <plugins/creation_graph/image_nodes.h>
#include <plugins/editor_views/properties.h>
#include <plugins/entity/entity.h>
#include <plugins/entity/transform_component.h>
#include <plugins/render_graph/render_graph.h>
#include <plugins/render_graph_toolbox/render_pipeline.h>
#include <plugins/render_graph_toolbox/shadow_mapping.h>
#include <plugins/render_graph_toolbox/toolbox_common.h>
#include <plugins/render_utilities/cubemap_capture_component.h>
#include <plugins/render_utilities/primitive_drawer.h>
#include <plugins/render_utilities/render_component.h>
#include <plugins/renderer/commands.h>
#include <plugins/renderer/render_backend.h>
#include <plugins/renderer/render_command_buffer.h>
#include <plugins/renderer/renderer.h>
#include <plugins/renderer/resources.h>
#include <plugins/shader_system/shader_system.h>
#include <plugins/the_machinery_shared/component_interfaces/editor_ui_interface.h>
#include <plugins/the_machinery_shared/component_interfaces/shader_interface.h>
#include <plugins/the_machinery_shared/render_context.h>
#include <plugins/ui/ui.h>
#include <plugins/ui/ui_icon.h>


#include <plugins/default_render_pipe/default_render_pipe.h>

typedef struct tm_density_profile_layer_t {
    float width;
    float exp_term;
    float exp_scale;
    float linear_term;
    float constant_term;
} tm_density_profile_layer_t;

typedef struct tm_density_profile_t {
    tm_density_profile_layer_t layers[2];
    TM_PAD(8);
} tm_density_profile_t;

typedef struct tm_atmosphere_parameters_t {
    // The sun's angular radius. Warning: the implementation uses approximations
    // that are valid only if this angle is smaller than 0.1 radians.
    float sun_angular_radius;
    // The distance between the planet center and the bottom of the atmosphere.
    float bottom_radius;
    // The distance between the planet center and the top of the atmosphere.
    float top_radius;
    // The density profile of air molecules, i.e. a function from altitude to
    // dimensionless values between 0 (null density) and 1 (maximum density).
    tm_density_profile_t rayleigh_density;
    // The scattering coefficient of air molecules at the altitude where their
    // density is maximum (usually the bottom of the atmosphere), as a function of
    // wavelength. The scattering coefficient at altitude h is equal to
    // 'rayleigh_scattering' times 'rayleigh_density' at this altitude.
    tm_vec3_t rayleigh_scattering;
    // The density profile of aerosols, i.e. a function from altitude to
    // dimensionless values between 0 (null density) and 1 (maximum density).
    tm_density_profile_t mie_density;
    // The scattering coefficient of aerosols at the altitude where their density
    // is maximum (usually the bottom of the atmosphere), as a function of
    // wavelength. The scattering coefficient at altitude h is equal to
    // 'mie_scattering' times 'mie_density' at this altitude.
    tm_vec3_t mie_scattering;
    // The extinction coefficient of aerosols at the altitude where their density
    // is maximum (usually the bottom of the atmosphere), as a function of
    // wavelength. The extinction coefficient at altitude h is equal to
    // 'mie_extinction' times 'mie_density' at this altitude.
    tm_vec3_t mie_extinction;
    // The asymetry parameter for the Cornette-Shanks phase function for the
    // aerosols.
    tm_vec3_t mie_absorption;
    float mie_phase_function_g;
    // The density profile of air molecules that absorb light (e.g. ozone), i.e.
    // a function from altitude to dimensionless values between 0 (null density)
    // and 1 (maximum density).
    tm_density_profile_t absorption_density;
    // The extinction coefficient of molecules that absorb light (e.g. ozone) at
    // the altitude where their density is maximum, as a function of wavelength.
    // The extinction coefficient at altitude h is equal to
    // 'absorption_extinction' times 'absorption_density' at this altitude.
    tm_vec3_t absorption_extinction;
    // The average albedo of the ground.
    tm_vec3_t ground_albedo;
    tm_vec3_t global_iluminance;
} tm_atmosphere_parameters_t;


typedef struct tm_atmospheric_sky_component_t {
    tm_atmosphere_parameters_t atmosphere;

    tm_vec4_t sun_direction;

    tm_vec3_t celestial_body_center;

    float offset_from_center;

} tm_atmospheric_sky_component_t;

typedef struct lut_module_runtime_data_t {
    tm_render_graph_handle_t multi_scattering_resource;
    tm_render_graph_handle_t transmittance_resource;
    tm_render_graph_handle_t sky_view_resource;
} lut_module_runtime_data_t;

typedef struct tm_component_manager_o
{
    tm_entity_context_o *ctx;
    tm_allocator_i allocator;

    tm_render_graph_module_o *atmospheric_sky_module;
    tm_renderer_backend_i *rb;
    tm_the_truth_o *tt;
    tm_tt_type_t atmospheric_sky_type;
    uint64_t version;

    uint32_t TRANSMITTANCE_TEXTURE_WIDTH;
    uint32_t TRANSMITTANCE_TEXTURE_HEIGHT;
    uint32_t MULTI_SCATTERING_TEXTURE_WIDTH;
    uint32_t MULTI_SCATTERING_TEXTURE_HEIGHT;

    tm_atmospheric_sky_component_t *atmosphere;

    tm_shader_o *transmittance_shader;
    tm_shader_o *multi_scattering_shader;
    tm_shader_o *render_transmittance_shader;
    tm_shader_o *render_sky_view;
    tm_shader_o *render_sky_ray_march;
    tm_render_graph_handle_t transmittance_texture;
    tm_render_graph_handle_t multi_scattering_texture;
    tm_render_graph_handle_t sky_view_lut_texture;
    tm_render_graph_handle_t depth_buffer_texture;
    tm_render_graph_handle_t render_target;

    bool active;
    bool lut_computed;
    TM_PAD(2);
} tm_component_manager_o;

typedef struct tm_const_data_t {
    tm_component_manager_o *manager;
} tm_const_data_t;

static const char* component__category(void)
{
    return TM_LOCALIZE("Samples");
}

static tm_ci_editor_ui_i* editor_aspect = &(tm_ci_editor_ui_i){
    .category = component__category
};

static tm_atmospheric_sky_component_t default_values = {
    .atmosphere = {
        .sun_angular_radius =  0.004675f,
        .bottom_radius = 6360.0f,
        .top_radius = 6460.0f,
        .rayleigh_density = { {
            {
                .width = 0.0f,
                .exp_term = 0.0f,
                .exp_scale = 0.0f,
                .linear_term = 0.0f,
                .constant_term = 0.0f
            },
            {
                .width = 0.0f,
                .exp_term = 1.0f,
                .exp_scale = -0.124f,
                .linear_term = 0.0f,
                .constant_term = 0.0f
            }
        } },
        .rayleigh_scattering = { 0.005802f, 0.013558f, 0.033100f },
        .mie_density = { {
            {
                .width = 0.0f,
                .exp_term = 0.0f,
                .exp_scale = 0.0f,
                .linear_term = 0.0f,
                .constant_term = 0.0f
            },
            {
                .width = 0.0f,
                .exp_term = 1.0f,
                .exp_scale = -0.8333333333333334f,
                .linear_term = 0.0f,
                .constant_term = 0.0f
            }
        } },
        .mie_scattering = { 0.003996f, 0.003996f, 0.003996f },
        .mie_extinction = { 0.004440f, 0.004440f, 0.004440f },
        .mie_absorption = { 0.0f, 0.0f, 0.0f },
        .mie_phase_function_g = 0.8f,
        .absorption_density = { {
            {
                .width = 25.0f,
                .exp_term = 0.0f,
                .exp_scale = 0.0f,
                .linear_term = 1.0f / 15.0f,
                .constant_term = -2.0f / 3.0f
            },
            {
                .width = 0.0f,
                .exp_term = 0.0f,
                .exp_scale = 0.0f,
                .linear_term = -1.0f / 15.0f,
                .constant_term = 8.0f / 3.0f
            }
        } },
        .absorption_extinction = { 0.000650f, 0.001881f, 0.000085f },
        .ground_albedo = { 0.0f, 0.0f, 0.0f },
        .global_iluminance = { 10.0f, 10.0f, 10.0f },
    },
    .sun_direction = { 0.7071068f, 0.0f, 0.0f, 0.7071068f },
    .celestial_body_center = { 0.0, 0.0, 0.0 },
};

static tm_ci_shader_i *shader_aspect;
static tm_properties_aspect_i *properties_aspect;

static void truth__create_types(struct tm_the_truth_o* tt)
{
    tm_the_truth_property_definition_t atmospheric_sky_component_properties[] = {
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__SUN_DIRECTION] = { "sun_direction", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__ROTATION },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__CELESTIAL_BODY_CENTER] = { "celestial_body_center", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__OFFSET_FROM_CENTER] = { "offset_from_center", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__SUN_ANGULAR_RADIUS] = { "sun_angular_radius", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__BOTTOM_RADIUS] = { "bottom_radius", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__TOP_RADIUS] = { "top_radius", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },

        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_WIDTH] = { "rayleigh_density0_width", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_EXP_TERM] = { "rayleigh_density0_exp_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_EXP_SCALE] = { "rayleigh_density0_exp_scale", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_LINEAR_TERM] = { "rayleigh_density0_linear_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_CONSTANT_TERM] = { "rayleigh_density0_constant_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_WIDTH] = { "rayleigh_density1_width", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_EXP_TERM] = { "rayleigh_density1_exp_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_EXP_SCALE] = { "rayleigh_density1_exp_scale", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_LINEAR_TERM] = { "rayleigh_density1_linear_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_CONSTANT_TERM] = { "rayleigh_density1_constant_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_SCATTERING] = { "rayleigh_scattering", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },

        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_WIDTH] = { "mie_density0_width", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_EXP_TERM] = { "mie_density0_exp_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_EXP_SCALE] = { "mie_density0_exp_scale", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_LINEAR_TERM] = { "mie_density0_linear_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_CONSTANT_TERM] = { "mie_density0_constant_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_WIDTH] = { "mie_density1_width", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_EXP_TERM] = { "mie_density1_exp_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_EXP_SCALE] = { "mie_density1_exp_scale", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_LINEAR_TERM] = { "mie_density1_linear_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_CONSTANT_TERM] = { "mie_density1_constant_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_SCATTERING] = { "mie_scattering", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_EXTINCTION] = { "mie_extinction", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_ABSORPTION] = { "mie_absorption", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_PHASE_FUNCTION_G] = { "mie_phase_function_g", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },

        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_WIDTH] = { "absorption_density0_width", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_EXP_TERM] = { "absorption_density0_exp_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_EXP_SCALE] = { "absorption_density0_exp_scale", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_LINEAR_TERM] = { "absorption_density0_linear_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_CONSTANT_TERM] = { "absorption_density0_constant_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_WIDTH] = { "absorption_density1_width", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_EXP_TERM] = { "absorption_density1_exp_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_EXP_SCALE] = { "absorption_density1_exp_scale", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_LINEAR_TERM] = { "absorption_density1_linear_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_CONSTANT_TERM] = { "absorption_density1_constant_term", TM_THE_TRUTH_PROPERTY_TYPE_FLOAT },

        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_EXTINCTION] = { "absorption_extinction", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__GROUND_ALBEDO] = { "ground_albedo", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
        [TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__GLOBAL_ILUMINANCE] = { "global_iluminance", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__VEC3 },
    };

    const tm_tt_type_t atmospheric_sky_component_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__ATMOSPHERIC_SKY_COMPONENT, atmospheric_sky_component_properties, TM_ARRAY_COUNT(atmospheric_sky_component_properties));
    const tm_tt_id_t component = tm_the_truth_api->create_object_of_type(tt, tm_the_truth_api->object_type_from_name_hash(tt, TM_TT_TYPE_HASH__ATMOSPHERIC_SKY_COMPONENT), TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_object_o *component_w = tm_the_truth_api->write(tt, component);
    tm_the_truth_common_types_api->set_rotation(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__SUN_DIRECTION, default_values.sun_direction, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_common_types_api->set_vec3(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__CELESTIAL_BODY_CENTER, default_values.celestial_body_center, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__OFFSET_FROM_CENTER, default_values.atmosphere.bottom_radius);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__SUN_ANGULAR_RADIUS, default_values.atmosphere.sun_angular_radius);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__BOTTOM_RADIUS, default_values.atmosphere.bottom_radius);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__TOP_RADIUS, default_values.atmosphere.top_radius);

    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_WIDTH, default_values.atmosphere.rayleigh_density.layers[0].width);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_EXP_TERM, default_values.atmosphere.rayleigh_density.layers[0].exp_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_EXP_SCALE, default_values.atmosphere.rayleigh_density.layers[0].exp_scale);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_LINEAR_TERM, default_values.atmosphere.rayleigh_density.layers[0].linear_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_CONSTANT_TERM, default_values.atmosphere.rayleigh_density.layers[0].constant_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_WIDTH, default_values.atmosphere.rayleigh_density.layers[1].width);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_EXP_TERM, default_values.atmosphere.rayleigh_density.layers[1].exp_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_EXP_SCALE, default_values.atmosphere.rayleigh_density.layers[1].exp_scale);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_LINEAR_TERM, default_values.atmosphere.rayleigh_density.layers[1].linear_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_CONSTANT_TERM, default_values.atmosphere.rayleigh_density.layers[1].constant_term);

    tm_the_truth_common_types_api->set_vec3(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_SCATTERING, default_values.atmosphere.rayleigh_scattering, TM_TT_NO_UNDO_SCOPE);

    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_WIDTH, default_values.atmosphere.mie_density.layers[0].width);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_EXP_TERM, default_values.atmosphere.mie_density.layers[0].exp_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_EXP_SCALE, default_values.atmosphere.mie_density.layers[0].exp_scale);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_LINEAR_TERM, default_values.atmosphere.mie_density.layers[0].linear_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_CONSTANT_TERM, default_values.atmosphere.mie_density.layers[0].constant_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_WIDTH, default_values.atmosphere.mie_density.layers[1].width);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_EXP_TERM, default_values.atmosphere.mie_density.layers[1].exp_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_EXP_SCALE, default_values.atmosphere.mie_density.layers[1].exp_scale);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_LINEAR_TERM, default_values.atmosphere.mie_density.layers[1].linear_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_CONSTANT_TERM, default_values.atmosphere.mie_density.layers[1].constant_term);

    tm_the_truth_common_types_api->set_vec3(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_SCATTERING, default_values.atmosphere.mie_scattering, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_common_types_api->set_vec3(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_EXTINCTION, default_values.atmosphere.mie_extinction, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_common_types_api->set_vec3(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_ABSORPTION, default_values.atmosphere.mie_absorption, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_PHASE_FUNCTION_G, default_values.atmosphere.mie_phase_function_g);

    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_WIDTH, default_values.atmosphere.absorption_density.layers[0].width);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_EXP_TERM, default_values.atmosphere.absorption_density.layers[0].exp_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_EXP_SCALE, default_values.atmosphere.absorption_density.layers[0].exp_scale);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_LINEAR_TERM, default_values.atmosphere.absorption_density.layers[0].linear_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_CONSTANT_TERM, default_values.atmosphere.absorption_density.layers[0].constant_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_WIDTH, default_values.atmosphere.absorption_density.layers[1].width);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_EXP_TERM, default_values.atmosphere.absorption_density.layers[1].exp_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_EXP_SCALE, default_values.atmosphere.absorption_density.layers[1].exp_scale);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_LINEAR_TERM, default_values.atmosphere.absorption_density.layers[1].linear_term);
    tm_the_truth_api->set_float(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_CONSTANT_TERM, default_values.atmosphere.absorption_density.layers[1].constant_term);


    tm_the_truth_common_types_api->set_vec3(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_EXTINCTION, default_values.atmosphere.absorption_extinction, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_common_types_api->set_vec3(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__GROUND_ALBEDO, default_values.atmosphere.ground_albedo, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_common_types_api->set_vec3(tt, component_w, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__GLOBAL_ILUMINANCE, default_values.atmosphere.global_iluminance, TM_TT_NO_UNDO_SCOPE);

    tm_the_truth_api->commit(tt, component_w, TM_TT_NO_UNDO_SCOPE);

    tm_the_truth_api->set_default_object(tt, atmospheric_sky_component_type, component);

    tm_the_truth_api->set_aspect(tt, atmospheric_sky_component_type, TM_CI_EDITOR_UI, editor_aspect);
    tm_the_truth_api->set_aspect(tt, atmospheric_sky_component_type, TM_CI_SHADER, shader_aspect);
    tm_the_truth_api->set_aspect(tt, atmospheric_sky_component_type, TM_TT_ASPECT__PROPERTIES, properties_aspect);
}

static bool component__load_asset(tm_component_manager_o* man, tm_entity_t e, void* c_vp, const tm_the_truth_o* tt, tm_tt_id_t asset)
{
    struct tm_atmospheric_sky_component_t* c = c_vp;
    const tm_the_truth_object_o* asset_r = tm_tt_read(tt, asset);
    c->sun_direction = tm_the_truth_common_types_api->get_rotation(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__SUN_DIRECTION);
    c->celestial_body_center = tm_the_truth_common_types_api->get_vec3(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__CELESTIAL_BODY_CENTER);
    c->offset_from_center = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__OFFSET_FROM_CENTER);
    c->atmosphere.sun_angular_radius = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__SUN_ANGULAR_RADIUS);
    c->atmosphere.bottom_radius = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__BOTTOM_RADIUS);
    c->atmosphere.top_radius = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__TOP_RADIUS);

    c->atmosphere.rayleigh_density.layers[0].width = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_WIDTH);
    c->atmosphere.rayleigh_density.layers[0].exp_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_EXP_TERM);
    c->atmosphere.rayleigh_density.layers[0].exp_scale = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_EXP_SCALE);
    c->atmosphere.rayleigh_density.layers[0].linear_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_LINEAR_TERM);
    c->atmosphere.rayleigh_density.layers[0].constant_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_CONSTANT_TERM);
    c->atmosphere.rayleigh_density.layers[1].width = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_WIDTH);
    c->atmosphere.rayleigh_density.layers[1].exp_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_EXP_TERM);
    c->atmosphere.rayleigh_density.layers[1].exp_scale = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_EXP_SCALE);
    c->atmosphere.rayleigh_density.layers[1].linear_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_LINEAR_TERM);
    c->atmosphere.rayleigh_density.layers[1].constant_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_CONSTANT_TERM);

    c->atmosphere.rayleigh_scattering = tm_the_truth_common_types_api->get_vec3(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_SCATTERING);

    c->atmosphere.mie_density.layers[0].width = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_WIDTH);
    c->atmosphere.mie_density.layers[0].exp_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_EXP_TERM);
    c->atmosphere.mie_density.layers[0].exp_scale = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_EXP_SCALE);
    c->atmosphere.mie_density.layers[0].linear_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_LINEAR_TERM);
    c->atmosphere.mie_density.layers[0].constant_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_CONSTANT_TERM);
    c->atmosphere.mie_density.layers[1].width = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_WIDTH);
    c->atmosphere.mie_density.layers[1].exp_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_EXP_TERM);
    c->atmosphere.mie_density.layers[1].exp_scale = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_EXP_SCALE);
    c->atmosphere.mie_density.layers[1].linear_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_LINEAR_TERM);
    c->atmosphere.mie_density.layers[1].constant_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_CONSTANT_TERM);

    c->atmosphere.mie_scattering = tm_the_truth_common_types_api->get_vec3(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_SCATTERING);
    c->atmosphere.mie_extinction = tm_the_truth_common_types_api->get_vec3(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_EXTINCTION);
    c->atmosphere.mie_absorption = tm_the_truth_common_types_api->get_vec3(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_ABSORPTION);
    c->atmosphere.mie_phase_function_g = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_PHASE_FUNCTION_G);

    c->atmosphere.absorption_density.layers[0].width = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_WIDTH);
    c->atmosphere.absorption_density.layers[0].exp_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_EXP_TERM);
    c->atmosphere.absorption_density.layers[0].exp_scale = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_EXP_SCALE);
    c->atmosphere.absorption_density.layers[0].linear_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_LINEAR_TERM);
    c->atmosphere.absorption_density.layers[0].constant_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_CONSTANT_TERM);
    c->atmosphere.absorption_density.layers[1].width = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_WIDTH);
    c->atmosphere.absorption_density.layers[1].exp_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_EXP_TERM);
    c->atmosphere.absorption_density.layers[1].exp_scale = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_EXP_SCALE);
    c->atmosphere.absorption_density.layers[1].linear_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_LINEAR_TERM);
    c->atmosphere.absorption_density.layers[1].constant_term = tm_the_truth_api->get_float(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_CONSTANT_TERM);
    c->atmosphere.absorption_extinction = tm_the_truth_common_types_api->get_vec3(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_EXTINCTION);
    c->atmosphere.ground_albedo = tm_the_truth_common_types_api->get_vec3(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__GROUND_ALBEDO);
    c->atmosphere.global_iluminance = tm_the_truth_common_types_api->get_vec3(tt, asset_r, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__GLOBAL_ILUMINANCE);

    return true;
}

static void update_atmosphere_parameters(const tm_const_data_t * cdata, tm_shader_io_o *io, tm_renderer_resource_command_buffer_o *res_buf, tm_shader_constant_buffer_instance_t *cbufs, tm_atmospheric_sky_component_t *atmosphere)
{

    uint32_t n_cbufs = tm_carray_size(cbufs);
    uint32_t constant_offset;
    tm_shader_constant_t constant;

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("sun_angular_radius", 0xf01da84c71a15788ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.sun_angular_radius), .data = &atmosphere->atmosphere.sun_angular_radius }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("sun_angular_radius", 0xf01da84c71a15788ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.sun_angular_radius), .data = &atmosphere->atmosphere.sun_angular_radius }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("offset_from_center", 0x58ec72ba46c74c98ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->offset_from_center), .data = &atmosphere->offset_from_center }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("celestial_body_center", 0xeebd53a5a88c66c5ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->celestial_body_center), .data = &atmosphere->celestial_body_center }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("mie_phase_function_g", 0xfb3551fa10d265e2ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.mie_phase_function_g), .data = &atmosphere->atmosphere.mie_phase_function_g }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("top_radius", 0x52b9fd3ccf6675b2ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.top_radius), .data = &atmosphere->atmosphere.top_radius }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("bottom_radius", 0x5fbb41b30893e4feULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.bottom_radius), .data = &atmosphere->atmosphere.bottom_radius }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("rayleigh_scattering", 0xc1c2431d75ff0736ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.rayleigh_scattering), .data = &atmosphere->atmosphere.rayleigh_scattering }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("absorption_extinction", 0xe83d1a05b00e46b9ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.absorption_extinction), .data = &atmosphere->atmosphere.absorption_extinction }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("mie_scattering", 0xe4fe7e198c76d3f6ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.mie_scattering), .data = &atmosphere->atmosphere.mie_scattering }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("mie_extinction", 0xa1be8cdd58360c60ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.mie_extinction), .data = &atmosphere->atmosphere.mie_extinction }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("mie_absorption", 0xfb8996a711fed1ffULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.mie_absorption), .data = &atmosphere->atmosphere.mie_absorption }, 1);
    }

    tm_vec4_t r_density[3] = {
        {
            atmosphere->atmosphere.rayleigh_density.layers[0].width,
            atmosphere->atmosphere.rayleigh_density.layers[0].exp_term,
            atmosphere->atmosphere.rayleigh_density.layers[0].exp_scale,
            atmosphere->atmosphere.rayleigh_density.layers[0].linear_term,
        },
        {
            atmosphere->atmosphere.rayleigh_density.layers[0].constant_term,
            atmosphere->atmosphere.rayleigh_density.layers[1].width,
            atmosphere->atmosphere.rayleigh_density.layers[1].exp_term,
            atmosphere->atmosphere.rayleigh_density.layers[1].exp_scale,
        },
        {
            atmosphere->atmosphere.rayleigh_density.layers[1].linear_term,
            atmosphere->atmosphere.rayleigh_density.layers[1].constant_term,
            0, 0
        }
    };
    tm_vec4_t m_density[3] = {
        {
            atmosphere->atmosphere.mie_density.layers[0].width,
            atmosphere->atmosphere.mie_density.layers[0].exp_term,
            atmosphere->atmosphere.mie_density.layers[0].exp_scale,
            atmosphere->atmosphere.mie_density.layers[0].linear_term,
        },
        {
            atmosphere->atmosphere.mie_density.layers[0].constant_term,
            atmosphere->atmosphere.mie_density.layers[1].width,
            atmosphere->atmosphere.mie_density.layers[1].exp_term,
            atmosphere->atmosphere.mie_density.layers[1].exp_scale,
        },
        {
            atmosphere->atmosphere.mie_density.layers[1].linear_term,
            atmosphere->atmosphere.mie_density.layers[1].constant_term,
            0, 0
        }
    };
    tm_vec4_t a_density[3] = {
        {
            atmosphere->atmosphere.absorption_density.layers[0].width,
            atmosphere->atmosphere.absorption_density.layers[0].exp_term,
            atmosphere->atmosphere.absorption_density.layers[0].exp_scale,
            atmosphere->atmosphere.absorption_density.layers[0].linear_term,
        },
        {
            atmosphere->atmosphere.absorption_density.layers[0].constant_term,
            atmosphere->atmosphere.absorption_density.layers[1].width,
            atmosphere->atmosphere.absorption_density.layers[1].exp_term,
            atmosphere->atmosphere.absorption_density.layers[1].exp_scale,
        },
        {
            atmosphere->atmosphere.absorption_density.layers[1].linear_term,
            atmosphere->atmosphere.absorption_density.layers[1].constant_term,
            0, 0
        }
    };

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("rayleigh_density", 0x43fe31ad6e0c5ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(r_density), .data = r_density }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("mie_density", 0x7989e44c792e45ceULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(m_density), .data = m_density }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("absorption_density", 0x3e782aa7e448057dULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(a_density), .data = a_density }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("ground_albedo", 0x4c446f35cd688868ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.ground_albedo), .data = &atmosphere->atmosphere.ground_albedo }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("global_iluminance", 0xd937ae8547805005ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(atmosphere->atmosphere.ground_albedo), .data = &atmosphere->atmosphere.global_iluminance }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("TRANSMITTANCE_TEXTURE_WIDTH", 0xae5b4c2ccc25da85ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(cdata->manager->TRANSMITTANCE_TEXTURE_WIDTH), .data = &cdata->manager->TRANSMITTANCE_TEXTURE_WIDTH }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("TRANSMITTANCE_TEXTURE_HEIGHT", 0x22702983e0a15faULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(cdata->manager->TRANSMITTANCE_TEXTURE_HEIGHT), .data = &cdata->manager->TRANSMITTANCE_TEXTURE_HEIGHT }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("MULTI_SCATTERING_TEXTURE_WIDTH", 0xb09831b6c2f9cdebULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(cdata->manager->MULTI_SCATTERING_TEXTURE_WIDTH), .data = &cdata->manager->MULTI_SCATTERING_TEXTURE_WIDTH }, 1);
    }

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("MULTI_SCATTERING_TEXTURE_HEIGHT", 0xf1f976bab1efa3d1ULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(cdata->manager->MULTI_SCATTERING_TEXTURE_HEIGHT), .data = &cdata->manager->MULTI_SCATTERING_TEXTURE_HEIGHT }, 1);
    }

    tm_vec3_t forward = { 0.0, 0.0, -1.0 };
    tm_vec3_t sun_direction = tm_quaternion_rotate_vec3(atmosphere->sun_direction, forward);

    if (tm_shader_api->lookup_constant(io, TM_STATIC_HASH("sun_direction", 0x93b473247f3ee6dfULL), &constant, &constant_offset)) {
        for (uint32_t i = 0; i != n_cbufs; ++i)
            tm_shader_api->update_constants(io, res_buf, &(tm_shader_constant_update_t){ .instance_id = cbufs[i].instance_id, .constant_offset = constant_offset, .num_bytes = sizeof(tm_vec3_t), .data = &sun_direction }, 1);
    }


}

static tm_render_graph_handle_t graph_gpu_resource(tm_render_graph_setup_o *graph_setup, tm_strhash_t resource_name)
{
    tm_render_graph_blackboard_value v;
    if (tm_render_graph_setup_api->read_blackboard(graph_setup, resource_name, &v))
        return (tm_render_graph_handle_t){ v.uint32 };
    else
        return tm_render_graph_setup_api->external_resource(graph_setup, resource_name);
}


static void compute_transmittance_setup_pass(const void *const_data, void *runtime_data, tm_render_graph_setup_o *graph_setup)
{
    const tm_const_data_t *cdata = (tm_const_data_t *)const_data;
    lut_module_runtime_data_t *rdata = (lut_module_runtime_data_t *)runtime_data;
    rdata->transmittance_resource = graph_gpu_resource(graph_setup, TM_ATMOSPHERIC_SKY_TRANSMITTANCE);
    tm_render_graph_setup_api->set_active(graph_setup, !cdata->manager->lut_computed && cdata->manager->active);
    if (cdata->manager->lut_computed || !cdata->manager->active)
        return;
    tm_render_graph_setup_api->set_output(graph_setup, true);
    tm_render_graph_setup_api->set_request_async_compute(graph_setup, true);
    cdata->manager->transmittance_shader = tm_shader_repository_api->lookup_shader(tm_render_graph_setup_api->shader_repository(graph_setup), TM_STATIC_HASH("compute_transmittance", 0xa33fbbc7e6ef4995ULL));
    tm_render_graph_setup_api->write_gpu_resource(graph_setup, rdata->transmittance_resource, TM_RENDER_GRAPH_WRITE_BIND_FLAG_UAV, TM_RENDERER_RESOURCE_STATE_COMPUTE_SHADER | TM_RENDERER_RESOURCE_STATE_UAV, TM_RENDERER_RESOURCE_LOAD_OP_DISCARD, 0, (tm_strhash_t){0});
}

static void compute_transmittance_execute_pass(const void *const_data, void *runtime_data, uint64_t sort_key, tm_render_graph_execute_o *graph_execute)
{
    const tm_const_data_t *cdata = (tm_const_data_t *)const_data;
    lut_module_runtime_data_t *rdata = (lut_module_runtime_data_t *)runtime_data;

    tm_renderer_resource_command_buffer_o *res_buf = tm_render_graph_execute_api->default_resource_command_buffer(graph_execute);

    TM_INIT_TEMP_ALLOCATOR(ta);
    const uint32_t n_cbufs = 1;
    tm_shader_constant_buffer_instance_t *cbufs = 0;
    tm_carray_temp_resize(cbufs, n_cbufs, ta);
    tm_shader_resource_binder_instance_t *rbinders = 0;
    tm_carray_temp_resize(rbinders, 1, ta);
    tm_shader_io_o *io = tm_shader_api->shader_io(cdata->manager->transmittance_shader);
    tm_shader_api->create_constant_buffer_instances(io, n_cbufs, cbufs);
    tm_shader_api->create_resource_binder_instances(io, 1, rbinders);

    tm_renderer_command_buffer_o *cmd_buf = tm_render_graph_execute_api->default_command_buffer(graph_execute);

    update_atmosphere_parameters(cdata, io, res_buf, cbufs, cdata->manager->atmosphere);

    tm_shader_resource_t shader_resource;
    uint32_t resource_slot;
    tm_renderer_handle_t resource = tm_render_graph_execute_api->backend_handle(graph_execute, rdata->transmittance_resource);
    if (tm_shader_api->lookup_resource(io, TM_STATIC_HASH("transmittance_texture", 0x45ded323f0483ccaULL), &shader_resource, &resource_slot)) {
        uint32_t view_aspect = TM_RENDERER_IMAGE_ASPECT_DEFAULT;
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = rbinders[0].instance_id, .resource_slot = resource_slot, .num_resources = 1, .resources = &resource, .resources_view_aspect_flags = &view_aspect }, 1);

        tm_renderer_shader_info_t shader_info;
        tm_shader_api->assemble_shader_infos(cdata->manager->transmittance_shader, 0, 0, tm_render_graph_execute_api->shader_context(graph_execute), (tm_strhash_t){0}, res_buf, &cbufs[0], &rbinders[0], 1, &shader_info);
        tm_renderer_api->tm_renderer_command_buffer_api->compute_dispatches(cmd_buf, &sort_key, &(tm_renderer_compute_info_t){ .dispatch_type = TM_RENDERER_DISPATCH_TYPE_NORMAL, .dispatch.group_count = { tm_uint32_div_ceil(cdata->manager->TRANSMITTANCE_TEXTURE_WIDTH, 8), tm_uint32_div_ceil(cdata->manager->TRANSMITTANCE_TEXTURE_HEIGHT, 8), 1 } }, &shader_info, 1);
    }

    tm_shader_api->destroy_constant_buffer_instances(io, cbufs, n_cbufs);
    tm_shader_api->destroy_resource_binder_instances(io, rbinders, 1);

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

static void compute_multi_scattering_setup_pass(const void *const_data, void *runtime_data, tm_render_graph_setup_o *graph_setup)
{
    const tm_const_data_t *cdata = (tm_const_data_t *)const_data;
    lut_module_runtime_data_t *rdata = (lut_module_runtime_data_t *)runtime_data;
    rdata->transmittance_resource = graph_gpu_resource(graph_setup, TM_ATMOSPHERIC_SKY_TRANSMITTANCE);
    rdata->multi_scattering_resource = graph_gpu_resource(graph_setup, TM_ATMOSPHERIC_SKY_MULTI_SCATTERING);
    tm_render_graph_setup_api->set_active(graph_setup, !cdata->manager->lut_computed && cdata->manager->active);
    if (cdata->manager->lut_computed || !cdata->manager->active)
        return;
    tm_render_graph_setup_api->set_output(graph_setup, true);
    tm_render_graph_setup_api->set_request_async_compute(graph_setup, true);
    cdata->manager->multi_scattering_shader = tm_shader_repository_api->lookup_shader(tm_render_graph_setup_api->shader_repository(graph_setup), TM_STATIC_HASH("compute_multiple_scattering", 0x957f2c3facf15a93ULL));
    tm_render_graph_setup_api->write_gpu_resource(graph_setup, rdata->multi_scattering_resource, TM_RENDER_GRAPH_WRITE_BIND_FLAG_UAV, TM_RENDERER_RESOURCE_STATE_COMPUTE_SHADER | TM_RENDERER_RESOURCE_STATE_UAV, TM_RENDERER_RESOURCE_LOAD_OP_CLEAR, 0, (tm_strhash_t){0});
    tm_render_graph_setup_api->read_gpu_resource(graph_setup, rdata->transmittance_resource, TM_RENDERER_RESOURCE_STATE_RESOURCE | TM_RENDERER_RESOURCE_STATE_COMPUTE_SHADER);
}

static void compute_multi_scattering_execute_pass(const void *const_data, void *runtime_data, uint64_t sort_key, tm_render_graph_execute_o *graph_execute)
{
    const tm_const_data_t *cdata = (tm_const_data_t *)const_data;
    lut_module_runtime_data_t *rdata = (lut_module_runtime_data_t *)runtime_data;
    cdata->manager->lut_computed = true;

    tm_renderer_resource_command_buffer_o *res_buf = tm_render_graph_execute_api->default_resource_command_buffer(graph_execute);

    TM_INIT_TEMP_ALLOCATOR(ta);
    const uint32_t n_cbufs = 1;
    tm_shader_constant_buffer_instance_t *cbufs = 0;
    tm_carray_temp_resize(cbufs, n_cbufs, ta);
    tm_shader_resource_binder_instance_t *rbinders = 0;
    tm_carray_temp_resize(rbinders, 1, ta);
    tm_shader_io_o *io = tm_shader_api->shader_io(cdata->manager->multi_scattering_shader);
    tm_shader_api->create_constant_buffer_instances(io, n_cbufs, cbufs);
    tm_shader_api->create_resource_binder_instances(io, 1, rbinders);

    tm_renderer_command_buffer_o *cmd_buf = tm_render_graph_execute_api->default_command_buffer(graph_execute);

    update_atmosphere_parameters(cdata, io, res_buf, cbufs, cdata->manager->atmosphere);

    tm_shader_resource_t shader_resource;
    uint32_t resource_slot;
    tm_renderer_handle_t resource = tm_render_graph_execute_api->backend_handle(graph_execute, rdata->multi_scattering_resource);
    if (tm_shader_api->lookup_resource(io, TM_STATIC_HASH("mult_scat_texture", 0x26151f336fdb082bULL), &shader_resource, &resource_slot)) {
        uint32_t view_aspect = TM_RENDERER_IMAGE_ASPECT_DEFAULT;
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = rbinders[0].instance_id, .resource_slot = resource_slot, .num_resources = 1, .resources = &resource, .resources_view_aspect_flags = &view_aspect }, 1);
    }
    resource = tm_render_graph_execute_api->backend_handle(graph_execute, rdata->transmittance_resource);
    if (tm_shader_api->lookup_resource(io, TM_STATIC_HASH("transmittance_texture", 0x45ded323f0483ccaULL), &shader_resource, &resource_slot)) {
        uint32_t view_aspect = TM_RENDERER_IMAGE_ASPECT_DEFAULT;
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = rbinders[0].instance_id, .resource_slot = resource_slot, .num_resources = 1, .resources = &resource, .resources_view_aspect_flags = &view_aspect }, 1);
    }

    tm_renderer_shader_info_t shader_info;
    tm_shader_api->assemble_shader_infos(cdata->manager->multi_scattering_shader, 0, 0, tm_render_graph_execute_api->shader_context(graph_execute), (tm_strhash_t){0}, res_buf, &cbufs[0], &rbinders[0], 1, &shader_info);
    tm_renderer_api->tm_renderer_command_buffer_api->compute_dispatches(cmd_buf, &sort_key, &(tm_renderer_compute_info_t){ .dispatch_type = TM_RENDERER_DISPATCH_TYPE_NORMAL, .dispatch.group_count = { cdata->manager->MULTI_SCATTERING_TEXTURE_WIDTH, cdata->manager->MULTI_SCATTERING_TEXTURE_HEIGHT, 1 } }, &shader_info, 1);

    tm_shader_api->destroy_constant_buffer_instances(io, cbufs, n_cbufs);
    tm_shader_api->destroy_resource_binder_instances(io, rbinders, 1);

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}


static void render_sky_lut_setup_pass(const void *const_data, void *runtime_data, tm_render_graph_setup_o *graph_setup)
{
    const tm_const_data_t *cdata = (tm_const_data_t *)const_data;
    lut_module_runtime_data_t *rdata = (lut_module_runtime_data_t *)runtime_data;
    rdata->transmittance_resource = graph_gpu_resource(graph_setup, TM_ATMOSPHERIC_SKY_TRANSMITTANCE);
    rdata->multi_scattering_resource = graph_gpu_resource(graph_setup, TM_ATMOSPHERIC_SKY_MULTI_SCATTERING);
    rdata->sky_view_resource = graph_gpu_resource(graph_setup, TM_ATMOSPHERIC_SKY_SKYVIEW_LUT);
    tm_render_graph_setup_api->set_active(graph_setup, cdata->manager->active);
    if (!cdata->manager->active)
        return;
    tm_render_graph_setup_api->set_output(graph_setup, true);
    cdata->manager->render_sky_view = tm_shader_repository_api->lookup_shader(tm_render_graph_setup_api->shader_repository(graph_setup), TM_STATIC_HASH("render_sky_view", 0xd21aece39fe7852aULL));

    tm_render_graph_setup_api->write_gpu_resource(graph_setup, rdata->sky_view_resource, TM_RENDER_GRAPH_WRITE_BIND_FLAG_COLOR_TARGET, TM_RENDERER_RESOURCE_STATE_RENDER_TARGET, TM_RENDERER_RESOURCE_LOAD_OP_LOAD, 0, (tm_strhash_t){0});

    tm_render_graph_setup_api->read_gpu_resource(graph_setup, rdata->multi_scattering_resource, TM_RENDERER_RESOURCE_STATE_RESOURCE | TM_RENDERER_RESOURCE_STATE_PIXEL_SHADER);
    tm_render_graph_setup_api->read_gpu_resource(graph_setup, rdata->transmittance_resource, TM_RENDERER_RESOURCE_STATE_RESOURCE | TM_RENDERER_RESOURCE_STATE_PIXEL_SHADER);
}

static void render_sky_lut_execute_pass(const void *const_data, void *runtime_data, uint64_t sort_key, tm_render_graph_execute_o *graph_execute)
{
    const tm_const_data_t *cdata = (tm_const_data_t *)const_data;
    lut_module_runtime_data_t *rdata = (lut_module_runtime_data_t *)runtime_data;

    tm_renderer_resource_command_buffer_o *res_buf = tm_render_graph_execute_api->default_resource_command_buffer(graph_execute);

    TM_INIT_TEMP_ALLOCATOR(ta);
    const uint32_t n_cbufs = 1;
    tm_shader_constant_buffer_instance_t *cbufs = 0;
    tm_carray_temp_resize(cbufs, n_cbufs, ta);
    tm_shader_resource_binder_instance_t *rbinders = 0;
    tm_carray_temp_resize(rbinders, 1, ta);
    tm_shader_io_o *io = tm_shader_api->shader_io(cdata->manager->render_sky_view);
    tm_shader_api->create_constant_buffer_instances(io, n_cbufs, cbufs);
    tm_shader_api->create_resource_binder_instances(io, 1, rbinders);

    tm_renderer_command_buffer_o *cmd_buf = tm_render_graph_execute_api->default_command_buffer(graph_execute);

    update_atmosphere_parameters(cdata, io, res_buf, cbufs, cdata->manager->atmosphere);

    tm_shader_resource_t shader_resource;
    uint32_t resource_slot;
    tm_renderer_handle_t resource = tm_render_graph_execute_api->backend_handle(graph_execute, rdata->multi_scattering_resource);
    if (tm_shader_api->lookup_resource(io, TM_STATIC_HASH("multi_scattering_texture", 0x6c173f100ef542e2ULL), &shader_resource, &resource_slot)) {
        uint32_t view_aspect = TM_RENDERER_IMAGE_ASPECT_DEFAULT;
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = rbinders[0].instance_id, .resource_slot = resource_slot, .num_resources = 1, .resources = &resource, .resources_view_aspect_flags = &view_aspect }, 1);
    }

    resource = tm_render_graph_execute_api->backend_handle(graph_execute, rdata->transmittance_resource);
    if (tm_shader_api->lookup_resource(io, TM_STATIC_HASH("transmittance_texture", 0x45ded323f0483ccaULL), &shader_resource, &resource_slot)) {
        uint32_t view_aspect = TM_RENDERER_IMAGE_ASPECT_DEFAULT;
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = rbinders[0].instance_id, .resource_slot = resource_slot, .num_resources = 1, .resources = &resource, .resources_view_aspect_flags = &view_aspect }, 1);
    }
    tm_renderer_shader_info_t shader_info;

    static const tm_renderer_draw_call_info_t draw_call = {
        .primitive_type = TM_RENDERER_PRIMITIVE_TYPE_TRIANGLE_LIST,
        .draw_type = TM_RENDERER_DRAW_TYPE_NON_INDEXED,
        .non_indexed.first_vertex = 0,
        .non_indexed.num_vertices = 3,
        .non_indexed.num_instances = 1,
    };

    if(tm_shader_api->assemble_shader_infos(cdata->manager->render_sky_view, 0, 0, tm_render_graph_execute_api->shader_context(graph_execute), (tm_strhash_t){0}, res_buf, &cbufs[0], &rbinders[0], 1, &shader_info)) {
        tm_renderer_api->tm_renderer_command_buffer_api->draw_calls(cmd_buf, &sort_key, &draw_call, &shader_info, 1);
    }

    tm_shader_api->destroy_constant_buffer_instances(io, cbufs, n_cbufs);
    tm_shader_api->destroy_resource_binder_instances(io, rbinders, 1);

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

static void render_sky_raymarch_setup_pass(const void *const_data, void *runtime_data, tm_render_graph_setup_o *graph_setup)
{
    const tm_const_data_t *cdata = (tm_const_data_t *)const_data;
    lut_module_runtime_data_t *rdata = (lut_module_runtime_data_t *)runtime_data;
    rdata->transmittance_resource = graph_gpu_resource(graph_setup, TM_ATMOSPHERIC_SKY_TRANSMITTANCE);
    rdata->multi_scattering_resource = graph_gpu_resource(graph_setup, TM_ATMOSPHERIC_SKY_MULTI_SCATTERING);
    rdata->sky_view_resource = graph_gpu_resource(graph_setup, TM_ATMOSPHERIC_SKY_SKYVIEW_LUT);
    tm_render_graph_setup_api->set_active(graph_setup, cdata->manager->active);
    if (!cdata->manager->active)
        return;
    tm_render_graph_setup_api->set_output(graph_setup, true);
    cdata->manager->render_target = graph_gpu_resource(graph_setup, TM_DEFAULT_RENDER_PIPE_MAIN_HDR_ACCUMULATION);
    cdata->manager->depth_buffer_texture = graph_gpu_resource(graph_setup, TM_DEFAULT_RENDER_PIPE_MAIN_LINEAR_DEPTH_TARGET);
    cdata->manager->render_sky_ray_march = tm_shader_repository_api->lookup_shader(tm_render_graph_setup_api->shader_repository(graph_setup), TM_STATIC_HASH("render_sky_ray_march", 0x96e6ad48816574d1ULL));

    tm_render_graph_setup_api->write_gpu_resource(graph_setup, cdata->manager->render_target, TM_RENDER_GRAPH_WRITE_BIND_FLAG_COLOR_TARGET, TM_RENDERER_RESOURCE_STATE_RENDER_TARGET, TM_RENDERER_RESOURCE_LOAD_OP_LOAD, 0, (tm_strhash_t){0});
    tm_render_graph_setup_api->write_gpu_resource(graph_setup, graph_gpu_resource(graph_setup, TM_DEFAULT_RENDER_PIPE_MAIN_DEPTH_STENCIL_TARGET), TM_RENDER_GRAPH_WRITE_BIND_FLAG_DEPTH_STENCIL_TARGET, TM_RENDERER_RESOURCE_STATE_RENDER_TARGET, TM_RENDERER_RESOURCE_LOAD_OP_LOAD, 0, (tm_strhash_t){0});

    tm_render_graph_setup_api->read_gpu_resource(graph_setup, cdata->manager->depth_buffer_texture, TM_RENDERER_RESOURCE_STATE_RESOURCE | TM_RENDERER_RESOURCE_STATE_PIXEL_SHADER);
    tm_render_graph_setup_api->read_gpu_resource(graph_setup, rdata->multi_scattering_resource, TM_RENDERER_RESOURCE_STATE_RESOURCE | TM_RENDERER_RESOURCE_STATE_PIXEL_SHADER);
    tm_render_graph_setup_api->read_gpu_resource(graph_setup, rdata->transmittance_resource, TM_RENDERER_RESOURCE_STATE_RESOURCE | TM_RENDERER_RESOURCE_STATE_PIXEL_SHADER);
    tm_render_graph_setup_api->read_gpu_resource(graph_setup, rdata->sky_view_resource, TM_RENDERER_RESOURCE_STATE_RESOURCE | TM_RENDERER_RESOURCE_STATE_PIXEL_SHADER);
}

static void render_sky_raymarch_execute_pass(const void *const_data, void *runtime_data, uint64_t sort_key, tm_render_graph_execute_o *graph_execute)
{
    const tm_const_data_t *cdata = (tm_const_data_t *)const_data;
    lut_module_runtime_data_t *rdata = (lut_module_runtime_data_t *)runtime_data;

    tm_renderer_resource_command_buffer_o *res_buf = tm_render_graph_execute_api->default_resource_command_buffer(graph_execute);

    TM_INIT_TEMP_ALLOCATOR(ta);
    const uint32_t n_cbufs = 1;
    tm_shader_constant_buffer_instance_t *cbufs = 0;
    tm_carray_temp_resize(cbufs, n_cbufs, ta);
    tm_shader_resource_binder_instance_t *rbinders = 0;
    tm_carray_temp_resize(rbinders, 1, ta);
    tm_shader_io_o *io = tm_shader_api->shader_io(cdata->manager->render_sky_ray_march);
    tm_shader_api->create_constant_buffer_instances(io, n_cbufs, cbufs);
    tm_shader_api->create_resource_binder_instances(io, 1, rbinders);

    tm_renderer_command_buffer_o *cmd_buf = tm_render_graph_execute_api->default_command_buffer(graph_execute);

    update_atmosphere_parameters(cdata, io, res_buf, cbufs, cdata->manager->atmosphere);

    tm_shader_resource_t shader_resource;
    uint32_t resource_slot;
    tm_renderer_handle_t resource = tm_render_graph_execute_api->backend_handle(graph_execute, rdata->multi_scattering_resource);
    if (tm_shader_api->lookup_resource(io, TM_STATIC_HASH("multi_scattering_texture", 0x6c173f100ef542e2ULL), &shader_resource, &resource_slot)) {
        uint32_t view_aspect = TM_RENDERER_IMAGE_ASPECT_DEFAULT;
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = rbinders[0].instance_id, .resource_slot = resource_slot, .num_resources = 1, .resources = &resource, .resources_view_aspect_flags = &view_aspect }, 1);
    }

    resource = tm_render_graph_execute_api->backend_handle(graph_execute, rdata->transmittance_resource);
    if (tm_shader_api->lookup_resource(io, TM_STATIC_HASH("transmittance_texture", 0x45ded323f0483ccaULL), &shader_resource, &resource_slot)) {
        uint32_t view_aspect = TM_RENDERER_IMAGE_ASPECT_DEFAULT;
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = rbinders[0].instance_id, .resource_slot = resource_slot, .num_resources = 1, .resources = &resource, .resources_view_aspect_flags = &view_aspect }, 1);
    }

    resource = tm_render_graph_execute_api->backend_handle(graph_execute, rdata->sky_view_resource);
    if (tm_shader_api->lookup_resource(io, TM_STATIC_HASH("sky_view_lut_texture", 0x41e0add7ee50eb70ULL), &shader_resource, &resource_slot)) {
        uint32_t view_aspect = TM_RENDERER_IMAGE_ASPECT_DEFAULT;
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = rbinders[0].instance_id, .resource_slot = resource_slot, .num_resources = 1, .resources = &resource, .resources_view_aspect_flags = &view_aspect }, 1);
    }

    resource = tm_render_graph_execute_api->backend_handle(graph_execute, cdata->manager->depth_buffer_texture);
    if (tm_shader_api->lookup_resource(io, TM_STATIC_HASH("depth_buffer_texture", 0x51998875030e8f86ULL), &shader_resource, &resource_slot)) {
        uint32_t view_aspect = TM_RENDERER_IMAGE_ASPECT_DEPTH;
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = rbinders[0].instance_id, .resource_slot = resource_slot, .num_resources = 1, .resources = &resource, .resources_view_aspect_flags = &view_aspect }, 1);
    }

    tm_renderer_shader_info_t shader_info;

    static const tm_renderer_draw_call_info_t draw_call = {
        .primitive_type = TM_RENDERER_PRIMITIVE_TYPE_TRIANGLE_LIST,
        .draw_type = TM_RENDERER_DRAW_TYPE_NON_INDEXED,
        .non_indexed.first_vertex = 0,
        .non_indexed.num_vertices = 3,
        .non_indexed.num_instances = 1,
    };

    if(tm_shader_api->assemble_shader_infos(cdata->manager->render_sky_ray_march, 0, 0, tm_render_graph_execute_api->shader_context(graph_execute), (tm_strhash_t){0}, res_buf, &cbufs[0], &rbinders[0], 1, &shader_info)) {
        tm_renderer_api->tm_renderer_command_buffer_api->draw_calls(cmd_buf, &sort_key, &draw_call, &shader_info, 1);
    }

    tm_shader_api->destroy_constant_buffer_instances(io, cbufs, n_cbufs);
    tm_shader_api->destroy_resource_binder_instances(io, rbinders, 1);

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
}

static tm_render_graph_module_o *create_sky_atmospheric_module(tm_component_manager_o *manager)
{
    tm_render_graph_module_o *mod = tm_render_graph_module_api->create(&manager->allocator, "Sky Atmosphere");

    const tm_renderer_image_desc_t transmittance_desc = {
        .type = TM_RENDERER_IMAGE_TYPE_2D,
        .usage_flags = TM_RENDERER_IMAGE_USAGE_UAV,
        .format = tm_buffer_format_api->encode_uncompressed_format(TM_BUFFER_COMPONENT_TYPE_FLOAT, true, 32, 32, 32, 32),
        .width = manager->TRANSMITTANCE_TEXTURE_WIDTH,
        .height = manager->TRANSMITTANCE_TEXTURE_HEIGHT,
        .depth = 1,
        .mip_levels = 1,
        .layer_count = 1,
        .sample_count = 1,
        .debug_tag = "TRANSMITTANCE LUT"
    };

    const tm_renderer_image_desc_t multi_scattering_desc = {
        .type = TM_RENDERER_IMAGE_TYPE_2D,
        .usage_flags = TM_RENDERER_IMAGE_USAGE_UAV,
        .format = tm_buffer_format_api->encode_uncompressed_format(TM_BUFFER_COMPONENT_TYPE_FLOAT, true, 16, 16, 16, 16),
        .width = manager->MULTI_SCATTERING_TEXTURE_WIDTH,
        .height = manager->MULTI_SCATTERING_TEXTURE_HEIGHT,
        .depth = 1,
        .mip_levels = 1,
        .layer_count = 1,
        .sample_count = 1,
        .debug_tag = "Multiple Scattering LUT"
    };

    const tm_renderer_image_desc_t sky_view_desc= {
        .type = TM_RENDERER_IMAGE_TYPE_2D,
        .usage_flags = TM_RENDERER_IMAGE_USAGE_RENDER_TARGET,
        .format = tm_buffer_format_api->encode_uncompressed_format(TM_BUFFER_COMPONENT_TYPE_FLOAT, true, 16, 16, 16, 16),
        .width = 192,
        .height = 108,
        .depth = 1,
        .mip_levels = 1,
        .layer_count = 1,
        .sample_count = 1,
        .debug_tag = "Sky View LUT"
    };

    tm_render_graph_module_api->create_persistent_gpu_image(mod, TM_ATMOSPHERIC_SKY_TRANSMITTANCE, &transmittance_desc, (tm_strhash_t){0});

    tm_render_graph_module_api->create_persistent_gpu_image(mod, TM_ATMOSPHERIC_SKY_MULTI_SCATTERING, &multi_scattering_desc, (tm_strhash_t){0});

    tm_render_graph_module_api->create_persistent_gpu_image(mod, TM_ATMOSPHERIC_SKY_SKYVIEW_LUT, &sky_view_desc, (tm_strhash_t){0});

    static struct tm_render_graph_pass_api compute_transmittance_api = {
        .setup_pass = compute_transmittance_setup_pass,
        .execute_pass = compute_transmittance_execute_pass
    };
    static struct tm_render_graph_pass_api compute_multiple_scattering_api = {
        .setup_pass = compute_multi_scattering_setup_pass,
        .execute_pass = compute_multi_scattering_execute_pass
    };

    static struct tm_render_graph_pass_api render_sky_lut_api = {
        .setup_pass = render_sky_lut_setup_pass,
        .execute_pass = render_sky_lut_execute_pass 
    };

    static struct tm_render_graph_pass_api render_sky_ray_march_api = {
        .setup_pass = render_sky_raymarch_setup_pass,
        .execute_pass = render_sky_raymarch_execute_pass 
    };


    tm_render_graph_module_api->add_pass(mod, &(tm_render_graph_pass_i){ .api = compute_transmittance_api, .profiling_scope = "Compute Transmittance", .const_data = &(tm_const_data_t){ .manager = manager }, .const_data_size = sizeof(tm_const_data_t), .runtime_data_size = sizeof(lut_module_runtime_data_t) });

    tm_render_graph_module_api->add_pass(mod, &(tm_render_graph_pass_i){ .api = compute_multiple_scattering_api, .profiling_scope = "Compute Multiple Scattering", .const_data = &(tm_const_data_t){ .manager = manager }, .const_data_size = sizeof(tm_const_data_t), .runtime_data_size = sizeof(lut_module_runtime_data_t) });

    tm_render_graph_module_api->add_pass(mod, &(tm_render_graph_pass_i){ .api = render_sky_lut_api, .profiling_scope = "Render Sky LUT", .const_data = &(tm_const_data_t){ .manager = manager }, .const_data_size = sizeof(tm_const_data_t), .runtime_data_size = sizeof(lut_module_runtime_data_t) });

    tm_render_graph_module_api->add_pass(mod, &(tm_render_graph_pass_i){ .api = render_sky_ray_march_api, .profiling_scope = "Render Sky Ray March", .const_data = &(tm_const_data_t){ .manager = manager }, .const_data_size = sizeof(tm_const_data_t), .runtime_data_size = sizeof(lut_module_runtime_data_t) });

    return mod;
}

static void component__destroy(tm_component_manager_o *manager)
{
    uint32_t num_backends;
    tm_renderer_backend_i *backend = (tm_renderer_backend_i *)(*tm_global_api_registry->implementations(TM_RENDER_BACKEND_INTERFACE_NAME, &num_backends));

    tm_renderer_resource_command_buffer_o *res_buf;
    backend->create_resource_command_buffers(backend->inst, &res_buf, 1);

    tm_render_graph_module_api->destroy(manager->atmospheric_sky_module, res_buf);

    tm_entity_context_o *ctx = manager->ctx;
    tm_allocator_i a = manager->allocator;
    tm_free(&a, manager, sizeof(*manager));
    tm_entity_api->destroy_child_allocator(ctx, &a);
}

static void component__create(struct tm_entity_context_o* ctx)
{
    uint32_t num_backends;
    tm_renderer_backend_i **backends = (tm_renderer_backend_i **)tm_global_api_registry->implementations(TM_RENDER_BACKEND_INTERFACE_NAME, &num_backends);
    if (num_backends == 0) 
        return;

    tm_renderer_backend_i *backend = *backends;

    tm_allocator_i a;
    tm_entity_api->create_child_allocator(ctx, TM_TT_TYPE__ATMOSPHERIC_SKY_COMPONENT, &a);
    tm_component_manager_o *manager = tm_alloc(&a, sizeof(*manager));
    *manager = (tm_component_manager_o){
        .ctx = ctx,
            .rb = backend,
            .tt = tm_entity_api->the_truth(ctx),
            .allocator = a,
            .TRANSMITTANCE_TEXTURE_WIDTH = 256,
            .TRANSMITTANCE_TEXTURE_HEIGHT = 64,
            .MULTI_SCATTERING_TEXTURE_WIDTH = 32,
            .MULTI_SCATTERING_TEXTURE_HEIGHT = 32,
            .atmosphere = &default_values,
            .active = false,
            .lut_computed = false,
            .version = UINT64_MAX - 1
    };
    manager->atmospheric_sky_module = create_sky_atmospheric_module(manager);
    manager->atmospheric_sky_type = tm_the_truth_api->object_type_from_name_hash(manager->tt, TM_TT_TYPE_HASH__ATMOSPHERIC_SKY_COMPONENT);

    tm_component_i component = {
        .name = TM_TT_TYPE__ATMOSPHERIC_SKY_COMPONENT,
        .bytes = sizeof(struct tm_atmospheric_sky_component_t),
        .manager = (tm_component_manager_o *)manager,
        .destroy = component__destroy,
        .load_asset = component__load_asset,
    };

    tm_entity_api->register_component(ctx, &component);
}

static void ci__graph_module_inject(struct tm_component_manager_o *manager, struct tm_render_graph_module_o *module)
{
    tm_render_graph_module_api->insert_extension(module, TM_DEFAULT_RENDER_PIPE_MAIN_EXTENSION_SKYDOME, manager->atmospheric_sky_module, 0.0f);
}

static void ci__init(struct tm_component_manager_o *manager, const union tm_entity_t *entities, const uint32_t *entity_indices,
        void **shader_component_data, uint32_t num_shader_datas)
{
    manager->active = num_shader_datas > 0;
    if (!manager->active)
        return;

    const uint64_t version = tm_the_truth_api->changed_objects(manager->tt, manager->atmospheric_sky_type, UINT64_MAX, 0).version;
    manager->lut_computed = manager->version == version;
}

static void ci__update(struct tm_component_manager_o *manager, struct tm_render_args_t *args, const union tm_entity_t *entities,
        const struct tm_transform_component_t *entity_transforms, const uint32_t *entity_indices,
        void **component_data, uint32_t num_components, const uint8_t *frustum_visibility)
{
    manager->active = num_components > 0;
    if (!manager->active)
        return;
    tm_atmospheric_sky_component_t **cdata = (tm_atmospheric_sky_component_t **)component_data;

    const uint64_t version = tm_the_truth_api->changed_objects(manager->tt, manager->atmospheric_sky_type, UINT64_MAX, 0).version;
    manager->lut_computed = manager->version == version;
    manager->version = version;
    if (num_components > 0) {
        tm_transform_t t = entity_transforms[entity_indices[0]].world;
        manager->atmosphere = cdata[0];
        manager->atmosphere->celestial_body_center = t.pos;
    }

}



static float properties_ui(struct tm_properties_ui_args_t *args, tm_rect_t item_rect,
        tm_tt_id_t object, uint32_t indent)
{
    const tm_the_truth_object_o *asset_obj = tm_the_truth_api->read(args->tt, object);

    const tm_tt_id_t sun_direction = tm_the_truth_api->get_subobject(args->tt, asset_obj, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__SUN_DIRECTION);
    item_rect.y = tm_properties_view_api->ui_rotation(args, item_rect, "Sun Direction", NULL, sun_direction);

    item_rect.y = tm_properties_view_api->ui_float(args, item_rect, "Offset from Center", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__OFFSET_FROM_CENTER, NULL);

    item_rect.y = tm_properties_view_api->ui_float(args, item_rect, "Sun Angular Radius", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__SUN_ANGULAR_RADIUS, NULL);

    item_rect.y = tm_properties_view_api->ui_float(args, item_rect, "Bottom Radius", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__BOTTOM_RADIUS, NULL);

    item_rect.y = tm_properties_view_api->ui_float(args, item_rect, "Top Radius", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__TOP_RADIUS, NULL);

    const tm_tt_id_t global_iluminance = tm_the_truth_api->get_subobject(args->tt, asset_obj, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__GLOBAL_ILUMINANCE);
    item_rect.y = tm_properties_view_api->ui_vec3(args, item_rect, "Global Iluminance", NULL, global_iluminance);

    const tm_tt_id_t rayleigh_scattering = tm_the_truth_api->get_subobject(args->tt, asset_obj, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_SCATTERING);
    item_rect.y = tm_properties_view_api->ui_vec3(args, item_rect, "Rayleigh Scattering", NULL, rayleigh_scattering);

    bool expanded = false;
    item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Rayleigh Density Profile", NULL, object, indent, true, &expanded);
    if (expanded) {
        uint32_t child_indent = 1;
        bool layer0 = false;
        item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Layer 0", NULL, object, indent + child_indent, true, &layer0);
        if (layer0) {
            tm_rect_t label_r = tm_rect_split_left(item_rect, args->metrics[TM_PROPERTIES_METRIC_GROUP_LABEL_LEFT_MARGIN] + (child_indent + indent) * args->metrics[TM_PROPERTIES_METRIC_INDENT], 0, 1);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Width", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_WIDTH, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_EXP_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Scale", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_EXP_SCALE, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Linear Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_LINEAR_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Constant Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_CONSTANT_TERM, NULL);
            item_rect.y = label_r.y;
        }

        bool layer1 = false;
        item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Layer 1", NULL, object, indent + child_indent, true, &layer1);
        if (layer1) {
            tm_rect_t label_r = tm_rect_split_left(item_rect, args->metrics[TM_PROPERTIES_METRIC_GROUP_LABEL_LEFT_MARGIN] + (child_indent + indent) * args->metrics[TM_PROPERTIES_METRIC_INDENT], 0, 1);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Width", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_WIDTH, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_EXP_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Scale", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_EXP_SCALE, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Linear Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_LINEAR_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Constant Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_CONSTANT_TERM, NULL);
            item_rect.y = label_r.y;
        }
    }

    const tm_tt_id_t mie_scattering = tm_the_truth_api->get_subobject(args->tt, asset_obj, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_SCATTERING);
    item_rect.y = tm_properties_view_api->ui_vec3(args, item_rect, "Mie Scattering", NULL, mie_scattering);

    const tm_tt_id_t mie_extinction = tm_the_truth_api->get_subobject(args->tt, asset_obj, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_EXTINCTION);
    item_rect.y = tm_properties_view_api->ui_vec3(args, item_rect, "Mie Extinction", NULL, mie_extinction);

    const tm_tt_id_t mie_absorption = tm_the_truth_api->get_subobject(args->tt, asset_obj, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_ABSORPTION);
    item_rect.y = tm_properties_view_api->ui_vec3(args, item_rect, "Mie Absorption", NULL, mie_absorption);

    item_rect.y = tm_properties_view_api->ui_float(args, item_rect, "Mie Phase Function G", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_PHASE_FUNCTION_G, NULL);


    item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Mie Density Profile", NULL, object, indent, true, &expanded);
    if (expanded) {
        uint32_t child_indent = 1;
        bool layer0 = false;
        item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Layer 0", NULL, object, indent + child_indent, true, &layer0);
        if (layer0) {
            tm_rect_t label_r = tm_rect_split_left(item_rect, args->metrics[TM_PROPERTIES_METRIC_GROUP_LABEL_LEFT_MARGIN] + (child_indent + indent) * args->metrics[TM_PROPERTIES_METRIC_INDENT], 0, 1);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Width", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_WIDTH, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_EXP_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Scale", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_EXP_SCALE, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Linear Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_LINEAR_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Constant Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_CONSTANT_TERM, NULL);
            item_rect.y = label_r.y;
        }

        bool layer1 = false;
        item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Layer 1", NULL, object, indent + child_indent, true, &layer1);
        if (layer1) {
            tm_rect_t label_r = tm_rect_split_left(item_rect, args->metrics[TM_PROPERTIES_METRIC_GROUP_LABEL_LEFT_MARGIN] + (child_indent + indent) * args->metrics[TM_PROPERTIES_METRIC_INDENT], 0, 1);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Width", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_WIDTH, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_EXP_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Scale", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_EXP_SCALE, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Linear Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_LINEAR_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Constant Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_CONSTANT_TERM, NULL);
            item_rect.y = label_r.y;
        }
    }


    const tm_tt_id_t absorption_extinction = tm_the_truth_api->get_subobject(args->tt, asset_obj, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_EXTINCTION);
    item_rect.y = tm_properties_view_api->ui_vec3(args, item_rect, "Absorption Extinction", NULL, absorption_extinction);



    item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Absorption Density Profile", NULL, object, indent, true, &expanded);
    if (expanded) {
        uint32_t child_indent = 1;
        bool layer0 = false;
        item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Layer 0", NULL, object, indent + child_indent, true, &layer0);
        if (layer0) {
            tm_rect_t label_r = tm_rect_split_left(item_rect, args->metrics[TM_PROPERTIES_METRIC_GROUP_LABEL_LEFT_MARGIN] + (child_indent + indent) * args->metrics[TM_PROPERTIES_METRIC_INDENT], 0, 1);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Width", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_WIDTH, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_EXP_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Scale", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_EXP_SCALE, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Linear Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_LINEAR_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Constant Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_CONSTANT_TERM, NULL);
            item_rect.y = label_r.y;
        }

        bool layer1 = false;
        item_rect.y = tm_properties_view_api->ui_group(args, item_rect, "Layer 1", NULL, object, indent + child_indent, true, &layer1);
        if (layer1) {
            tm_rect_t label_r = tm_rect_split_left(item_rect, args->metrics[TM_PROPERTIES_METRIC_GROUP_LABEL_LEFT_MARGIN] + (child_indent + indent) * args->metrics[TM_PROPERTIES_METRIC_INDENT], 0, 1);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Width", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_WIDTH, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_EXP_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Exponential Scale", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_EXP_SCALE, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Linear Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_LINEAR_TERM, NULL);
            label_r.y = tm_properties_view_api->ui_float(args, label_r, "Constant Term", NULL, object, TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_CONSTANT_TERM, NULL);
            item_rect.y = label_r.y;
        }
    }


    return item_rect.y;
}

static const char *get_type_display_name(void)
{
    return "Atmospheric Sky Component";
}

static tm_ci_shader_i *shader_aspect = &(tm_ci_shader_i){
    .init = ci__init,
        .graph_module_inject = ci__graph_module_inject,
        .update = ci__update
};

static tm_properties_aspect_i *properties_aspect = &(tm_properties_aspect_i){
    .custom_ui = properties_ui,
        .get_type_display_name = get_type_display_name,
};



TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{

    tm_global_api_registry = reg;
    tm_allocator_api = reg->get(TM_ALLOCATOR_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);
    tm_the_truth_common_types_api = reg->get(TM_THE_TRUTH_COMMON_TYPES_API_NAME);
    tm_localizer_api = reg->get(TM_LOCALIZER_API_NAME);
    tm_render_graph_module_api = reg->get(TM_RENDER_GRAPH_MODULE_API_NAME);
    tm_render_graph_toolbox_api = reg->get(TM_RENDER_GRAPH_TOOLBOX_API_NAME);
    tm_render_graph_setup_api = reg->get(TM_RENDER_GRAPH_SETUP_API_NAME);
    tm_properties_view_api = reg->get(TM_PROPERTIES_VIEW_API_NAME);
    tm_buffer_format_api = reg->get(TM_BUFFER_FORMAT_API_NAME);
    tm_shader_repository_api = reg->get(TM_SHADER_REPOSITORY_API_NAME);
    tm_renderer_api = reg->get(TM_RENDERER_API_NAME);
    tm_render_graph_execute_api = reg->get(TM_RENDER_GRAPH_EXECUTE_API_NAME);
    tm_temp_allocator_api = reg->get(TM_TEMP_ALLOCATOR_API_NAME);
    tm_shader_api = reg->get(TM_SHADER_API_NAME);
    tm_logger_api = reg->get(TM_LOGGER_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, truth__create_types);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, component__create);
}
