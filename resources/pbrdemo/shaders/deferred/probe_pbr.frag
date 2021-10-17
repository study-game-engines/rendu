
#include "common_pbr.glsl"
#include "utils.glsl"

layout(set = 2, binding = 0) uniform texture2D albedoTexture; ///< The albedo texture.
layout(set = 2, binding = 1) uniform texture2D normalTexture; ///< The normal texture.
layout(set = 2, binding = 2) uniform texture2D effectsTexture; ///< The effects texture.
layout(set = 2, binding = 3) uniform texture2D depthTexture; ///< The depth texture.
layout(set = 2, binding = 4) uniform texture2D ssaoTexture; ///< The SSAO texture.
layout(set = 2, binding = 5) uniform texture2D brdfPrecalc; ///< Preintegrated BRDF lookup table.
layout(set = 2, binding = 6) uniform textureCube textureCubeMap; ///< Background environment cubemap (with preconvoluted versions of increasing roughness in mipmap levels).

/// SH approximation of the environment probe irradiance (UBO).
layout(std140, set = 3, binding = 0) uniform SHCoeffs {
	vec4 shCoeffs[9];
};

layout(set = 0, binding = 0) uniform UniformBlock {
	mat4 inverseV; ///< The view to world transformation matrix.
	vec4 projectionMatrix; ///< The camera projection matrix.
	vec3 cubemapPos; ///< The cubemap location.
	vec3 cubemapCenter; ///< The cubemap parallax box center.
	vec3 cubemapExtent; ///< The cubemap parallax box half size.
	vec3 cubemapSize; ///< The cubemap effect box half size.
	vec2 cubemapCosSin; ///< The cubemap parallax box orientation (precomputed cos/sin).
	float cubemapFade; ///< The cubemap effect box fade zone size.
	float maxLod; ///< Mip level count for the cubemap.
};

layout(location = 0) out vec4 fragColor; ///< Color.

/** Compute the environment probe ambient lighting contribution on the scene. */
void main(){

	vec2 uv = gl_FragCoord.xy/textureSize(depthTexture, 0).xy;
	Material material = decodeMaterialFromGbuffer(uv, albedoTexture, normalTexture, effectsTexture);
	// Retrieve world space position.

	float depth = textureLod(sampler2D(depthTexture, sClampNear), uv, 0.0).r;
	vec3 position = positionFromDepth(depth, uv, projectionMatrix);
	vec3 worldP = vec3(inverseV * vec4(position, 1.0));

	// Test if we are inside the effect box or the fade region.
	float weight = probeWeight(worldP, cubemapPos, cubemapSize, cubemapCosSin, cubemapFade);
	if(weight <= 0.0f){
		discard;
	}
	vec3 v = normalize(-position);

	// Sample illumination envmap using world space normal and SH pre-computed coefficients.
	vec3 worldN = normalize(vec3(inverseV * vec4(material.normal, 0.0)));
	vec3 worldV = normalize(inverseV[3].xyz - worldP);
	
	vec3 radiance = radiance(worldN, worldV, worldP, material.roughness, textureCubeMap, cubemapPos, cubemapCenter, cubemapExtent, cubemapCosSin, maxLod);
	vec3 irradiance = applySH(worldN, shCoeffs);

	float NdotV = max(0.0, dot(v, material.normal));
	// BRDF contributions.
	vec3 diffuse, specular;
	ambientBrdf(material, NdotV, brdfPrecalc, diffuse, specular);

	// Ambient occlusion.
	float precomputedAO = material.ao;
	float realtimeAO = textureLod(sampler2D(ssaoTexture, sClampLinear), uv, 0).r;
	float aoDiffuse = min(realtimeAO, precomputedAO);
	float aoSpecular = approximateSpecularAO(aoDiffuse, NdotV, material.roughness);

	fragColor.rgb = weight * (aoDiffuse * diffuse * irradiance + aoSpecular * specular * radiance);
	
	// Apply weighting.
	fragColor.a = weight;
}
