samplers : {
    pcf_clamp_linear : {
        anisotropy_enable: false 
        compare_enable: true 
        compare_op : "greater"
        min_filter : "linear" 
        max_filter : "linear" 
        mip_mode : "linear" 
        address_u : "clamp" 
        address_v : "clamp" 
        address_w : "clamp"
    }
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
	{ name: "sun_direction" type: "float3" }
    { name: "celestial_body_center" type: "float3" }
    { name: "offset_from_center" type: "float" }
    { name: "sun_angular_radius" type: "float" }
    { name: "mie_phase_function_g" type: "float" }
    { name: "top_radius" type: "float" }
    { name: "bottom_radius" type: "float" }
    { name: "rayleigh_scattering" type: "float3" }
    { name: "absorption_extinction" type: "float3" }
    { name: "mie_scattering" type: "float3" }
    { name: "mie_extinction" type: "float3" }
    { name: "mie_absorption" type: "float3" }
    { name: "rayleigh_density" type: "float4" elements: 3 }
    { name: "mie_density" type: "float4" elements: 3 }
    { name: "absorption_density" type: "float4" elements: 3 }
    { name: "ground_albedo" type: "float3" }
    { name: "global_iluminance" type: "float3" }
	{ name: "TRANSMITTANCE_TEXTURE_WIDTH" type: "int" }
	{ name: "TRANSMITTANCE_TEXTURE_HEIGHT" type: "int" }
	{ name: "MULTI_SCATTERING_TEXTURE_WIDTH" type: "int" }
	{ name: "MULTI_SCATTERING_TEXTURE_HEIGHT" type: "int" }
    { name: "clamp_linear" type: "sampler" sampler: "clamp_linear" }
    { name: "pcf_sampler" type: "sampler_comparison" sampler: "pcf_clamp_linear" }
]

common : [[
#define PI 3.1415926535897932384626433832795f

    struct tm_density_profile_layer_t {
      float width;
      float exp_term;
      float exp_scale;
      float linear_term;
      float constant_term;
    };

    struct tm_density_profile_t {
      tm_density_profile_layer_t layers[2];
    };

    struct tm_atmosphere_parameters_t {
      float sun_angular_radius;
      float bottom_radius;
      float top_radius;
      tm_density_profile_t rayleigh_density;
      float3 rayleigh_scattering;
      tm_density_profile_t mie_density;
      float3 mie_scattering;
      float3 mie_extinction;
      float3 mie_absorption;
      float mie_phase_function_g;
      tm_density_profile_t absorption_density;
      float3 absorption_extinction;
      float3 ground_albedo;
    };

    float clamp_cosine(float mu) {
      return clamp(mu, float(-1.0), float(1.0));
    }

    float clamp_distance(float d) {
      return max(d, 0.0);
    }

    float clamp_radius(tm_atmosphere_parameters_t atmosphere, float r) {
      return clamp(r, atmosphere.bottom_radius, atmosphere.top_radius);
    }

    float safe_sqrt(float a) {
      return sqrt(max(a, 0.0));
    }

    tm_density_profile_t get_density_profile(float4 density[3])
    {
        tm_density_profile_t profile;
        profile.layers[0].width = density[0].x;
        profile.layers[0].exp_term = density[0].y;
        profile.layers[0].exp_scale = density[0].z;
        profile.layers[0].linear_term = density[0].w;
        profile.layers[0].constant_term = density[1].x;
        profile.layers[1].width = density[1].y;
        profile.layers[1].exp_term = density[1].z;
        profile.layers[1].exp_scale = density[1].w;
        profile.layers[1].linear_term = density[2].x;
        profile.layers[1].constant_term = density[2].y;

        return profile;
    }

    tm_density_profile_t get_rayleigh_profile() 
    {
        float4 rayleigh[3] = { load_rayleigh_density(0), load_rayleigh_density(1), load_rayleigh_density(2) };
        return get_density_profile(rayleigh);
    }

    tm_density_profile_t get_mie_profile() 
    {
        float4 mie[3] = { load_mie_density(0), load_mie_density(1), load_mie_density(2) };
        return get_density_profile(mie);
    }

    tm_density_profile_t get_absorption_profile() 
    {
        float4 absorption[3] = { load_absorption_density(0), load_absorption_density(1), load_absorption_density(2) };
        return get_density_profile(absorption);
    }

    tm_atmosphere_parameters_t get_atmosphere_parameters()
    {
        tm_atmosphere_parameters_t params;
        params.sun_angular_radius = load_sun_angular_radius();
        params.bottom_radius = load_bottom_radius();
        params.top_radius = load_top_radius();
        params.rayleigh_density = get_rayleigh_profile();
        params.rayleigh_scattering = load_rayleigh_scattering();
        params.mie_density = get_mie_profile();
        params.mie_scattering = load_mie_scattering();
        params.mie_extinction = load_mie_extinction();
        params.mie_absorption = load_mie_absorption();
        params.mie_phase_function_g = load_mie_phase_function_g();
        params.absorption_density = get_absorption_profile();
        params.absorption_extinction = load_absorption_extinction();
        params.ground_albedo = load_ground_albedo();

        return params;
    }

    float get_layer_density(tm_density_profile_layer_t layer, float altitude) 
    {
        float density = layer.exp_term * exp(layer.exp_scale * altitude) + layer.linear_term * altitude + layer.constant_term;
        return clamp(density, 0.0001, 1.0);
    }

    float get_profile_density(tm_density_profile_t profile, float altitude) 
    {
        return altitude < profile.layers[0].width ? get_layer_density(profile.layers[0], altitude) :
                                                    get_layer_density(profile.layers[1], altitude);
    }

    // - r0: ray origin
    // - rd: normalized ray direction
    // - s0: sphere center
    // - sR: sphere radius
    // - Returns distance from r0 to first intersecion with sphere,
    //   or -1.0 if no intersection.
    float ray_sphere_intersect_nearest(float3 r0, float3 rd, float3 s0, float sR)
    {
        float a = dot(rd, rd);
        float3 s0_r0 = r0 - s0;
        float b = 2.0 * dot(rd, s0_r0);
        float c = dot(s0_r0, s0_r0) - (sR * sR);
        float delta = b * b - 4.0 * a * c;
        if (delta < 0.0 || a == 0.0) {
            return -1.0;
        }
        float sol0 = (-b - safe_sqrt(delta)) / (2.0*a);
        float sol1 = (-b + safe_sqrt(delta)) / (2.0*a);
        if (sol0 < 0.0 && sol1 < 0.0) {
            return -1.0;
        }
        if (sol0 < 0.0) {
            return max(0.0, sol1);
        } else if (sol1 < 0.0) {
            return max(0.0, sol0);
        }
        return max(0.0, min(sol0, sol1));
    }

]]
