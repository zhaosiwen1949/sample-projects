depth_stencil_states : {
    depth_test_enable : true
    depth_write_enable : false
    depth_compare_op : "greater_equal"
}

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
            { -1,  1,  0, 1 },
            {  3,  1,  0, 1 },
            { -1, -3,  0, 1 },
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
        Texture2D<float4> view_depht_texture = get_depth_buffer_texture();
        float2 near_far = load_camera_near_far();
        float depth = view_depht_texture.Load(int3(input.position.xy, 0)).x;

        float2 pix_pos = input.position.xy;
        tm_atmosphere_parameters_t atmosphere = get_atmosphere_parameters();

        float3 clip_space = float3((pix_pos / load_render_target_resolution()) * float2(2.0, -2.0) - float2(1.0, -1.0), 1.0);
        float4 h_pos = mul(float4(clip_space, 1.0), load_camera_inverse_view_projection());

        float3 world_dir = normalize(h_pos.xyz / h_pos.w - load_camera_position());
        /*float3 from_center_dir = normalize(load_camera_position() - load_celestial_body_center());*/
        float3 from_center_dir = float3(0.0, 1.0, 0.0);
        float3 world_pos = load_camera_position() + from_center_dir * load_offset_from_center();
        float3 sun_direction = load_sun_direction();

        float depth_buffer_value = -1.0;
        float view_height = length(world_pos - load_celestial_body_center());
        float3 L = float3(0.0, 0.0, 0.0);
        depth_buffer_value = depth;

#if defined(FASTSKY_ENABLED)
        if (view_height < atmosphere.top_radius)
        {
            float2 uv;
            float3 up_vector = (world_pos - load_celestial_body_center()) / view_height;
            float view_zenith_cos_angle = dot(world_dir, up_vector);

            float3 side_vector = normalize(cross(up_vector, world_dir));		// assumes non parallel vectors
            float3 forward_vector = normalize(cross(side_vector, up_vector));	// aligns toward the sun light but perpendicular to up vector
            float2 light_on_plane = float2(dot(sun_direction, forward_vector), dot(sun_direction, side_vector));
            light_on_plane = normalize(light_on_plane);
            float light_view_cos_angle = light_on_plane.x;

            bool intersect_ground = ray_sphere_intersect_nearest(world_pos, world_dir, load_celestial_body_center(), atmosphere.bottom_radius) >= 0.0f;

            sky_view_lut_params_to_uv(atmosphere, intersect_ground, view_zenith_cos_angle, light_view_cos_angle, view_height, uv);

            Texture2D<float4> sky_view_lut_texture = get_sky_view_lut_texture();
            SamplerState samp = get_clamp_linear();
            output.color = float4(sky_view_lut_texture.Sample(samp, uv).rgb + get_sun_luminance(world_pos, world_dir, atmosphere.bottom_radius), 1.0);
            return output;
        }
#else
        if (depth_buffer_value >= near_far.y)
            L += get_sun_luminance(world_pos, world_dir, atmosphere.bottom_radius);
#endif

#if defined(FASTAERIALPERSPECTIVE_ENABLED)

        /*clip_space = float3((pix_pos / load_render_target_resolution())*float2(2.0, -2.0) - float2(1.0, -1.0), depth_buffer_value);*/
        /*float4 depth_buffer_world_pos = mul(float4(clip_space, 1.0), load_camera_inverse_view_projection());*/
        /*depth_buffer_world_pos /= depth_buffer_world_pos.w;*/
        /*float t_depth = length(depth_buffer_world_pos.xyz - (world_pos + float3(0.0, -atmosphere.bottom_radius, 0.0)));*/
        /*float slice = aerial_perspective_depth_to_slice(t_depth);*/
        /*float Weight = 1.0;*/
        /*if (slice < 0.5)*/
        /*{*/
            /*// We multiply by weight to fade to 0 at depth 0. That works for luminance and opacity.*/
            /*Weight = saturate(slice * 2.0);*/
            /*slice = 0.5;*/
        /*}*/
        /*float w = sqrt(slice / AP_SLICE_COUNT);	// squared distribution*/

        /*const float4 AP = Weight * AtmosphereCameraScatteringVolume.SampleLevel(samplerLinearClamp, float3(pix_pos / float2(load_render_target_resolution()), w), 0);*/
        /*L.rgb += AP.rgb;*/
        /*float Opacity = AP.a;*/

        /*output.Luminance = float4(L, Opacity);*/
        //output.Luminance *= frac(clamp(w*AP_SLICE_COUNT, 0, AP_SLICE_COUNT));
#else // FASTAERIALPERSPECTIVE_ENABLED

        // Move to top atmosphere as the starting point for ray marching.
        // This is critical to be after the above to not disrupt above atmosphere tests and voxel selection.
        if (!move_to_top_atmosphere(world_pos, world_dir, atmosphere.top_radius))
        {
            // Ray is not intersecting the atmosphere		
            output.color = float4(get_sun_luminance(world_pos, world_dir, atmosphere.bottom_radius), 1.0);
            return output;
        }

        const bool ground = true;
        const float sample_count_ini = 0.0f;
        const bool variable_sample_count = true;
        const bool mie_rayleigh_phase = true;
        single_scattering_result_t ss = integrate_scattered_luminance(pix_pos, world_pos, world_dir, sun_direction, atmosphere, ground, sample_count_ini, depth_buffer_value, variable_sample_count, mie_rayleigh_phase);

        L += ss.L;
        float3 throughput = ss.transmittance;

        const float transmittance = dot(throughput, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f));
        output.color = float4(L, 1.0);

#endif // FASTAERIALPERSPECTIVE_ENABLED

        return output;

    ]]
}

compile : {
    includes : [ "ray_march_defines", "atmospheric_sky_common", "transmittance_common", "atmospheric_sky_render_common" ]

    variations : [
        { systems : [ "viewer_system" ] }
    ]
}



