#pragma once
#include <foundation/api_types.h>
#include <plugins/renderer/renderer_api_types.h>

#define TM_TT_TYPE__ATMOSPHERIC_SKY_COMPONENT "tm_atmospheric_sky_component"
#define TM_TT_TYPE_HASH__ATMOSPHERIC_SKY_COMPONENT TM_STATIC_HASH("tm_atmospheric_sky_component", 0xfb5b98241d6f7939ULL)

#define TM_ATMOSPHERIC_SKY_TRANSMITTANCE TM_STATIC_HASH("transmittance_lut", 0xbc5cde866678958cULL)
#define TM_ATMOSPHERIC_SKY_MULTI_SCATTERING TM_STATIC_HASH("multi_scattering_lut", 0x6e7cc7bf269613a3ULL)
#define TM_ATMOSPHERIC_SKY_SKYVIEW_LUT TM_STATIC_HASH("sky_view_lut", 0x5848117d59ff7743ULL)

enum {
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__SUN_DIRECTION,     // SUBOBJECT(TM_TT_TYPE__VEC3)
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__CELESTIAL_BODY_CENTER, // SUBOBJECT(TM_TT_TYPE__VEC3)
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__OFFSET_FROM_CENTER,     // SUBOBJECT(TM_TT_TYPE__VEC3)
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__SUN_ANGULAR_RADIUS, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__BOTTOM_RADIUS, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__TOP_RADIUS, // float

    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_WIDTH, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_EXP_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_EXP_SCALE, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_LINEAR_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY0_CONSTANT_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_WIDTH, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_EXP_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_EXP_SCALE, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_LINEAR_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_DENSITY1_CONSTANT_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__RAYLEIGH_SCATTERING, // SUBOBJECT(TM_TT_TYPE__VEC3)

    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_WIDTH, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_EXP_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_EXP_SCALE, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_LINEAR_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY0_CONSTANT_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_WIDTH, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_EXP_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_EXP_SCALE, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_LINEAR_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_DENSITY1_CONSTANT_TERM, // float

    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_SCATTERING,// SUBOBJECT(TM_TT_TYPE__VEC3)
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_EXTINCTION, // SUBOBJECT(TM_TT_TYPE__VEC3)
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_ABSORPTION, // SUBOBJECT(TM_TT_TYPE__VEC3)
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__MIE_PHASE_FUNCTION_G, // float

    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_WIDTH, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_EXP_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_EXP_SCALE, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_LINEAR_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY0_CONSTANT_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_WIDTH, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_EXP_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_EXP_SCALE, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_LINEAR_TERM, // float
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_DENSITY1_CONSTANT_TERM, // float

    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__ABSORPTION_EXTINCTION, // SUBOBJECT(TM_TT_TYPE__VEC3)
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__GROUND_ALBEDO, // SUBOBJECT(TM_TT_TYPE__VEC3)
    TM_TT_PROP__ATMOSPHERIC_SKY_COMPONENT__GLOBAL_ILUMINANCE, // SUBOBJECT(TM_TT_TYPE__VEC3)

};
