
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
    /*float4 RenderTransmittanceLutPS(VertexOutput Input) : SV_TARGET*/
        float2 pix_pos = input.position.xy;
        tm_atmosphere_parameters_t atmosphere = get_atmosphere_parameters();

        // Compute camera position from LUT coords
        /*float2 uv = pix_pos / float2(load_TRANSMITTANCE_TEXTURE_WIDTH(), load_TRANSMITTANCE_TEXTURE_HEIGHT());*/
        float2 uv = input.uv;
        float view_height;
        float view_zenith_cos_angle;
        get_RMu_from_transmittance_texture_uv(atmosphere, uv, view_height, view_zenith_cos_angle);
        /*uv_to_lut_transmittance_params(atmosphere, uv, view_height, view_zenith_cos_angle);*/
        float2 uv2 = get_transmittance_texture_uv_from_RMu(atmosphere, view_height, view_zenith_cos_angle);

        //  A few extra needed constants
        float3 world_pos = float3(0.0f, view_height, 0.0);
        float3 world_dir = float3(0.0, view_zenith_cos_angle, sqrt(1.0 - view_zenith_cos_angle * view_zenith_cos_angle));
        float3 sun_direction = float3(0.0f, 1.0f, 0.0f);

        const bool ground = false;
        const float sample_count_ini = 40.0f;	// Can go a low as 10 sample but energy lost starts to be visible.
        const float depth_buffer_value = 0.0;
        const bool variable_sample_count = false;
        const bool mie_rayleigh_phase = false;
        float3 transmittance = exp(-integrate_scattered_luminance(pix_pos, world_pos, world_dir, sun_direction, atmosphere, ground, sample_count_ini, depth_buffer_value, variable_sample_count, mie_rayleigh_phase).optical_depth);

        // Opetical depth to transmittance
        /*output.color =  float4(uv2, 0.0f, 1.0f);*/
        Texture2D<float4> tex = get_multi_scattering_texture();
        SamplerState samp = get_clamp_linear();
        float4 org = tex.Sample(samp, input.uv);
        output.color = float4(50.0 * org.xyz, 1.0);
        /*output.color = float4(transmittance, 1.0);*/

        return output;
    ]]
}

compile : {
    includes : [ "atmospheric_sky_common", "transmittance_common", "atmospheric_sky_render_common" ]

    variations : [
        { systems : [ "viewer_system" ] }
    ]
}
