#include "DebugRenderer.hpp"
#include "scene/lights/Light.hpp"
#include "system/System.hpp"
#include "graphics/GLUtilities.hpp"


DebugRenderer::DebugRenderer() : _lightDebugRenderer("object_basic_uniform") {

	const Descriptor desc = {Layout::RGBA8, Filter::LINEAR_LINEAR, Wrap::CLAMP};
	_preferredFormat.push_back(desc);
	_needsDepth = true;

	_sphere = Resources::manager().getMesh("sphere", Storage::GPU);
	_probeProgram = Resources::manager().getProgram("probe_debug");
	_boxesProgram = Resources::manager().getProgram("bboxes_visu", "object_basic", "object_basic_uniform");
	_frameProgram = Resources::manager().getProgram("object_basic_color");

	// Create frame mesh.
	{
		// RGB XYZ Gizmo
		const float arrowLength = 0.1f;
		const std::vector<glm::vec3> positions = {
			glm::vec3(-1.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(1.0f-2.0f*arrowLength, -arrowLength, -arrowLength),
			glm::vec3(1.0f-2.0f*arrowLength, -arrowLength,  arrowLength),
			glm::vec3(1.0f-2.0f*arrowLength,  arrowLength,  arrowLength),
			glm::vec3(1.0f-2.0f*arrowLength,  arrowLength, -arrowLength)
		};
		const std::vector<uint> indices = { 0, 1, 0, 1, 2, 1, 1, 3, 1, 1, 4, 1, 1, 5, 1, 2, 3, 2, 3, 4, 3, 4, 5, 4, 5, 2, 5};
		for(uint i = 0; i < 3; ++i) {
			const uint id0 = i;
			const uint id1	 = (i + 1) % 3;
			const uint id2 = (i + 2) % 3;
			const glm::vec3 axeColor(float(i == 0), float(i == 1), float(i == 2));
			for(const auto & pos : positions){
				_frame.positions.emplace_back(pos[id0], pos[id1], pos[id2]);
				_frame.colors.emplace_back(axeColor);
			}
			const uint baseId = i * uint(positions.size());
			for(const auto ind : indices){
				_frame.indices.push_back(ind + baseId);
			}
		}
		// Y = 0 grid
		const float extent = 10.0f;
		for(float dz = -extent; dz <= extent; ++dz){
			const uint baseId = uint(_frame.positions.size());
			_frame.positions.emplace_back(-extent, 0.0f, dz);
			_frame.positions.emplace_back( extent, 0.0f, dz);
			_frame.colors.emplace_back(0.3f, 0.3f, 0.3f);
			_frame.colors.emplace_back(0.3f, 0.3f, 0.3f);
			_frame.indices.push_back(baseId);
			_frame.indices.push_back(baseId + 1);
			_frame.indices.push_back(baseId);
		}
		for(float dx = -extent; dx <= extent; ++dx){
			const uint baseId = uint(_frame.positions.size());
			_frame.positions.emplace_back(dx, 0.0f, -extent);
			_frame.positions.emplace_back(dx, 0.0f,  extent);
			_frame.colors.emplace_back(0.3f, 0.3f, 0.3f);
			_frame.colors.emplace_back(0.3f, 0.3f, 0.3f);
			_frame.indices.push_back(baseId);
			_frame.indices.push_back(baseId + 1);
			_frame.indices.push_back(baseId);
		}
	}
	_frame.upload();
	checkGLError();
}

void DebugRenderer::setScene(const std::shared_ptr<Scene> & scene) {
	// Do not accept a null scene.
	if(!scene) {
		return;
	}
	_scene = scene;
	updateSceneMesh();
	checkGLError();
}

void DebugRenderer::draw(const Camera & camera, Framebuffer & framebuffer, size_t layer) {

	if(!_scene){
		return;
	}

	const glm::mat4 & view = camera.view();
	const glm::mat4 & proj = camera.projection();
	const glm::mat4 vp = proj * view;

	_lightDebugRenderer.updateCameraInfos(view, proj);

	framebuffer.bind(layer);
	GLUtilities::setDepthState(true);


	GLUtilities::setPolygonState(PolygonMode::LINE, Faces::ALL);
	GLUtilities::setCullState(false);

	if(_showLights){
		for(const auto & light : _scene->lights){
			light->draw(_lightDebugRenderer);
		}
	}

	if(_showBoxes){
		if(_scene->animated()){
			updateSceneMesh();
		}
		_boxesProgram->use();
		_boxesProgram->uniform("mvp", vp);
		_boxesProgram->uniform("color", glm::vec4(1.0f,0.9f,0.2f, 1.0f));
		GLUtilities::drawMesh(_sceneBoxes);
	}

	if(_showFrame){
		_frameProgram->use();
		_frameProgram->uniform("mvp", vp);
		GLUtilities::drawMesh(_frame);

	}

	GLUtilities::setCullState(true);
	GLUtilities::setPolygonState(PolygonMode::FILL, Faces::ALL);
	// Render probe.
	if(_showProbe){
		// Combine the three matrices.
		const glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f),  _scene->environment.position()), glm::vec3(0.15f));
		const glm::mat4 MVP = vp * model;
		const glm::mat3 normalMat(glm::inverse(glm::transpose(model)));
		_probeProgram->use();
		_probeProgram->uniform("mvp", MVP);
		_probeProgram->uniform("m", model);
		_probeProgram->uniform("normalMatrix", normalMat);
		_probeProgram->uniform("camPos", camera.position());
		_probeProgram->uniform("lod", _probeRoughness * _scene->environment.map()->levels);
		_probeProgram->uniform("mode", int(_probeMode));
		GLUtilities::bindTexture(_scene->environment.map(), 0);
		GLUtilities::bindBuffer(*_scene->environment.shCoeffs(), 0);
		GLUtilities::drawMesh(*_sphere);
	}
	GLUtilities::setDepthState(false);
	framebuffer.unbind();

	checkGLError();
}

void DebugRenderer::updateSceneMesh(){
	// Compute bounding boxes mesh and upload.
	_sceneBoxes.clean();

	// Degenerate lines.
	const std::vector<unsigned int> indices = { 0, 1, 0, 0, 2, 0, 1, 3, 1, 2, 3, 2, 4, 5, 4, 4, 6, 4, 5, 7, 5, 6, 7, 6, 1, 5, 1, 0, 4, 0, 2, 6, 2, 3, 7, 3};

	// Generate the geometry for all objects.
	for(const auto & obj : _scene->objects) {
		const unsigned int firstIndex = uint(_sceneBoxes.positions.size());
		const auto corners = obj.boundingBox().getCorners();
		for(const auto & corner : corners) {
			_sceneBoxes.positions.push_back(corner);
		}
		for(const unsigned int iid : indices) {
			_sceneBoxes.indices.push_back(firstIndex + iid);
		}
	}
	_sceneBoxes.upload();
}

void DebugRenderer::clean() {
	_sceneBoxes.clean();
}

void DebugRenderer::interface(){
	ImGui::Checkbox("Show bboxes", &_showBoxes); ImGui::SameLine();
	ImGui::Checkbox("Show frame", &_showFrame); 
	ImGui::Checkbox("Show lights", &_showLights); ImGui::SameLine();
	ImGui::Checkbox("Show probe", &_showProbe);
	if(_showProbe){
		ImGui::PushItemWidth(80);
		ImGui::Combo("Mode##debugProbe", reinterpret_cast<int*>(&_probeMode), "Irradiance\0Radiance\0\0");
		ImGui::SameLine();
		if(ImGui::SliderFloat("Roughness##debugProbe", &_probeRoughness, 0.0f, 1.0f)){
			_probeRoughness = glm::clamp(_probeRoughness, 0.0f, 1.0f);
		}
		ImGui::PopItemWidth();
	}
}