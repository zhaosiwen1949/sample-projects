#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/buffer_format.h>
#include <foundation/log.h>
#include <foundation/macros.h>
#include <foundation/math.inl>
#include <foundation/the_truth.h>

#include <plugins/entity/entity.h>
#include <plugins/default_render_pipe/default_render_pipe.h>
#include <plugins/render_graph/render_graph.h>
#include <plugins/render_graph_toolbox/toolbox_common.h>
#include <plugins/renderer/commands.h>
#include <plugins/renderer/render_backend.h>
#include <plugins/renderer/render_command_buffer.h>
#include <plugins/renderer/renderer.h>
#include <plugins/renderer/resources.h>
#include <plugins/shader_system/shader_system.h>
#include <plugins/the_machinery_shared/component_interfaces/shader_interface.h>

#include <string.h>

#define TM_TT_TYPE__RAY_TRACING_TEST "tm_default_render_pipe_ray_tracing_test"
#define TM_RAY_TRACING_TEST_OUTPUT_IMAGE TM_STATIC_HASH("tm_ray_tracing_test__output", 0xf3474cf604a4b868ULL)

static struct tm_api_registry_api *tm_api_registry_api;
static struct tm_buffer_format_api *tm_buffer_format_api;
static struct tm_entity_api *tm_entity_api;
static struct tm_logger_api *tm_logger_api;
static struct tm_render_graph_execute_api *tm_render_graph_execute_api;
static struct tm_render_graph_module_api *tm_render_graph_module_api;
static struct tm_render_graph_setup_api *tm_render_graph_setup_api;
static struct tm_render_graph_toolbox_api *tm_render_graph_toolbox_api;
static struct tm_renderer_api *tm_renderer_api;
static struct tm_shader_api *tm_shader_api;
static struct tm_shader_repository_api *tm_shader_repository_api;
static struct tm_the_truth_api *tm_the_truth_api;

typedef struct tm_component_manager_o
{
    tm_entity_context_o *ctx;
    tm_allocator_i allocator;
    tm_renderer_backend_i *backend;

    tm_render_graph_module_o *test_module;

    tm_renderer_handle_t vertex_buffer_handle;
    tm_renderer_handle_t blas_handle;
    tm_renderer_handle_t pipeline_handle;
    tm_renderer_handle_t sbt_raygen_handle;
    tm_renderer_handle_t sbt_hit_handle;
    tm_renderer_handle_t sbt_miss_handle;
    tm_renderer_handle_t tlas_handle;
    TM_PAD(4);
} tm_component_manager_o;

typedef struct tm_module_runtime_data_o
{
    tm_shader_o *shaders[3];
    tm_render_graph_handle_t output_handle;
    uint32_t group_count[2];
    TM_PAD(4);
} tm_module_runtime_data_o;

static void shader_ci__graph_module_inject(tm_component_manager_o *manager, tm_render_graph_module_o *mod)
{
    tm_render_graph_module_api->insert_extension(mod, TM_DEFAULT_RENDER_PIPE_MAIN_EXTENSION_DEBUG_VISUALIZATION, manager->test_module, 0.0f);
}

static void component__create_truth_types(struct tm_the_truth_o *tt)
{
    uint32_t num_backends;
    tm_renderer_backend_i *backend = tm_api_registry_api->implementations(TM_RENDER_BACKEND_INTERFACE_NAME, &num_backends)[0];
    if (!backend->supports_ray_tracing(backend->inst, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL))
        return;

    static tm_ci_shader_i shader_aspect = {
        .graph_module_inject = shader_ci__graph_module_inject
    };

    const uint64_t component_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__RAY_TRACING_TEST, 0, 0);
    tm_the_truth_api->set_aspect(tt, component_type, TM_CI_SHADER, &shader_aspect);
}

static void module__init_trace_pass(void *const_data, tm_allocator_i *allocator, tm_renderer_resource_command_buffer_o *res_buf)
{
    tm_component_manager_o *manager = *(tm_component_manager_o **)const_data;
    if (manager->vertex_buffer_handle.resource)
        return;

    const tm_vec3_t vertices[] = { { -0.75f, 0.5f, 15.0f }, { 0.0f, -0.5f, 15.0f }, { 0.75f, 0.5f, 15.0f } };
    const tm_renderer_buffer_desc_t vertex_buffer_desc = {
        .usage_flags = TM_RENDERER_BUFFER_USAGE_ACCELERATION_STRUCTURE,
        .size = sizeof(vertices),
        .debug_tag = "Ray Tracing Vertex Buffer"
    };

    void *vertex_data;
    manager->vertex_buffer_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->map_create_buffer(res_buf, &vertex_buffer_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL, 0, &vertex_data);
    memcpy(vertex_data, vertices, vertex_buffer_desc.size);

    const tm_renderer_geometry_desc_t geometry_desc = {
        .type = TM_RENDERER_GEOMETRY_TYPE_TRIANGLES,
        .flags = TM_RENDERER_GEOMETRY_OPAQUE,
        .triangle_desc = {
            .format = tm_buffer_format_api->encode_uncompressed_format(TM_BUFFER_COMPONENT_TYPE_FLOAT, true, 32, 32, 32, 0),
            .vertex_data = manager->vertex_buffer_handle,
            .vertex_stride = sizeof(tm_vec3_t),
            .vertex_count = TM_ARRAY_COUNT(vertices) }
    };

    const tm_renderer_bottom_level_acceleration_structure_desc_t blas_desc = {
        .build_flags = TM_RENDERER_ACCELERATION_STRUCTURE_BUILD_PREFER_FAST_TRACE,
        .geometry_desc_count = 1,
        .geometry_desc = &geometry_desc,
        .debug_tag = "Test BLAS"
    };

    manager->blas_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_bottom_level_acceleration_structure(res_buf, &blas_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);

    const tm_renderer_top_level_acceleration_structure_desc_t tlas_desc = {
        .build_flags = TM_RENDERER_ACCELERATION_STRUCTURE_BUILD_PREFER_FAST_TRACE,
        .num_instances = 1,
        .debug_tag = "Test TLAS",
        .instaces = &(tm_renderer_top_level_acceleration_structure_instance_t){
            .transform = *tm_mat44_identity(),
            .mask = 0xFF,
            .flags = TM_RENDERER_GEOMETRY_INSTANCE_DISABLE_TRIANGLE_CULL | TM_RENDERER_GEOMETRY_INSTANCE_OPAQUE,
            .blas_handle = manager->blas_handle
        }
    };

    manager->tlas_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_top_level_acceleration_structure(res_buf, &tlas_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
}

static void module__shutdown_trace_pass(void *const_data, tm_allocator_i *allocator, tm_renderer_resource_command_buffer_o *res_buf)
{
    tm_component_manager_o *manager = *(tm_component_manager_o **)const_data;
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->blas_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->vertex_buffer_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->tlas_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->sbt_raygen_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->sbt_hit_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->sbt_miss_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->pipeline_handle);
}

static void module__setup_trace_pass(const void *const_data, void *runtime_data, tm_render_graph_setup_o *graph_setup)
{
    const tm_component_manager_o *manager = *(tm_component_manager_o **)const_data;
    tm_module_runtime_data_o *rdata = runtime_data;

    if (manager->pipeline_handle.resource == 0) {
        tm_shader_repository_o *shader_repo = tm_render_graph_setup_api->shader_repository(graph_setup);
        rdata->shaders[0] = tm_shader_repository_api->lookup_shader(shader_repo, TM_STATIC_HASH("raygen", 0x5a7f3dc6adf96104ULL));
        rdata->shaders[1] = tm_shader_repository_api->lookup_shader(shader_repo, TM_STATIC_HASH("hit", 0x6f2598e77d07074cULL));
        rdata->shaders[2] = tm_shader_repository_api->lookup_shader(shader_repo, TM_STATIC_HASH("miss", 0x92070bf3352c5ce3ULL));
    }

    tm_render_graph_setup_api->set_active(graph_setup, true);

    tm_renderer_image_desc_t output_desc = *tm_render_graph_toolbox_api->image_desc(graph_setup, TM_DEFAULT_RENDER_PIPE_MAIN_OUTPUT_TARGET);
    output_desc.usage_flags = TM_RENDERER_IMAGE_USAGE_UAV;
    output_desc.debug_tag = "Test Output";
    tm_render_graph_setup_api->create_gpu_images(graph_setup, &output_desc, 1, &rdata->output_handle);
    tm_render_graph_setup_api->write_gpu_resource(graph_setup, rdata->output_handle, TM_RENDER_GRAPH_WRITE_BIND_FLAG_UAV, TM_RENDERER_RESOURCE_STATE_UAV | TM_RENDERER_RESOURCE_STATE_RAY_TRACING_SHADER, TM_RENDERER_RESOURCE_LOAD_OP_CLEAR, 0, TM_RAY_TRACING_TEST_OUTPUT_IMAGE);
    
    rdata->group_count[0] = output_desc.width;
    rdata->group_count[1] = output_desc.height;
}

static void module__execute_trace_pass(const void *const_data, void *runtime_data, uint64_t sort_key, tm_render_graph_execute_o *graph_execute)
{
    tm_component_manager_o *manager = *(tm_component_manager_o **)const_data;
    tm_module_runtime_data_o *rdata = runtime_data;

    if (manager->pipeline_handle.resource == 0) {
        const tm_shader_system_context_o *shader_ctx = tm_render_graph_execute_api->shader_context(graph_execute);
        tm_renderer_resource_command_buffer_o *res_buf = tm_render_graph_execute_api->default_resource_command_buffer(graph_execute);

        tm_shader_resource_binder_instance_t rbinder;
        tm_shader_io_o *io = tm_shader_api->shader_io(rdata->shaders[0]);
        tm_shader_api->create_resource_binder_instances(io, 1, &rbinder);

        uint32_t resource_slot;
        tm_shader_api->lookup_resource(io, TM_STATIC_HASH("tm_ray_tracing_test__scene", 0x6542aed4352649f6ULL), 0, &resource_slot);
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = rbinder.instance_id, .resource_slot = resource_slot, .num_resources = 1, .resources = &manager->tlas_handle }, 1);

        const tm_renderer_handle_t output_backend_handle = tm_render_graph_execute_api->backend_handle(graph_execute, rdata->output_handle);
        tm_shader_api->lookup_resource(io, TM_RAY_TRACING_TEST_OUTPUT_IMAGE, 0, &resource_slot);
        tm_shader_api->update_resources(io, res_buf, &(tm_shader_resource_update_t){ .instance_id = rbinder.instance_id, .resource_slot = resource_slot, .num_resources = 1, .resources = &output_backend_handle }, 1);

        tm_renderer_shader_info_t shader_infos[3];
        tm_shader_api->assemble_shader_infos(rdata->shaders[0], 0, 0, shader_ctx, 0, res_buf, 0, &rbinder, 1, shader_infos);
        tm_shader_api->assemble_shader_infos(rdata->shaders[1], 0, 0, shader_ctx, 0, res_buf, 0, 0, 1, shader_infos + 1);
        tm_shader_api->assemble_shader_infos(rdata->shaders[2], 0, 0, shader_ctx, 0, res_buf, 0, 0, 1, shader_infos + 2);
        tm_shader_api->destroy_resource_binder_instances(io, &rbinder, 1);

        const tm_renderer_ray_tracing_pipeline_desc_t pipeline_desc = {
            .max_recursion_depth = 1,
            .num_shaders = TM_ARRAY_COUNT(shader_infos),
            .shader_infos = shader_infos,
            .debug_tag = "Test Pipeline"
        };

        manager->pipeline_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_ray_tracing_pipeline(res_buf, &pipeline_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);

        tm_renderer_shader_binding_table_desc_t sbt_desc = {
            .pipeline = manager->pipeline_handle,
            .num_shader_infos = 1,
            .shader_infos = shader_infos,
            .debug_tag = "Test SBT"
        };

        manager->sbt_raygen_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_shader_binding_table(res_buf, &sbt_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
        ++sbt_desc.shader_infos;
        manager->sbt_hit_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_shader_binding_table(res_buf, &sbt_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
        ++sbt_desc.shader_infos;
        manager->sbt_miss_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_shader_binding_table(res_buf, &sbt_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    }

    const tm_renderer_trace_call_t trace_desc = {
        .pipeline = manager->pipeline_handle,
        .raygen_sbt = manager->sbt_raygen_handle,
        .miss_sbt = manager->sbt_miss_handle,
        .hit_sbt = manager->sbt_hit_handle,
        .group_count = { rdata->group_count[0], rdata->group_count[1], 1 }
    };

    tm_renderer_api->tm_renderer_command_buffer_api->trace_dispatches(tm_render_graph_execute_api->default_command_buffer(graph_execute), &sort_key, &trace_desc, 1);
}

static void component__manager_destroy(tm_component_manager_o *manager)
{
    tm_renderer_resource_command_buffer_o *res_buf;
    manager->backend->create_resource_command_buffers(manager->backend->inst, &res_buf, 1);
    tm_render_graph_module_api->destroy(manager->test_module, res_buf);
    manager->backend->submit_resource_command_buffers(manager->backend->inst, &res_buf, 1);
    manager->backend->destroy_resource_command_buffers(manager->backend->inst, &res_buf, 1);

    tm_entity_context_o *ctx = manager->ctx;
    tm_allocator_i allocator = manager->allocator;
    tm_free(&allocator, manager, sizeof(tm_component_manager_o));
    tm_entity_api->destroy_child_allocator(ctx, &allocator);
}

static void component__manager_create(tm_entity_context_o *ctx)
{
    uint32_t num_backends;
    tm_renderer_backend_i *backend = tm_api_registry_api->implementations(TM_RENDER_BACKEND_INTERFACE_NAME, &num_backends)[0];
    if (!backend->supports_ray_tracing(backend->inst, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL)) {
        tm_logger_api->print(TM_LOG_TYPE_ERROR, "Cannot run Hello Triangle example (ray tracing is not supported).");
        return;
    }

    tm_allocator_i allocator;
    tm_entity_api->create_child_allocator(ctx, TM_TT_TYPE__RAY_TRACING_TEST, &allocator);
    tm_component_manager_o *manager = tm_alloc(&allocator, sizeof(tm_component_manager_o));

    *manager = (tm_component_manager_o){
        .ctx = ctx,
        .allocator = allocator,
        .backend = backend
    };

    const tm_render_graph_pass_i pass_trace = {
        .api = { .init_pass = module__init_trace_pass, .shutdown_pass = module__shutdown_trace_pass, .setup_pass = module__setup_trace_pass, .execute_pass = module__execute_trace_pass },
        .const_data_size = sizeof(tm_component_manager_o **),
        .const_data = &manager,
        .runtime_data_size = sizeof(tm_module_runtime_data_o),
        .profiling_scope = "Trace"
    };

    tm_fullscreen_pass_setup_t pass_copy = {
        .input_slots[0].slot_name = TM_STATIC_HASH("texture", 0xcd4238c6a0c69e32ULL),
        .input_slots[0].resources[0].name = TM_RAY_TRACING_TEST_OUTPUT_IMAGE,
        .color_targets[0] = { .name = TM_DEFAULT_RENDER_PIPE_MAIN_OUTPUT_TARGET },
        .shader = TM_STATIC_HASH("copy_with_blend", 0x96cc5d5b7e68e12ULL)
    };

    manager->test_module = tm_render_graph_module_api->create(&allocator, "Ray Tracing Test");
    tm_render_graph_module_api->add_pass(manager->test_module, &pass_trace);
    tm_render_graph_toolbox_api->fullscreen_pass(manager->test_module, &pass_copy, "Copy");

    const tm_component_i component = {
        .name = TM_TT_TYPE__RAY_TRACING_TEST,
        .manager = manager,
        .destroy = component__manager_destroy
    };

    tm_entity_api->register_component(ctx, &component);
}

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api *reg, bool load)
{
    tm_api_registry_api = reg;
    tm_buffer_format_api = reg->get(TM_BUFFER_FORMAT_API_NAME);
    tm_entity_api = reg->get(TM_ENTITY_API_NAME);
    tm_logger_api = reg->get(TM_LOGGER_API_NAME);
    tm_render_graph_execute_api = reg->get(TM_RENDER_GRAPH_EXECUTE_API_NAME);
    tm_render_graph_module_api = reg->get(TM_RENDER_GRAPH_MODULE_API_NAME);
    tm_render_graph_setup_api = reg->get(TM_RENDER_GRAPH_SETUP_API_NAME);
    tm_render_graph_toolbox_api = reg->get(TM_RENDER_GRAPH_TOOLBOX_API_NAME);
    tm_renderer_api = reg->get(TM_RENDERER_API_NAME);
    tm_shader_api = reg->get(TM_SHADER_API_NAME);
    tm_shader_repository_api = reg->get(TM_SHADER_REPOSITORY_API_NAME);
    tm_the_truth_api = reg->get(TM_THE_TRUTH_API_NAME);

    tm_add_or_remove_implementation(reg, load, TM_THE_TRUTH_CREATE_TYPES_INTERFACE_NAME, component__create_truth_types);
    tm_add_or_remove_implementation(reg, load, TM_ENTITY_CREATE_COMPONENT_INTERFACE_NAME, component__manager_create);
}