imports : [
    { name : "transmittance_texture" type : "texture_2d" element_type: "float4" uav : true }
]

compute_shader : {
    import_system_semantics: [ "dispatch_thread_id", "group_index" ]

    attributes : {
        num_threads: [8, 8, 1]
    }

    code : [[
        uint2 st = dispatch_thread_id.xy;
        RWTexture2D<float4> image_data = get_transmittance_texture();

        uint2 res = float2(load_TRANSMITTANCE_TEXTURE_WIDTH(), load_TRANSMITTANCE_TEXTURE_HEIGHT());
        if (st.x >= res.x || st.y >= res.y)
            return;

        tm_atmosphere_parameters_t atmosphere = get_atmosphere_parameters();
        float2 frag_coord = float2(st);
        float4 color = float4(compute_transmittance_to_top_atmosphere_boundary_texture(atmosphere, frag_coord), 1.0);
        
        image_data[st] = color;
    ]]
}

compile : {
    includes : [ "atmospheric_sky_common", "transmittance_common" ]
}
