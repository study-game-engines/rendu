#include <stdio.h>
#include <iostream>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>


#include "Object.h"


Object::Object() {}

Object::~Object() {}

void Object::init(const std::string& meshPath, const std::vector<std::pair<std::string, bool>>& texturesPaths, const Object::Type & type) {

	_material = static_cast<int>(type);

	// Load the shaders
	_programDepth = Resources::manager().getProgram("object_depth");
	_programDepth->registerUniform("mvp");

	switch (_material) {
	case Object::Parallax:
			_program = Resources::manager().getProgram("parallax_gbuffer");
			_program->registerUniforms({ "mvp", "mv", "p", "normalMatrix", "materialId" });
		break;
	case Object::Regular:
	default:
			_program = Resources::manager().getProgram("object_gbuffer");
			_program->registerUniforms({ "mvp", "normalMatrix", "materialId" });
		break;
	}

	// Load geometry.
	_mesh = Resources::manager().getMesh(meshPath);

	// Load and upload the textures.
	for (unsigned int i = 0; i < texturesPaths.size(); ++i) {
		const auto & textureName = texturesPaths[i];
		_textures.push_back(Resources::manager().getTexture(textureName.first, textureName.second));
		_program->registerTexture("texture" + std::to_string(i), i);
	}

	checkGLError();

}

void Object::update(const glm::mat4& model) {

	_model = model;

}


void Object::draw(const glm::mat4& view, const glm::mat4& projection) const {

	// Combine the three matrices.
	glm::mat4 MV = view * _model;
	glm::mat4 MVP = projection * MV;

	// Compute the normal matrix
	glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(MV)));
	// Select the program (and shaders).
	glUseProgram(_program->id());


	// Upload the MVP matrix.
	glUniformMatrix4fv(_program->uniform("mvp"), 1, GL_FALSE, &MVP[0][0]);
	
	
	// Upload the normal matrix.
	glUniformMatrix3fv(_program->uniform("normalMatrix"), 1, GL_FALSE, &normalMatrix[0][0]);
	
	// Material id.
	glUniform1i(_program->uniform("materialId"), _material);

	if (_material == Type::Parallax) {
		// Upload the projection matrix.
		glUniformMatrix4fv(_program->uniform("p"), 1, GL_FALSE, &projection[0][0]);
		// Upload the MV matrix.
		glUniformMatrix4fv(_program->uniform("mv"), 1, GL_FALSE, &MV[0][0]);
	}

	// Bind the textures.
	for (unsigned int i = 0; i < _textures.size(); ++i){
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(_textures[i].cubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, _textures[i].id);
	}
	
	
	// Select the geometry.
	glBindVertexArray(_mesh.vId);
	// Draw!
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _mesh.eId);
	glDrawElements(GL_TRIANGLES, _mesh.count, GL_UNSIGNED_INT, (void*)0);

	glBindVertexArray(0);
	glUseProgram(0);
	
	
}


void Object::drawDepth(const glm::mat4& lightVP) const {
	
	// Combine the three matrices.
	glm::mat4 lightMVP = lightVP * _model;
	
	glUseProgram(_programDepth->id());
	
	// Upload the MVP matrix.
	glUniformMatrix4fv(_programDepth->uniform("mvp"), 1, GL_FALSE, &lightMVP[0][0]);
	
	// Select the geometry.
	glBindVertexArray(_mesh.vId);
	// Draw!
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _mesh.eId);
	glDrawElements(GL_TRIANGLES, _mesh.count, GL_UNSIGNED_INT, (void*)0);
	
	glBindVertexArray(0);
	glUseProgram(0);
	
}


void Object::clean() const {
	glDeleteVertexArrays(1, &_mesh.vId);
	for (auto & texture : _textures) {
		glDeleteTextures(1, &(texture.id));
	}
	glDeleteProgram(_program->id());
}


