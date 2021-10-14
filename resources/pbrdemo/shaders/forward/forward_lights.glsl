
#include "constants.glsl"
#include "shadow_maps.glsl"

#define POINT 0
#define DIRECTIONAL 1
#define SPOT 2

#define MAX_LIGHTS_COUNT 50
#define MAX_PROBES_COUNT 4

/** \brief Represent a light in the forward renderer. */
struct GPULight {
	mat4 viewToLight; ///< View to light matrix.
	vec4 colorAndBias; ///< Light tint and shadow bias.
	vec4 positionAndRadius; ///< Light position and effect radius.
	vec4 directionAndPlane; ///< Light direction and far plane distance.
	vec4 typeModeLayer; ///< Light type, shadow mode and shadow map layer.
	vec4 angles; ///< Cone inner and outer angles.
};

/** \brief Represent an environment probe in the forward renderer. */
struct GPUProbe {
	vec4 positionAndMip; ///< The cubemap location and the mip count.
	vec4 sizeAndFade;	 ///< The cubemap box effect size, and the size of its fading region on edges.
	vec4 centerAndCos; ///< The cubemap parallax box center, and the cubemap parallax box orientation (precomputed cos).
	vec4 extentAndSin; ///< The cubemap parallax box half size, and the cubemap parallax box orientation (precomputed sin).
};

/** Compute a light contribution for a given point in forward shading.
 \param light the light information
 \param viewSpacePos the point position in view space
 \param smapCube the cube shadow maps
 \param smap2D the 2D shadow maps
 \param l will contain the light direction for the point
 \param shadowing will contain the shadowing factor
 \return true if the light contributes to the point shading
 */
bool applyLight(GPULight light, vec3 viewSpacePos, textureCubeArray smapCube, texture2DArray smap2D, out vec3 l, out float shadowing){

	shadowing = 1.0;

	int lightType = int(light.typeModeLayer[0]);
	int shadowMode = int(light.typeModeLayer[1]);
	int layer = int(light.typeModeLayer[2]);
	if(lightType == POINT){
		vec3 deltaPosition = light.positionAndRadius.xyz - viewSpacePos;
		// Early exit if we are outside the sphere of influence.
		float lightRadius = light.positionAndRadius.w;
		if(length(deltaPosition) > lightRadius){
			return false;
		}
		// Light direction: from the surface point to the light point.
		l = normalize(deltaPosition);
		// Attenuation with increasing distance to the light.
		float localRadius2 = dot(deltaPosition, deltaPosition);
		float radiusRatio2 = localRadius2/(lightRadius*lightRadius);
		float attenNum = clamp(1.0 - radiusRatio2, 0.0, 1.0);
		shadowing = attenNum*attenNum;
		// Shadowing.
		int shadowMode = int(light.typeModeLayer[1]);
		if(shadowMode != SHADOW_NONE){
			// Compute the light to surface vector in light centered space.
			// We only care about the direction, so we don't need the translation.
			vec3 deltaPositionWorld = -mat3(light.viewToLight) * deltaPosition;
			shadowing *= shadowCube(shadowMode, deltaPositionWorld, smapCube, layer, light.directionAndPlane.w, light.colorAndBias.w);
		}
	} else if(lightType == DIRECTIONAL){
		l = normalize(-light.directionAndPlane.xyz);
		// Shadowing
		int shadowMode = int(light.typeModeLayer[1]);
		if(shadowMode != SHADOW_NONE){
			vec3 lightSpacePosition = (light.viewToLight * vec4(viewSpacePos, 1.0)).xyz;
			lightSpacePosition.xy = 0.5 * lightSpacePosition.xy + 0.5;
			shadowing *= shadow(shadowMode, lightSpacePosition, smap2D, layer, light.colorAndBias.w);
		}
	} else if(lightType == SPOT){
		vec3 deltaPosition = light.positionAndRadius.xyz - viewSpacePos;
		float lightRadius = light.positionAndRadius.w;
		// Early exit if we are outside the sphere of influence.
		if(length(deltaPosition) > lightRadius){
			return false;
		}
		l = normalize(deltaPosition);
		// Compute the angle between the light direction and the (light, surface point) vector.
		float currentAngleCos = dot(l, -normalize(light.directionAndPlane.xyz));
		vec2 intOutAnglesCos = light.angles.xy;
		// If we are outside the spotlight cone, no lighting.
		if(currentAngleCos < intOutAnglesCos.y){
			return false;
		}
		// Compute the spotlight attenuation factor based on our angle compared to the inner and outer spotlight angles.
		float angleAttenuation = clamp((currentAngleCos - intOutAnglesCos.y)/(intOutAnglesCos.x - intOutAnglesCos.y), 0.0, 1.0);
		// Attenuation with increasing distance to the light.
		float localRadius2 = dot(deltaPosition, deltaPosition);
		float radiusRatio2 = localRadius2/(lightRadius*lightRadius);
		float attenNum = clamp(1.0 - radiusRatio2, 0.0, 1.0);
		shadowing = angleAttenuation * attenNum * attenNum;

		// Shadowing
		if(shadowMode != SHADOW_NONE){
			vec4 lightSpacePosition = (light.viewToLight) * vec4(viewSpacePos,1.0);
			lightSpacePosition /= lightSpacePosition.w;
			lightSpacePosition.xy = 0.5 * lightSpacePosition.xy + 0.5;
			shadowing *= shadow(shadowMode, lightSpacePosition.xyz, smap2D, layer, light.colorAndBias.w);
		}
	}
	return true;
}

/** Compute an environment probe contribution for a given point in forward shading.
 \param probe the probe information
 \param n the surface normal (world space)
 \param v the view direction (world space)
 \param p the surface position (world space)
 \param roughness the surface roughness
 \param cubeMap the environment map texture
 \return the retrieved radiance weighted by the probe contribution (the weight is also stored in the w component)
 */
vec4 applyProbe(GPUProbe probe, vec3 n, vec3 v, vec3 p, float roughness, textureCube cubeMap){
	float lod = probe.positionAndMip.w;
	vec2 cosSinOrientation = vec2(probe.centerAndCos.w, probe.extentAndSin.w);
	vec3 probePosition = probe.positionAndMip.xyz;
	vec3 rad = radiance(n, v, p, roughness, cubeMap, probePosition, probe.centerAndCos.xyz, probe.extentAndSin.xyz, cosSinOrientation, lod);
	float weight = probeWeight(p, probePosition, probe.sizeAndFade.xyz, cosSinOrientation, probe.sizeAndFade.w);
	return weight * vec4(rad, 1.0);
}
