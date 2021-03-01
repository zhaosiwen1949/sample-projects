samplers : {
    clamp_linear : {
        anisotropy_enable: false
        min_filter: "linear"
        max_filter: "linear"
        mip_mode: "linear"
        address_u: "clamp"
        address_v: "clamp"
        address_w: "clamp"
    }
}


imports : [
    { name: "clamp_linear" type: "sampler" sampler: "clamp_linear" }
]

vertex_shader : {
    import_system_semantics : [ "vertex_id" ]
    exports : [
        { name: "uv" type: "float2" }
    ]

    code : [[
        static const float4 pos[3] = {
            { -1,  1,  1, 1 },
            {  3,  1,  1, 1 },
            { -1, -3,  1, 1 },
        };
    
        output.position = pos[vertex_id];

        static const float2 uvs[3] = {
            { 0, 0 },
            { 2, 0 },
            { 0, 2 },
        };
        output.uv = uvs[vertex_id];

        return output;
    ]]
}


pixel_shader : {
    exports : [
        { name : "color" type: "float4" }
    ]

    code : [[
        float2 pix_pos = input.position.xy;
        tm_atmosphere_parameters_t atmosphere = get_atmosphere_parameters();

        float3 clip_space = float3((pix_pos / load_render_target_resolution())*float2(2.0, -2.0) - float2(1.0, -1.0), 1.0);
        float4 h_pos = mul(float4(clip_space, 1.0), load_camera_inverse_view_projection());

        float3 world_dir = normalize(h_pos.xyz / h_pos.w - load_camera_position());
        float3 from_center_dir = normalize(load_camera_position() - load_celestial_body_center());
        /*float3 from_center_dir = float3(0.0, 1.0, 1.0);*/
        float3 world_pos = load_camera_position() + from_center_dir * load_offset_from_center();
        float3 sun_direction = load_sun_direction();


        float2 uv = input.uv;;

        float view_height = length(world_pos - load_celestial_body_center());

        float view_zenith_cos_angle;
        float light_view_cos_angle;
        uv_to_sky_view_lut_param(atmosphere, view_zenith_cos_angle, light_view_cos_angle, view_height, uv);

        float3 sun_dir;
        {
            float3 up_vector = (world_pos - load_celestial_body_center()) / view_height;
            float sun_zenith_cos_angle = dot(up_vector, sun_direction);
            sun_dir = normalize(float3(0.0f, sun_zenith_cos_angle, sqrt(1.0 - sun_zenith_cos_angle * sun_zenith_cos_angle)));
        }
        /*sun_dir = sun_direction;*/

        world_pos = float3(0.0, view_height, 0.0);
        float view_zenith_sin_angle = sqrt(1 - view_zenith_cos_angle * view_zenith_cos_angle);

        world_dir.x = view_zenith_sin_angle * sqrt(1.0 - light_view_cos_angle * light_view_cos_angle);
        world_dir.y = view_zenith_cos_angle;
        world_dir.z = view_zenith_sin_angle * light_view_cos_angle;


        // Move to top atmospehre
        if (!move_to_top_atmosphere(world_pos, world_dir, atmosphere.top_radius))
        {
            // Ray is not intersecting the atmosphere
            output.color = float4(0.0, 0.0, 0.0, 1.0);
            return output;
        }

        const bool ground = false;
        const float sample_count_ini = 30;
        const float depth_buffer_value = -1.0;
        const bool variable_sample_count = true;
        const bool mie_rayleigh_phase = true;
        single_scattering_result_t ss = integrate_scattered_luminance(pix_pos, world_pos, world_dir, sun_dir, atmosphere, ground, sample_count_ini, depth_buffer_value, variable_sample_count, mie_rayleigh_phase);

        float3 L = ss.L;

        output.color = float4(ss.L, 1.0);

        return output;

    ]]
}

compile : {
    includes : [ "ray_march_defines", "atmospheric_sky_common", "transmittance_common", "atmospheric_sky_render_common" ]

    variations : [
        { systems : [ "viewer_system" ] }
    ]
}
