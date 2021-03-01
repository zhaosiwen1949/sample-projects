imports : [
    { name : "mult_scat_texture" type : "texture_2d" element_type: "float4" uav : true }
]

common : [[
groupshared float3 multi_scattering_as1_shared[64];
groupshared float3 l_shared[64];
]]

compute_shader : {
    import_system_semantics: [ "dispatch_thread_id", "group_index" ]

    attributes : {
        num_threads: [1, 1, 64]
    }

    code : [[
        float2 pix_pos = float2(dispatch_thread_id.xy) + 0.5f;
        RWTexture2D<float4> multi_scattering_texture = get_mult_scat_texture();
        uint2 res = float2(load_MULTI_SCATTERING_TEXTURE_WIDTH(), load_MULTI_SCATTERING_TEXTURE_HEIGHT());
        float2 uv = pix_pos / res;


        uv = float2(from_sub_uvs_to_unit(uv.x, res.x), from_sub_uvs_to_unit(uv.y, res.y));

        tm_atmosphere_parameters_t atmosphere = get_atmosphere_parameters();

        float cos_sun_zenith_angle = -(uv.x * 2.0 - 1.0);
        /*float cos_sun_zenith_angle = uv.x;*/
        float3 sun_dir = float3(0.0f, cos_sun_zenith_angle, sqrt(1.0 - cos_sun_zenith_angle * cos_sun_zenith_angle));
        // We adjust again view_height according to PLANET_RADIUS_OFFSET to be in a valid range.
        float view_height = atmosphere.bottom_radius + saturate(uv.y + PLANET_RADIUS_OFFSET) * (atmosphere.top_radius - atmosphere.bottom_radius - PLANET_RADIUS_OFFSET);

        float3 world_pos = float3(0.0f, view_height, 0.0f);
        float3 world_dir = float3(0.0, 1.0, 0.0);


        const bool ground = true;
        const float sample_count_ini = 20;// a minimum set of step is required for accuracy unfortunately
        const float depth_buffer_value = -1.0;
        const bool variable_sample_count = false;
        const bool mie_rayleigh_phase = false;

        const float sphere_solid_angle = 4.0 * PI;
        const float isotropic_angle = 1.0 / sphere_solid_angle;


        // Reference. Since there are many sample, it requires MULTI_SCATTERING_POWER_SERIE to be true for accuracy and to avoid divergences (see declaration for explanations)
#define SQRTSAMPLECOUNT 8
        const float sqrtSample = float(SQRTSAMPLECOUNT);
        float i = 0.5f + float(dispatch_thread_id.z / SQRTSAMPLECOUNT);
        float j = 0.5f + float(dispatch_thread_id.z - float((dispatch_thread_id.z / SQRTSAMPLECOUNT)*SQRTSAMPLECOUNT));
        {
            float randa_a = i / sqrtSample;
            float randa_b = j / sqrtSample;
            float theta = 2.0f * PI * randa_a;
            float phi = PI * randa_b;
            float cos_phi = cos(phi);
            float sin_phi = sin(phi);
            float cos_theta = cos(theta);
            float sin_theta = sin(theta);
            world_dir.x = sin_theta * sin_phi;
            world_dir.y = cos_phi;
            world_dir.z = cos_theta * sin_phi;
            single_scattering_result_t result = integrate_scattered_luminance(pix_pos, world_pos, world_dir, sun_dir, atmosphere, ground, sample_count_ini, depth_buffer_value, variable_sample_count, mie_rayleigh_phase);

            multi_scattering_as1_shared[dispatch_thread_id.z] = result.multiple_scattering_as1 * sphere_solid_angle / (sqrtSample * sqrtSample);
            l_shared[dispatch_thread_id.z] = result.L * sphere_solid_angle / (sqrtSample * sqrtSample);
        }
#undef SQRTSAMPLECOUNT

        GroupMemoryBarrierWithGroupSync();

        // 64 to 32
        if (dispatch_thread_id.z < 32)
        {
            multi_scattering_as1_shared[dispatch_thread_id.z] += multi_scattering_as1_shared[dispatch_thread_id.z + 32];
            l_shared[dispatch_thread_id.z] += l_shared[dispatch_thread_id.z + 32];
        }
        GroupMemoryBarrierWithGroupSync();

        // 32 to 16
        if (dispatch_thread_id.z < 16)
        {
            multi_scattering_as1_shared[dispatch_thread_id.z] += multi_scattering_as1_shared[dispatch_thread_id.z + 16];
            l_shared[dispatch_thread_id.z] += l_shared[dispatch_thread_id.z + 16];
        }
        GroupMemoryBarrierWithGroupSync();

        // 16 to 8 (16 is thread group min hardware size with intel, no sync required from there)
        if (dispatch_thread_id.z < 8)
        {
            multi_scattering_as1_shared[dispatch_thread_id.z] += multi_scattering_as1_shared[dispatch_thread_id.z + 8];
            l_shared[dispatch_thread_id.z] += l_shared[dispatch_thread_id.z + 8];
        }
        GroupMemoryBarrierWithGroupSync();
        if (dispatch_thread_id.z < 4)
        {
            multi_scattering_as1_shared[dispatch_thread_id.z] += multi_scattering_as1_shared[dispatch_thread_id.z + 4];
            l_shared[dispatch_thread_id.z] += l_shared[dispatch_thread_id.z + 4];
        }
        GroupMemoryBarrierWithGroupSync();
        if (dispatch_thread_id.z < 2)
        {
            multi_scattering_as1_shared[dispatch_thread_id.z] += multi_scattering_as1_shared[dispatch_thread_id.z + 2];
            l_shared[dispatch_thread_id.z] += l_shared[dispatch_thread_id.z + 2];
        }
        GroupMemoryBarrierWithGroupSync();
        if (dispatch_thread_id.z < 1)
        {
            multi_scattering_as1_shared[dispatch_thread_id.z] += multi_scattering_as1_shared[dispatch_thread_id.z + 1];
            l_shared[dispatch_thread_id.z] += l_shared[dispatch_thread_id.z + 1];
        }
        GroupMemoryBarrierWithGroupSync();
        if (dispatch_thread_id.z > 0)
            return;

        float3 multiple_scattering_as1 = multi_scattering_as1_shared[0] * isotropic_angle;	// Equation 7 f_ms
        float3 in_scattered_luminance	= l_shared[0] * isotropic_angle;				// Equation 5 L_2ndOrder

        // multiple_scattering_as1 represents the amount of luminance scattered as if the integral of scattered luminance over the sphere would be 1.
        //  - 1st order of scattering: one can ray-march a straight path as usual over the sphere. That is in_scattered_luminance.
        //  - 2nd order of scattering: the inscattered luminance is in_scattered_luminance at each of samples of fist order integration. Assuming a uniform phase function that is represented by multiple_scattering_as1,
        //  - 3nd order of scattering: the inscattered luminance is (in_scattered_luminance * multiple_scattering_as1 * multiple_scattering_as1)
        //  - etc.
#if !defined(MULTI_SCATTERING_POWER_SERIE)
        float3 multi_scatttering_as1_SQR = multiple_scattering_as1 * multiple_scattering_as1;
        float3 L = in_scattered_luminance * (1.0 + multiple_scattering_as1 + multi_scatttering_as1_SQR + multiple_scattering_as1 * multi_scatttering_as1_SQR + multi_scatttering_as1_SQR * multi_scatttering_as1_SQR);
#else
        // For a serie, sum_{n=0}^{n=+inf} = 1 + r + r^2 + r^3 + ... + r^n = 1 / (1.0 - r), see https://en.wikipedia.org/wiki/Geometric_series 
        const float3 r = multiple_scattering_as1;
        const float3 sum_all_mult_scat_contrib = 1.0f / (1.0 - r);
        float3 L = in_scattered_luminance * sum_all_mult_scat_contrib;// Equation 10 Psi_ms
#endif

        multi_scattering_texture[dispatch_thread_id.xy] = saturate(float4(L, 1.0f));

    ]]
}

compile : {
    includes : [ "multi_scattering_defines", "atmospheric_sky_common", "transmittance_common", "atmospheric_sky_render_common" ]
    variations : [
        { systems : [ "viewer_system" ] }
    ]
}


