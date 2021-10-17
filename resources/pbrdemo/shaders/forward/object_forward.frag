
#include "common_pbr.glsl"
#include "forward_lights.glsl"

layout(location = 0) in INTERFACE {
    mat4 tbn; ///< Normal to view matrix.
	vec4 viewSpacePosition; ///< View space position.
	vec2 uv; ///< UV coordinates.
} In ;

layout(set = 2, binding = 0) uniform texture2D albedoTexture; ///< Albedo.
layout(set = 2, binding = 1) uniform texture2D normalTexture; ///< Normal map.
layout(set = 2, binding = 2) uniform texture2D effectsTexture; ///< Effects map.
layout(set = 2, binding = 4) uniform texture2D brdfPrecalc; ///< Preintegrated BRDF lookup table.
layout(set = 2, binding = 5) uniform textureCube textureProbes[MAX_PROBES_COUNT]; ///< Background environment cubemaps (with preconvoluted versions of increasing roughness in mipmap levels).
layout(set = 2, binding = 6) uniform texture2DArray shadowMaps2D; ///< Shadow maps array.
layout(set = 2, binding = 7) uniform textureCubeArray shadowMapsCube; ///< Shadow cubemaps array.
layout(set = 2, binding = 8) uniform texture2D ssaoTexture; ///< Ambient occlusion.

layout(set = 0, binding = 0) uniform UniformBlock {
	mat4 inverseV; ///< The view to world transformation matrix.
	vec2 invScreenSize; ///< Destination size.
	int lightsCount; ///< Number of active lights.
	int probesCount; ///< Number of active envmaps.
	bool hasUV; ///< Does the mesh have UV coordinates.
};

/// Store the lights in a continuous buffer (UBO).
layout(std140, set = 3, binding = 0) uniform Lights {
	GPULight lights[MAX_LIGHTS_COUNT];
};

/// Store the probes in a continuous buffer (UBO).
layout(std140, set = 3, binding = 1) uniform Probes {
	GPUProbe probes[MAX_PROBES_COUNT];
};

///SH approximations of the environment irradiance (UBO). 
layout(std140, set = 3, binding = 2) uniform SHCoeffs {
	vec4 coeffs[9];
} probesSH[MAX_PROBES_COUNT];


layout (location = 0) out vec4 fragColor; ///< Shading result.

/** Shade the object, applying lighting. */
void main(){
	
	vec4 albedoInfos = texture(sampler2D(albedoTexture, sRepeatLinearLinear), In.uv);
	if(albedoInfos.a <= 0.01){
		discard;
	}
	Material material = initMaterial();
	material.id = MATERIAL_STANDARD;
	material.reflectance = albedoInfos.rgb;
	
	// Flip the up of the local frame for back facing fragments.
	mat3 tbn = mat3(In.tbn);
	tbn[2] *= (gl_FrontFacing ? 1.0 : -1.0);
	// Compute the normal at the fragment using the tangent space matrix and the normal read in the normal map.
	// If we dont have UV, use the geometric normal.
	vec3 n;
	if(hasUV){
		n = texture(sampler2D(normalTexture, sRepeatLinearLinear), In.uv).rgb ;
		n = normalize(n * 2.0 - 1.0);
		n = normalize(tbn * n);
	} else {
		n = normalize(tbn[2]);
	}
	material.normal = n;

	vec3 infos = texture(sampler2D(effectsTexture, sRepeatLinearLinear), In.uv).rgb;
	material.roughness = max(0.045, infos.r);
	material.ao = infos.b;
	material.metalness = infos.g;

	vec3 v = normalize(-In.viewSpacePosition.xyz);

	// Sample illumination envmaps using world space normal and SH pre-computed coefficients.
	vec3 worldN = normalize(vec3(inverseV * vec4(material.normal, 0.0)));
	vec3 worldP = vec3(inverseV * vec4(In.viewSpacePosition.xyz, 1.0));
	vec3 worldV = normalize(inverseV[3].xyz - worldP);

	// Accumulate envmaps contributions.
	vec3 irradiance = vec3(0.0);
	vec4 radiance = vec4(0.0);
	for(int pid = 0; pid < MAX_PROBES_COUNT; ++pid){
		if(pid >= probesCount){
			break;
		}
		vec4 radianceAndWeight = applyProbe(probes[pid], worldN, worldV, worldP, material.roughness, textureProbes[pid]);
		// Weight based on proximity to probe.
		radiance += radianceAndWeight;
		irradiance += radianceAndWeight.w * applySH(worldN, probesSH[pid].coeffs);
	}
	// Normalize weighted sum of probes contributions.
	if(radiance.a != 0.0){
		radiance.rgb /= radiance.a;
		irradiance /= radiance.a;
	}

	float NdotV = max(0.0, dot(v, material.normal));
	// BRDF contributions.
	vec3 diffuse, specular;
	ambientBrdf(material, NdotV, brdfPrecalc, diffuse, specular);

	vec2 screenUV = gl_FragCoord.xy * invScreenSize;
	float realtimeAO = textureLod(sampler2D(ssaoTexture, sClampLinear), screenUV, 0).r;
	float precomputedAO = material.ao;
	float aoDiffuse = min(realtimeAO, precomputedAO);
	float aoSpecular = approximateSpecularAO(aoDiffuse, NdotV, material.roughness);

	fragColor = vec4(aoDiffuse * diffuse * irradiance + aoSpecular * specular * radiance.rgb, 1.0);

	for(int lid = 0; lid < MAX_LIGHTS_COUNT; ++lid){
		if(lid >= lightsCount){
			break;
		}
		float shadowing;
		vec3 l;
		if(!applyLight(lights[lid], In.viewSpacePosition.xyz, shadowMapsCube, shadowMaps2D, l, shadowing)){
			continue;
		}

		vec3 diffuseL, specularL;
		directBrdf(material, material.normal, v, l, diffuseL, specularL);
		fragColor.rgb += shadowing * (diffuseL + specularL) * lights[lid].colorAndBias.rgb;
	}
}
