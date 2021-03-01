
imports : [
    { name : "transmittance_texture" type : "texture_2d" element_type: "float4" }
    { name : "multi_scattering_texture" type : "texture_2d" element_type: "float4" }
    { name : "sky_view_lut_texture" type : "texture_2d" element_type: "float4" }
    { name : "shadow_map_texture" type : "texture_2d" element_type: "float4" }
    { name : "depth_buffer_texture" type : "texture_2d" element_type: "float4" }
]


common : [[
#define RAYDPOS 0.00001f
#define PLANET_RADIUS_OFFSET 0.01f

    // transmittance LUT function parameterisation from Bruneton 2017 https://github.com/ebruneton/precomputed_atmospheric_scattering
    // uv in [0,1]
    // view_zenith_cos_angle in [-1,1]
    // view_height in [bottomRAdius, topRadius]

    // We should precompute those terms from resolutions (Or set resolution as #defined constants)
    float from_sub_uvs_to_unit(float u, float resolution) 
    { 
        return (u + 0.5f / resolution) * (resolution / (resolution + 1.0f)); 
    }

    float from_unit_to_sub_uvs(float u, float resolution) { 
        return (u - 0.5f / resolution) * (resolution / (resolution - 1.0f)); 
    }

    void uv_to_lut_transmittance_params(tm_atmosphere_parameters_t atmosphere, float2 uv, out float view_height, out float view_zenith_cos_angle)
    {
        uv = float2(from_sub_uvs_to_unit(uv.x, load_TRANSMITTANCE_TEXTURE_WIDTH()), from_sub_uvs_to_unit(uv.y, load_TRANSMITTANCE_TEXTURE_HEIGHT()));
        float x_mu = uv.x;
        float x_r = uv.y;

        float H = sqrt(atmosphere.top_radius * atmosphere.top_radius - atmosphere.bottom_radius * atmosphere.bottom_radius);
        float rho = H * x_r;
        view_height = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);

        float d_min = atmosphere.top_radius - view_height;
        float d_max = rho + H;
        float d = d_min + x_mu * (d_max - d_min);
        view_zenith_cos_angle = d == 0.0 ? 1.0f : (H * H - rho * rho - d * d) / (2.0 * view_height * d);
        view_zenith_cos_angle = clamp(view_zenith_cos_angle, -1.0, 1.0);
    }

    void uv_to_sky_view_lut_param(tm_atmosphere_parameters_t atmosphere, out float view_zenith_cos_angle, out float light_view_cos_angle, in float view_height, in float2 uv)
    {
        // Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible)
        uv = float2(from_sub_uvs_to_unit(uv.x, load_render_target_resolution().x), from_sub_uvs_to_unit(uv.y, load_render_target_resolution().y));

        float v_horizon = sqrt(view_height * view_height - atmosphere.bottom_radius * atmosphere.bottom_radius);
        float cos_beta = v_horizon / view_height;				// GroundToHorizonCos
        float beta = acos(cos_beta);
        float zenith_horizon_angle = PI - beta;

        if (uv.y < 0.5f)
        {
            float coord = 2.0 * uv.y;
            coord = 1.0 - coord;
#if defined(NONLINEARSKYVIEWLUT)
            coord *= coord;
#endif
            coord = 1.0 - coord;
            view_zenith_cos_angle = cos(zenith_horizon_angle * coord);
        }
        else
        {
            float coord = uv.y*2.0 - 1.0;
#if defined(NONLINEARSKYVIEWLUT)
            coord *= coord;
#endif
            view_zenith_cos_angle = cos(zenith_horizon_angle + beta * coord);
        }

        float coord = uv.x;
        coord *= coord;
        light_view_cos_angle = -(coord * 2.0 - 1.0);
    }

    void sky_view_lut_params_to_uv(tm_atmosphere_parameters_t atmosphere, in bool intersect_ground, in float view_zenith_cos_angle, in float light_view_cos_angle, in float view_height, out float2 uv)
    {
        float v_horizon = sqrt(view_height * view_height - atmosphere.bottom_radius * atmosphere.bottom_radius);
        float cos_beta = v_horizon / view_height;				// GroundToHorizonCos
        float beta = acos(cos_beta);
        float zenith_horizon_angle = PI - beta;

        if (!intersect_ground)
        {
            float coord = acos(view_zenith_cos_angle) / zenith_horizon_angle;
            coord = 1.0 - coord;
#if defined(NONLINEARSKYVIEWLUT)
            coord = sqrt(coord);
#endif
            coord = 1.0 - coord;
            uv.y = coord * 0.5f;
        }
        else
        {
            float coord = (acos(view_zenith_cos_angle) - zenith_horizon_angle) / beta;
#if defined(NONLINEARSKYVIEWLUT)
            coord = sqrt(coord);
#endif
            uv.y = coord * 0.5f + 0.5f;
        }

        {
            float coord = -light_view_cos_angle * 0.5f + 0.5f;
            coord = sqrt(coord);
            uv.x = coord;
        }

        // Constrain uvs to valid sub texel range (avoid zenith derivative issue making LUT usage visible)
        uv = float2(from_sub_uvs_to_unit(uv.x, load_render_target_resolution().x), from_sub_uvs_to_unit(uv.y, load_render_target_resolution().y));
    }

    float get_albedo(float scattering, float extinction)
    {
        return scattering / max(0.001, extinction);
    }
    float3 get_albedo(float3 scattering, float3 extinction)
    {
        return scattering / max(0.001, extinction);
    }

    struct medium_sample_rgb_t
    {
        float3 scattering;
        float3 absorption;
        float3 extinction;

        float3 mie_scattering;
        float3 mie_absorption;
        float3 mie_extinction;

        float3 rayleigh_scattering;
        float3 rayleigh_absorption;
        float3 rayleigh_extinction;

        float3 ozo_scattering;
        float3 ozo_absorption;
        float3 ozo_extinction;

        float3 albedo;
    };

    medium_sample_rgb_t sample_medium_rgb(in float3 world_pos, in tm_atmosphere_parameters_t atmosphere)
    {
        const float view_height = length(world_pos - load_celestial_body_center()) - atmosphere.bottom_radius;
        
        const float mie_density = get_profile_density(atmosphere.rayleigh_density, view_height);
        const float rayleigh_density = get_profile_density(atmosphere.rayleigh_density, view_height);
        const float ozo_density = saturate(get_profile_density(atmosphere.absorption_density, view_height));

        medium_sample_rgb_t s;

        s.mie_scattering = mie_density * atmosphere.mie_scattering;
        s.mie_absorption = mie_density * atmosphere.mie_absorption;
        s.mie_extinction = mie_density * atmosphere.mie_extinction;

        s.rayleigh_scattering = rayleigh_density * atmosphere.rayleigh_scattering;
        s.rayleigh_absorption = 0.0f;
        s.rayleigh_extinction = s.rayleigh_scattering + s.rayleigh_absorption;

        s.ozo_scattering = 0.0;
        s.ozo_absorption = ozo_density * atmosphere.absorption_extinction;
        s.ozo_extinction = s.ozo_scattering + s.ozo_absorption;

        s.scattering = s.mie_scattering + s.rayleigh_scattering + s.ozo_scattering;
        s.absorption = s.mie_absorption + s.rayleigh_absorption + s.ozo_absorption;
        s.extinction = s.mie_extinction + s.rayleigh_extinction + s.ozo_extinction;
        s.albedo = get_albedo(s.scattering, s.extinction);

        return s;
    }

    float rayleigh_phase(float cos_theta)
    {
        float factor = 3.0f / (16.0f * PI);
        return factor * (1.0f + cos_theta * cos_theta);
    }

    float cornette_shank_mie_phase_function(float g, float cos_theta)
    {
        float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
        return k * (1.0 + cos_theta * cos_theta) / pow(1.0 + g * g - 2.0 * g * -cos_theta, 1.5);
    }

    float hg_phase(float g, float cos_theta)
    {
#if defined(USE_CornetteShanks)
        return cornette_shank_mie_phase_function(g, cos_theta);
#else
        // Reference implementation (i.e. not schlick approximation). 
        // See http://www.pbr-book.org/3ed-2018/Volume_Scattering/Phase_Functions.html
        float numer = 1.0f - g * g;
        float denom = 1.0f + g * g + 2.0f * g * cos_theta;
        return numer / (4.0f * PI * denom * sqrt(denom));
#endif
    }

    float dual_lob_phase(float g0, float g1, float w, float cos_theta)
    {
        return lerp(hg_phase(g0, cos_theta), hg_phase(g1, cos_theta), w);
    }

    float uniform_phase()
    {
        return 1.0f / (4.0f * PI);
    }

    float whang_hash_noise(uint u, uint v, uint s)
    {
        uint seed = (u * 1664525u + v) + s;
        seed = (seed ^ 61u) ^ (seed >> 16u);
        seed *= 9u;
        seed = seed ^ (seed >> 4u);
        seed *= uint(0x27d4eb2d);
        seed = seed ^ (seed >> 15u);
        float value = float(seed) / (4294967296.0);
        return value;
    }

    bool move_to_top_atmosphere(inout float3 world_pos, in float3 world_dir, in float atmosphere_top_radius)
    {
        float view_height = length(world_pos - load_celestial_body_center());
        if (view_height > atmosphere_top_radius)
        {
            world_dir = normalize(world_dir);
            float t_top = ray_sphere_intersect_nearest(world_pos, world_dir, load_celestial_body_center(), atmosphere_top_radius);
            if (t_top >= 0.0f)
            {
                float3 up_vector = normalize(load_camera_position() - load_celestial_body_center());
                float3 up_offset = up_vector * -PLANET_RADIUS_OFFSET;
                world_pos = world_pos + world_dir * t_top + up_offset;
            }
            else
            {
                // Ray is not intersecting the atmosphere
                return false;
            }
        }
        return true; // ok to start tracing
    }

    float3 get_sun_luminance(float3 world_pos, float3 world_dir, float planet_radius)
    {
#if defined(RENDER_SUN_DISK)
        if (dot(world_dir, load_sun_direction()) > cos(load_sun_angular_radius()*3.14159 / 180.0))
        {
            float t = ray_sphere_intersect_nearest(world_pos, world_dir, load_celestial_body_center(), planet_radius);
            if (t < 0.0f) // no intersection
            {
                const float3 SunLuminance = 100000.0 / 20.0; // arbitrary. But fine, not use when comparing the models
                return SunLuminance;
            }
        }
#endif
        return 0;
    }

    float3 get_multiple_scattering(tm_atmosphere_parameters_t atmosphere, float3 scattering, float3 extinction, float3 world_pos, float view_zenith_cos_angle)
    {
        float2 uv = saturate(float2(view_zenith_cos_angle * 0.5f + 0.5f, (length(world_pos - load_celestial_body_center()) - atmosphere.bottom_radius) / (atmosphere.top_radius - atmosphere.bottom_radius)));
        uint2 res = float2(load_MULTI_SCATTERING_TEXTURE_WIDTH(), load_MULTI_SCATTERING_TEXTURE_HEIGHT());
        uv = float2(from_unit_to_sub_uvs(uv.x, res.x), from_unit_to_sub_uvs(uv.y, res.y));

        SamplerState samp = get_clamp_linear();
        float3 multi_scattered_luminance = get_multi_scattering_texture().SampleLevel(samp, uv, 0).rgb;
        return multi_scattered_luminance;
    }

    float get_shadow(in tm_atmosphere_parameters_t atmosphere, float3 P)
    {
        // First evaluate opaque shadow
        // TODO
        // check shadow matrix
        /*float3 from_center_dir = normalize(load_camera_position() - load_celestial_body_center());*/
        float3 from_center_dir = float3(0.0, 1.0, 0.0);
        float4 shadow_uv = mul(load_camera_inverse_view_projection(), float4(P + from_center_dir * load_offset_from_center(), 1.0));
        shadow_uv.x = shadow_uv.x*0.5 + 0.5;
        shadow_uv.y = -shadow_uv.y*0.5 + 0.5;
        if (all(shadow_uv.xyz >= 0.0) && all(shadow_uv.xyz < 1.0))
        {
            SamplerComparisonState pcf_samp = get_pcf_sampler();
            return get_shadow_map_texture().SampleCmpLevelZero(pcf_samp, shadow_uv.xy, shadow_uv.z, int2(0, 0));
        }
        return 1.0f;
    }

#define USE_REVERSE_DEPTH

    float clip_space_depth(float linear_depth, float near, float far) {
        #if defined(USE_REVERSE_DEPTH)
            return ((near * far)/linear_depth - near)/(far-near);
        #else
            return -((near * far)/linear_depth - far)/(far-near);
        #endif
    }

    struct single_scattering_result_t
    {
        float3 L;						// Scattered light (luminance)
        float3 optical_depth;			// Optical depth (1/m)
        float3 transmittance;			// transmittance in [0,1] (unitless)
        float3 multiple_scattering_as1;

        float3 new_multiple_scattering_step0_out;
        float3 new_multiple_scattering_step1_out;
    };

    single_scattering_result_t integrate_scattered_luminance(in float2 pix_pos, in float3 world_pos, in float3 world_dir, in float3 sun_dir, in tm_atmosphere_parameters_t atmosphere,
            in bool ground, in float sample_count_ini, in float depth_buffer_value, in bool variable_sample_count, in bool mie_rayleigh_phase, in float t_max_max = 9000000.0f)
    {
        single_scattering_result_t result = (single_scattering_result_t)0;
        if ((length(world_pos - load_celestial_body_center()) < atmosphere.bottom_radius)) {
            return result;
        }

        float3 clip_space = float3((pix_pos / load_render_target_resolution()) * float2(2.0, -2.0) - float2(1.0, -1.0), 1.0);

        // Compute next intersection with atmosphere or ground 
        float3 earth_org = load_celestial_body_center();
        float t_bottom = ray_sphere_intersect_nearest(world_pos, world_dir, earth_org, atmosphere.bottom_radius);
        float t_top = ray_sphere_intersect_nearest(world_pos, world_dir, earth_org, atmosphere.top_radius);
        float t_max = 0.0f;

        if (t_bottom < 0.0f)
        {
            if (t_top < 0.0f)
            {
                t_max = 0.0f; // No intersection with earth nor atmosphere: stop right away  
                return result;
            }
            else
            {
                t_max = t_top;
            }
        }
        else
        {
            if (t_top > 0.0f)
            {
                t_max = min(t_top, t_bottom);
            }
        }

        float2 near_far = load_camera_near_far();
        float depth = clip_space_depth(depth_buffer_value, near_far.x, near_far.y);

        if (depth >= 1.0)
        {
            clip_space.z = depth;
            if (clip_space.z < 1.0)
            {
                float4 depth_buffer_world_pos = mul(float4(clip_space, 1.0), load_camera_inverse_view_projection());
                depth_buffer_world_pos /= depth_buffer_world_pos.w;

                /*float3 from_center_dir = normalize(load_camera_position() - load_celestial_body_center());*/
                float3 from_center_dir = float3(0.0, 1.0, 0.0);
                float t_depth = length(depth_buffer_world_pos.xyz - (world_pos - from_center_dir * load_offset_from_center())); // apply earth offset to go back to origin as top of earth mode. 
                if (t_depth < t_max)
                {
                    t_max = t_depth;
                }
            }
            //		if (variable_sample_count && clip_space.z == 1.0f)
            //			return result;
        }
        t_max = min(t_max, t_max_max);

        // Sample count 
        float sample_count = sample_count_ini;
        float sample_count_floor = sample_count_ini;
        float t_max_floor = t_max;
        if (variable_sample_count)
        {
            sample_count = lerp(1, 20, saturate(t_max * 0.01));
            sample_count_floor = floor(sample_count);
            t_max_floor = t_max * sample_count_floor / sample_count;	// rescale t_max to map to the last entire step segment.
        }
        float dt = t_max / sample_count;

        // Phase functions
        const float uniform_phase = 1.0 / (4.0 * PI);
        const float3 wi = sun_dir;
        const float3 wo = world_dir;
        float cos_theta = dot(wi, wo);
        float mie_phase_value = hg_phase(atmosphere.mie_phase_function_g, -cos_theta);	// mnegate cos_theta because due to world_dir being a "in" direction. 
        float rayleigh_phase_value = rayleigh_phase(cos_theta);

#ifdef ILLUMINANCE_IS_ONE
        // When building the scattering factor, we assume light illuminance is 1 to compute a transfert function relative to identity illuminance of 1.
        // This make the scattering factor independent of the light. It is now only linked to the atmosphere properties.
        float3 global_l = 1.0f;
#else
        float3 global_l = load_global_iluminance();
#endif

        // Ray march the atmosphere to integrate optical depth
        float3 L = 0.0f;
        float3 throughput = 1.0;
        float3 optical_depth = 0.0;
        float t = 0.0f;
        float tPrev = 0.0;
        const float sample_segment_t = 0.3f;
        for (float s = 0.0f; s < sample_count; s += 1.0f)
        {
            if (variable_sample_count)
            {
                // More expenssive but artefact free
                float t0 = (s) / sample_count_floor;
                float t1 = (s + 1.0f) / sample_count_floor;
                // Non linear distribution of sample within the range.
                t0 = t0 * t0;
                t1 = t1 * t1;
                // Make t0 and t1 world space distances.
                t0 = t_max_floor * t0;
                if (t1 > 1.0)
                {
                    t1 = t_max;
                    //	t1 = t_max_floor;	// this reveal depth slices
                }
                else
                {
                    t1 = t_max_floor * t1;
                }
                //t = t0 + (t1 - t0) * (whang_hash_noise(pix_pos.x, pix_pos.y, gFrameId * 1920 * 1080)); // With dithering required to hide some sampling artefact relying on TAA later? This may even allow volumetric shadow?
                t = t0 + (t1 - t0) * sample_segment_t;
                dt = t1 - t0;
            }
            else
            {
                //t = t_max * (s + sample_segment_t) / sample_count;
                // Exact difference, important for accuracy of multiple scattering
                float new_t = t_max * (s + sample_segment_t) / sample_count;
                dt = new_t - t;
                t = new_t;
            }
            float3 P = world_pos + t * world_dir;

            medium_sample_rgb_t medium = sample_medium_rgb(P, atmosphere);
            const float3 sample_optical_depth = medium.extinction * dt;
            const float3 sample_transmittance = exp(-sample_optical_depth);
            optical_depth += sample_optical_depth;

            float p_height = length(P - earth_org);
            const float3 up_vector = (P - earth_org) / p_height;
            float sun_zenith_cos_angle = dot(sun_dir, up_vector);

            float2 uv = get_transmittance_texture_uv_from_RMu(atmosphere, p_height, sun_zenith_cos_angle);
            SamplerState samp = get_clamp_linear();
            float3 transmittance_to_sun = get_transmittance_texture().SampleLevel(samp, uv, 0).rgb;

            float3 phase_times_scattering;
            if (mie_rayleigh_phase)
            {
                phase_times_scattering = medium.mie_scattering * mie_phase_value + medium.rayleigh_scattering * rayleigh_phase_value;
            }
            else
            {
                phase_times_scattering = medium.scattering * uniform_phase;
            }

            // Earth shadow 
            float tEarth = ray_sphere_intersect_nearest(P, sun_dir, earth_org + PLANET_RADIUS_OFFSET * up_vector, atmosphere.bottom_radius);
            float earthShadow = tEarth >= 0.0f ? 0.0f : 1.0f;

            // Dual scattering for multi scattering 

            float3 multiScatteredLuminance = 0.0f;
#if defined(MULTISCATAPPROX_ENABLED)
            multiScatteredLuminance = get_multiple_scattering(atmosphere, medium.scattering, medium.extinction, P, sun_zenith_cos_angle);
#endif

            float shadow = 1.0f;
#if defined(SHADOWMAP_ENABLED)
            // First evaluate opaque shadow
            shadow = get_shadow(atmosphere, P);
#endif

            float3 S = global_l * (earthShadow * shadow * transmittance_to_sun * phase_times_scattering + multiScatteredLuminance * medium.scattering);

            // When using the power serie to accumulate all sattering order, serie r must be <1 for a serie to converge.
            // Under extreme coefficient, multiple_scattering_as1 can grow larger and thus result in broken visuals.
            // The way to fix that is to use a proper analytical integration as proposed in slide 28 of http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
            // However, it is possible to disable as it can also work using simple power serie sum unroll up to 5th order. The rest of the orders has a really low contribution.

#if !defined(MULTI_SCATTERING_POWER_SERIE)
            // 1 is the integration of luminance over the 4pi of a sphere, and assuming an isotropic phase function of 1.0/(4*PI)
            result.multiple_scattering_as1 += throughput * medium.scattering * 1 * dt;
#else
            float3 MS = medium.scattering * 1;
            float3 MSint = (MS - MS * sample_transmittance) / medium.extinction;
            result.multiple_scattering_as1 += throughput * MSint;
#endif

            // Evaluate input to multi scattering 
            {
                float3 newMS;

                newMS = earthShadow * transmittance_to_sun * medium.scattering * uniform_phase * 1;
                result.new_multiple_scattering_step0_out += throughput * (newMS - newMS * sample_transmittance) / medium.extinction;
                //	result.new_multiple_scattering_step0_out += sample_transmittance * throughput * newMS * dt;

                newMS = medium.scattering * uniform_phase * multiScatteredLuminance;
                result.new_multiple_scattering_step1_out += throughput * (newMS - newMS * sample_transmittance) / medium.extinction;
                //	result.new_multiple_scattering_step1_out += sample_transmittance * throughput * newMS * dt;
            }

#if 0
            L += throughput * S * dt;
            throughput *= sample_transmittance;
#else
            // See slide 28 at http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/ 
            float3 Sint = (S - S * sample_transmittance) / medium.extinction;	// integrate along the current step segment 
            L += throughput * Sint;														// accumulate and also take into account the transmittance from previous steps
            throughput *= sample_transmittance;
#endif

            tPrev = t;
        }

        if (ground && t_max == t_bottom && t_bottom > 0.0)
        {
            // Account for bounced light off the earth
            float3 P = world_pos + t_bottom * world_dir;

            float p_height = length(P - earth_org);
            const float3 up_vector = (P - earth_org) / p_height;
            float sun_zenith_cos_angle = dot(sun_dir, up_vector);
            float2 uv = get_transmittance_texture_uv_from_RMu(atmosphere, p_height, sun_zenith_cos_angle);
            SamplerState samp = get_clamp_linear();
            float3 transmittance_to_sun = get_transmittance_texture().SampleLevel(samp, uv, 0).rgb;

            const float n_dot_l = saturate(dot(normalize(up_vector), normalize(sun_dir)));
            L += global_l * transmittance_to_sun * throughput * n_dot_l * atmosphere.ground_albedo / PI;
        }

        result.L = L;
        result.optical_depth = optical_depth;
        result.transmittance = throughput;
        return result;
    }

#define AP_SLICE_COUNT 32.0f
#define AP_KM_PER_SLICE 4.0f

    float aerial_perspective_depth_to_slice(float depth)
    {
        return depth * (1.0f / AP_KM_PER_SLICE);
    }
    float aerial_perspective_to_depth(float slice)
    {
        return slice * AP_KM_PER_SLICE;
    }

    /*float4 RenderCameraVolumePS(GeometryOutput Input) : SV_TARGET0*/
    /*{*/
        /*float2 pix_pos = Input.position.xy;*/
        /*tm_atmosphere_parameters_t atmosphere = GetAtmosphereParameters();*/

        /*float3 clip_space = float3((pix_pos / float2(load_render_target_resolution()))*float2(2.0, -2.0) - float2(1.0, -1.0), 0.5);*/
        /*float4 HPos = mul(gSkyInvViewProjMat, float4(clip_space, 1.0));*/
        /*float3 world_dir = normalize(HPos.xyz / HPos.w - camera);*/
        /*float earthR = atmosphere.bottom_radius;*/
        /*float3 earth_org = float3(0.0, 0.0, -earthR);*/
        /*float3 camPos = camera + float3(0, 0, earthR);*/
        /*float3 sun_dir = sun_direction;*/
        /*float3 SunLuminance = 0.0;*/

        /*float Slice = ((float(Input.sliceId) + 0.5f) / AP_SLICE_COUNT);*/
        /*Slice *= Slice;	// squared distribution*/
        /*Slice *= AP_SLICE_COUNT;*/

        /*float3 world_pos = camPos;*/
        /*float view_height;*/


        /*// Compute position from froxel information*/
        /*float t_max = aerial_perspective_to_depth(Slice);*/
        /*float3 newWorldPos = world_pos + t_max * world_dir;*/


        /*// If the voxel is under the ground, make sure to offset it out on the ground.*/
        /*view_height = length(newWorldPos);*/
        /*if (view_height <= (atmosphere.bottom_radius + PLANET_RADIUS_OFFSET))*/
        /*{*/
            /*// Apply a position offset to make sure no artefact are visible close to the earth boundaries for large voxel.*/
            /*newWorldPos = normalize(newWorldPos) * (atmosphere.bottom_radius + PLANET_RADIUS_OFFSET + 0.001f);*/
            /*world_dir = normalize(newWorldPos - camPos);*/
            /*t_max = length(newWorldPos - camPos);*/
        /*}*/
        /*float t_max_max = t_max;*/


        /*// Move ray marching start up to top atmosphere.*/
        /*view_height = length(world_pos);*/
        /*if (view_height >= atmosphere.top_radius)*/
        /*{*/
            /*float3 prevWorlPos = world_pos;*/
            /*if (!move_to_top_atmosphere(world_pos, world_dir, atmosphere.top_radius))*/
            /*{*/
                /*// Ray is not intersecting the atmosphere*/
                /*return float4(0.0, 0.0, 0.0, 1.0);*/
            /*}*/
            /*float LengthToAtmosphere = length(prevWorlPos - world_pos);*/
            /*if (t_max_max < LengthToAtmosphere)*/
            /*{*/
                /*// t_max_max for this voxel is not within earth atmosphere*/
                /*return float4(0.0, 0.0, 0.0, 1.0);*/
            /*}*/
            /*// Now world position has been moved to the atmosphere boundary: we need to reduce t_max_max accordingly. */
            /*t_max_max = max(0.0, t_max_max - LengthToAtmosphere);*/
        /*}*/


        /*const bool ground = false;*/
        /*const float sample_count_ini = max(1.0, float(Input.sliceId + 1.0) * 2.0f);*/
        /*const float depth_buffer_value = -1.0;*/
        /*const bool variable_sample_count = false;*/
        /*const bool mie_rayleigh_phase = true;*/
        /*single_scattering_result_t ss = integrate_scattered_luminance(pix_pos, world_pos, world_dir, sun_dir, atmosphere, ground, sample_count_ini, depth_buffer_value, variable_sample_count, mie_rayleigh_phase, t_max_max);*/


        /*const float transmittance = dot(ss.transmittance, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f));*/
        /*return float4(ss.L, 1.0 - transmittance);*/
    /*}*/

]]
