common : [[

    float distance_to_top_atmosphere_boundary(tm_atmosphere_parameters_t atmosphere, float r, float mu) 
    {
        float discriminant = r * r * (mu * mu - 1.0) +atmosphere.top_radius * atmosphere.top_radius;
        return clamp_distance(-r * mu + safe_sqrt(discriminant));
    }

    float distance_top_bottom_atmosphere_boundary(tm_atmosphere_parameters_t atmosphere, float r, float mu) 
    {
        float discriminant = r * r * (mu * mu - 1.0) + atmosphere.bottom_radius * atmosphere.bottom_radius;
        return clamp_distance(-r * mu - safe_sqrt(discriminant));
    }

    bool ray_intersects_ground(tm_atmosphere_parameters_t atmosphere, float r, float mu) 
    {
        return mu < 0.0 && r * r * (mu * mu - 1.0) + atmosphere.bottom_radius * atmosphere.bottom_radius >= 0.0;
    }

    float compute_optical_lenght_to_top_atmosphere_boundary(tm_atmosphere_parameters_t atmosphere, tm_density_profile_t profile, float r, float mu) 
    {
        // float of intervals for the numerical integration.
        const int SAMPLE_COUNT = 500;
        // The integration step, i.e. the length of each integration interval.
        float dx = distance_to_top_atmosphere_boundary(atmosphere, r, mu) / float(SAMPLE_COUNT);
        // Integration loop.
        float result = 0.0;
        result += get_profile_density(profile, max(r - atmosphere.bottom_radius, 0)) * 0.5 * dx;
        for (int i = 1; i < SAMPLE_COUNT; ++i) {
            float d_i = float(i) * dx;
            // Distance between the current sample point and the planet center.
            float r_i = sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r);
            // float density at the current sample point (divided by the number density
            // at the bottom of the atmosphere, yielding a dimensionless number).
            float y_i = get_profile_density(profile, r_i - atmosphere.bottom_radius);
            // Sample weight (from the trapezoidal rule).
            float weight_i = 1.0;
            result += y_i * weight_i * dx;
        }
        float d_i = float(SAMPLE_COUNT) * dx;
        float r_i = sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r);
        result += get_profile_density(profile, max(r_i - atmosphere.bottom_radius, 0)) * 0.5 * dx;

        return result;
    }

    float3 compute_transmittance_to_top_atmosphere_boundary(tm_atmosphere_parameters_t atmosphere, float r, float mu) 
    {
        return exp(-(atmosphere.rayleigh_scattering * compute_optical_lenght_to_top_atmosphere_boundary(atmosphere, atmosphere.rayleigh_density, r, mu)
                     + atmosphere.mie_extinction * compute_optical_lenght_to_top_atmosphere_boundary(atmosphere, atmosphere.mie_density, r, mu)
                     + atmosphere.absorption_extinction * compute_optical_lenght_to_top_atmosphere_boundary(atmosphere, atmosphere.absorption_density, r, mu)));
    }

    float get_texture_coord_from_unit_range(float x, int texture_size) 
    {
        return 0.5 / float(texture_size) + x * (1.0 - 1.0 / float(texture_size));
    }

    float get_unit_range_from_texture_coord(float u, int texture_size) 
    {
        return (u - 0.5 / float(texture_size)) / (1.0 - 1.0 / float(texture_size));
    }

    float2 get_transmittance_texture_uv_from_RMu(tm_atmosphere_parameters_t atmosphere, float r, float mu) 
    {
        // Distance to top atmosphere boundary for a horizontal ray at ground level.
        float H = sqrt(atmosphere.top_radius * atmosphere.top_radius - atmosphere.bottom_radius * atmosphere.bottom_radius);
        // Distance to the horizon.
        float rho = safe_sqrt(r * r - atmosphere.bottom_radius * atmosphere.bottom_radius);
        // Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
        // and maximum values over all mu - obtained for (r,1) and (r,mu_horizon).
        float d = distance_to_top_atmosphere_boundary(atmosphere, r, mu);
        float d_min = atmosphere.top_radius - r;
        float d_max = rho + H;
        float x_mu = (d - d_min) / (d_max - d_min);
        float x_r = rho / H;
        return float2(get_texture_coord_from_unit_range(x_mu, load_TRANSMITTANCE_TEXTURE_WIDTH()),
                      get_texture_coord_from_unit_range(x_r, load_TRANSMITTANCE_TEXTURE_HEIGHT()));
    }

    void get_RMu_from_transmittance_texture_uv(tm_atmosphere_parameters_t atmosphere, float2 uv, out float r, out float mu) 
    {
        float x_mu = get_unit_range_from_texture_coord(uv.x, load_TRANSMITTANCE_TEXTURE_WIDTH());
        float x_r = get_unit_range_from_texture_coord(uv.y, load_TRANSMITTANCE_TEXTURE_HEIGHT());
        // Distance to top atmosphere boundary for a horizontal ray at ground level.
        float H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
                atmosphere.bottom_radius * atmosphere.bottom_radius);
        // Distance to the horizon, from which we can compute r:
        float rho = H * x_r;
        r = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);
        // Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
        // and maximum values over all mu - obtained for (r,1) and (r,mu_horizon) -
        // from which we can recover mu:
        float d_min = atmosphere.top_radius - r;
        float d_max = rho + H;
        float d = d_min + x_mu * (d_max - d_min);
        mu = d == 0.0 ? float(1.0) : (H * H - rho * rho - d * d) / (2.0 * r * d);
        mu = clamp_cosine(mu);
    }

    float3 compute_transmittance_to_top_atmosphere_boundary_texture(tm_atmosphere_parameters_t atmosphere, float2 frag_coord) 
    {
        const float2 TRANSMITTANCE_TEXTURE_SIZE = float2(load_TRANSMITTANCE_TEXTURE_WIDTH(), load_TRANSMITTANCE_TEXTURE_HEIGHT());
        float r;
        float mu;
        get_RMu_from_transmittance_texture_uv(atmosphere, frag_coord / TRANSMITTANCE_TEXTURE_SIZE, r, mu);
        return compute_transmittance_to_top_atmosphere_boundary(atmosphere, r, mu);
    }

    float3 get_transmittance_to_top_atmosphere_boundary(tm_atmosphere_parameters_t atmosphere, Texture2D<float4> transmittance_texture, float r, float mu) 
    {
        float2 uv = get_transmittance_texture_uv_from_RMu(atmosphere, r, mu);
        SamplerState samp = get_clamp_linear();
        return transmittance_texture.Sample(samp, uv).rgb;
    }

    float3 get_transmittance(tm_atmosphere_parameters_t atmosphere, Texture2D<float4> transmittance_texture, float r, float mu, float d, bool ray_r_mu_intersects_ground) 
    {
        float r_d = clamp_radius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
        float mu_d = clamp_cosine((r * mu + d) / r_d);

        [branch]
        if (ray_r_mu_intersects_ground) {
            return min(get_transmittance_to_top_atmosphere_boundary(atmosphere, transmittance_texture, r_d, -mu_d)
                       / get_transmittance_to_top_atmosphere_boundary(atmosphere, transmittance_texture, r, -mu),
                       float3(1.0, 1.0, 1.0));
        } else {
            return min(get_transmittance_to_top_atmosphere_boundary(atmosphere, transmittance_texture, r, mu)
                       / get_transmittance_to_top_atmosphere_boundary(atmosphere, transmittance_texture, r_d, mu_d),
                       float3(1.0, 1.0, 1.0));
        }
    }

    float3 get_transmittance_to_sun(tm_atmosphere_parameters_t atmosphere, Texture2D<float4> transmittance_texture, float r, float mu_s) {
        float sin_theta_h = atmosphere.bottom_radius / r;
        float cos_theta_h = -sqrt(max(1.0 - sin_theta_h * sin_theta_h, 0.0));

        return get_transmittance_to_top_atmosphere_boundary(atmosphere, transmittance_texture, r, mu_s)
               * smoothstep(-sin_theta_h * atmosphere.sun_angular_radius, sin_theta_h * atmosphere.sun_angular_radius , mu_s - cos_theta_h);
    }
]]
