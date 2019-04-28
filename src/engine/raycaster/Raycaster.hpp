#pragma once
#include "Common.hpp"
#include "resources/MeshUtilities.hpp"

/**
 \brief
 \ingroup Engine
 */
class Raycaster {
public:
	Raycaster();

	void addMesh(const Mesh & mesh);
	
	struct RayHit {
		bool hit;
		float dist;
		float u;
		float v;
		unsigned long localId;
		unsigned long meshId;
		
		RayHit();
		
		RayHit(float distance, float uu, float vv, unsigned long lid, unsigned long mid);
		
	};
	
	const RayHit intersects(const glm::vec3 & origin, const glm::vec3 & direction) const;
	
private:
	
	struct TriangleInfos {
		unsigned long v0;
		unsigned long v1;
		unsigned long v2;
		unsigned long localId;
		unsigned int meshId;
	};
	
	struct Ray {
		const glm::vec3 pos;
		const glm::vec3 dir;
		
		Ray(const glm::vec3 & origin, const glm::vec3 & direction);
		
	};
	
	const RayHit intersects(const Ray & ray, const TriangleInfos & tri) const;
	
	std::vector<TriangleInfos> _triangles;
	std::vector<glm::vec3> _vertices;
	
	unsigned int _meshCount = 0;
};
