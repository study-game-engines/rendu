#include "Probe.hpp"
#include "graphics/GPUObjects.hpp"
#include "graphics/GLUtilities.hpp"
#include "resources/ResourcesManager.hpp"


Probe::Probe(const glm::vec3 & position, std::shared_ptr<Renderer> renderer, uint size, uint mips, const glm::vec2 & clippingPlanes){
	_renderer = renderer;
	_framebuffer = renderer->createOutput(TextureShape::Cube, size, size, 6, mips);
	_framebuffer->clear(glm::vec4(0.0f), 1.0f);
	_position = position;
	// Texture used to compute irradiance spherical harmonics.
	_copy = _renderer->createOutput(TextureShape::Cube, 16, 16, 6, 1);
	_shCoeffs.resize(9);

	// Compute the camera for each face.
	const std::array<glm::vec3, 6> ups = {
		glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
		glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)
	};
	const glm::vec3 centers[6] = {glm::vec3(1.0, 0.0, 0.0), glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, 0.0, -1.0)};
	for(uint i = 0; i < 6; ++i){
		_cameras[i].pose(position, position + centers[i], ups[i]);
		_cameras[i].projection(1.0f, glm::half_pi<float>(), clippingPlanes[0], clippingPlanes[1]);
		// Generate centered MVPs for post-process steps.
		_mvps[i] = glm::perspective(glm::half_pi<float>(), 1.0f, 0.01f, 10.0f) * glm::lookAt(glm::vec3(0.0f), centers[i], ups[i]);
	}
}

void Probe::draw(){
	for(uint i = 0; i < 6; ++i){
		_renderer->draw(_cameras[i], *_framebuffer, i);
	}
}

void Probe::convolveRadiance(float clamp){

	const auto _integration = Resources::manager().getProgram("cubemap_convo", "skybox_basic", "cubemap_convo");
	const auto mesh			= Resources::manager().getMesh("skybox", Storage::GPU);

	GLUtilities::setDepthState(false);
	GLUtilities::setBlendState(false);
	GLUtilities::setCullState(true);

	_integration->use();
	_integration->uniform("clampMax", clamp);

	for(uint mid = 1; mid < _framebuffer->textureId()->levels; ++mid){
		const uint wh = _framebuffer->textureId()->width / (1 << mid);
		const float roughness = float(mid) / float((_framebuffer->textureId()->levels)-1);
		const int samplesCount = (mid == 1 ? 64 : 128);

		GLUtilities::setViewport(0, 0, int(wh), int(wh));
		_integration->uniform("mipmapRoughness", roughness);
		_integration->uniform("samplesCount", samplesCount);

		for(uint lid = 0; lid < 6; ++lid){
			_framebuffer->bind(lid, mid);
			_integration->uniform("mvp", _mvps[lid]);
			GLUtilities::bindTexture(_framebuffer->textureId(), 0);
			GLUtilities::drawMesh(*mesh);
		}
	}
	_framebuffer->unbind();

	// Downscale radiance to a smaller texture, to be copied on the CPU for SH decomposition.
	for(uint lid = 0; lid < 6; ++lid){
		GLUtilities::blit(*_framebuffer, *_copy, lid, lid, 0, 0, Filter::LINEAR);
	}

}

void Probe::estimateIrradiance(){
	// Download the texture to the CPU.
	Texture & tex = *_copy->textureId();
	GLUtilities::downloadTexture(tex, 0);
	// Compute SH coeffs.
	extractIrradianceSHCoeffs(tex, _shCoeffs);
}

void Probe::clean(){
	_framebuffer->clean();
}

void Probe::extractIrradianceSHCoeffs(const Texture & cubemap, std::vector<glm::vec3> & shCoeffs){
	shCoeffs.resize(9);

	// Indices conversions from cubemap UVs to direction.
	static const std::vector<int> axisIndices = {0, 0, 1, 1, 2, 2};
	static const std::vector<float> axisMul   = {1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f};
	static const std::vector<int> horizIndices = {2, 2, 0, 0, 0, 0};
	static const std::vector<float> horizMul   = {-1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
	static const std::vector<int> vertIndices = {1, 1, 2, 2, 1, 1};
	static const std::vector<float> vertMul   = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, -1.0f};

	// Spherical harmonics coefficients.
	std::array<glm::vec3, 9> LCoeffs = {};
	LCoeffs.fill(glm::vec3(0.0f, 0.0f, 0.0f));
	const float y0 = 0.282095f;
	const float y1 = 0.488603f;
	const float y2 = 1.092548f;
	const float y3 = 0.315392f;
	const float y4 = 0.546274f;

	float denom     = 0.0f;
	const uint side = cubemap.width;
	for(uint i = 0; i < 6; ++i) {
		const auto & currentSide = cubemap.images[i];
		for(uint y = 0; y < side; ++y) {
			for(uint x = 0; x < side; ++x) {

				const float v = -1.0f + 1.0f / float(side) + float(y) * 2.0f / float(side);
				const float u = -1.0f + 1.0f / float(side) + float(x) * 2.0f / float(side);
				glm::vec3 pos		 = glm::vec3(0.0f, 0.0f, 0.0f);
				pos[axisIndices[i]]  = axisMul[i];
				pos[horizIndices[i]] = horizMul[i] * u;
				pos[vertIndices[i]]  = vertMul[i] * v;
				pos					 = glm::normalize(pos);
				// Normalization factor.
				const float fTmp   = 1.0f + u * u + v * v;
				const float weight = 4.0f / (sqrt(fTmp) * fTmp);
				denom += weight;
				// HDR color.
				const size_t pixelPos = (y * side + x) * currentSide.components;
				const glm::vec3 hdr   = weight * glm::vec3(currentSide.pixels[pixelPos + 0],
														   currentSide.pixels[pixelPos + 1],
														   currentSide.pixels[pixelPos + 2]);
				// Y0,0  = 0.282095
				LCoeffs[0] += hdr * y0;
				// Y1,-1 = 0.488603 y
				LCoeffs[1] += hdr * (y1 * pos[1]);
				// Y1,0  = 0.488603 z
				LCoeffs[2] += hdr * (y1 * pos[2]);
				// Y1,1  = 0.488603 x
				LCoeffs[3] += hdr * (y1 * pos[0]);
				// Y2,-2 = 1.092548 xy
				LCoeffs[4] += hdr * (y2 * (pos[0] * pos[1]));
				// Y2,-1 = 1.092548 yz
				LCoeffs[5] += hdr * (y2 * pos[1] * pos[2]);
				// Y2,0  = 0.315392 (3z^2 - 1)
				LCoeffs[6] += hdr * (y3 * (3.0f * pos[2] * pos[2] - 1.0f));
				// Y2,1  = 1.092548 xz
				LCoeffs[7] += hdr * (y2 * pos[0] * pos[2]);
				// Y2,2  = 0.546274 (x^2 - y^2)
				LCoeffs[8] += hdr * (y4 * (pos[0] * pos[0] - pos[1] * pos[1]));
			}
		}
	}
	// Normalization.
	for(auto & coeff : LCoeffs) {
		coeff *= 4.0 / denom;
	}
	// To go from radiance to irradiance, we need to apply a cosine lobe convolution on the sphere in spatial domain.
	// This can be expressed as a product in frequency (on the SH basis) domain, with constant pre-computed coefficients.
	// See:	Ramamoorthi, Ravi, and Pat Hanrahan. "An efficient representation for irradiance environment maps."
	//		Proceedings of the 28th annual conference on Computer graphics and interactive techniques. ACM, 2001.
	const float c1 = 0.429043f;
	const float c2 = 0.511664f;
	const float c3 = 0.743125f;
	const float c4 = 0.886227f;
	const float c5 = 0.247708f;
	shCoeffs[0] = c4 * LCoeffs[0] - c5 * LCoeffs[6];
	shCoeffs[1] = 2.0f * c2 * LCoeffs[1];
	shCoeffs[2] = 2.0f * c2 * LCoeffs[2];
	shCoeffs[3] = 2.0f * c2 * LCoeffs[3];
	shCoeffs[4] = 2.0f * c1 * LCoeffs[4];
	shCoeffs[5] = 2.0f * c1 * LCoeffs[5];
	shCoeffs[6] = c3 * LCoeffs[6];
	shCoeffs[7] = 2.0f * c1 * LCoeffs[7];
	shCoeffs[8] = c1 * LCoeffs[8];
}
