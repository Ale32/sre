#include "gameobject/GameObjectLoader.h"
#include "Engine.h"
#include "rendering/mesh/MeshLoader.h"
#include "rendering/materials/BlinnPhongMaterial.h"
#include "gameobject/Transform.h"
#include "skeletalAnimation/SkeletralAnimationControllerComponent.h"
#include "skeletalAnimation/SkeletalAnimationLoader.h"
#include "geometry/BoundingBox.h"
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_access.hpp>

std::map<std::string, Mesh> GameObjectLoader::mMeshCache;

/** Transpose the assimp aiMatrix4x4 which is a row major matrix
  * @param a row major assimp matrix
  * @return a column major glm matrix */
glm::mat4 convertMatrix(const aiMatrix4x4 &aiMat)
{
    return {
        aiMat.a1, aiMat.b1, aiMat.c1, aiMat.d1,
        aiMat.a2, aiMat.b2, aiMat.c2, aiMat.d2,
        aiMat.a3, aiMat.b3, aiMat.c3, aiMat.d3,
        aiMat.a4, aiMat.b4, aiMat.c4, aiMat.d4
    };
}

GameObjectEH GameObjectLoader::processNode(aiNode* node, const aiScene* scene)
{
    GameObjectEH go = Engine::gameObjectManager.createGameObject();
    go->name = std::string{node->mName.C_Str()};

    //std::cout << "at node " << node->mName.C_Str() << "\n";
    for (std::uint32_t i = 0; i < node->mNumMeshes; ++i) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        //std::cout << "loading mesh " << mesh->mName.C_Str() << "\n";
        processMesh(go, node, i, mesh, scene);
    }

    for (std::uint32_t i = 0; i < node->mNumChildren; ++i) {

		// bones are also part of the node hierarchy
		// but they should not be processed here
		if (isBone(node->mChildren[i]))
			continue;

		/* This must be a two steps process:
		 * When go->transform is evaluated it returns a pointer to an element of a vector,
		 * when a new child is added this pointer may become invalid.
		 * Hence, using go->transform.addChild(processNode(node->mChildren[i], scene)); causes
		 * undefined behavior since go->transform is evaluated before a new game object is added */
		auto child = processNode(node->mChildren[i], scene);
        go->transform.addChild(child);
    }

    /* assimp only provides the transformation matrix relative to the parent node
     * hence we need to extract position, rotation and scale to obtain the local
     * components of that matrix */
	glm::vec3 position, scale;
	glm::quat rotation;
	decompose(convertMatrix(node->mTransformation), position, rotation, scale);

    /* There is no need to use local transformations since when the following code is executed
     * all the children have their position already set relative to the parent (thanks to the
     * decomposed matrix). When the following transformations are applied they also affect the
     * children of this node */
    go->transform.setPosition(position);
    go->transform.setRotation(rotation);
    go->transform.setScale(scale);

    return go;
}

void GameObjectLoader::processMesh(const GameObjectEH& go, aiNode* node, int meshNumber, aiMesh* mesh, const aiScene* scene)
{
	// builds the parents name
	std::string parents;
	aiNode* current = node;
	while (current != nullptr) {
		parents += current->mName.C_Str();
		current = current->mParent;
	}

	// creates the cache name for this mesh
	std::string cacheName = mFilePath + parents + std::string{ mesh->mName.C_Str() } + std::to_string(meshNumber);

	// loads material
	MaterialPtr loadedMaterial = processMaterial(mesh, scene, cacheName);
	if (loadedMaterial == nullptr) {
		std::cerr << "Mesh " << mesh->mName.C_Str() << " does not have a corresponding material, discarded\n";
		return;
	}

	auto cachedMesh = mMeshCache.find(cacheName);
	if (cachedMesh != mMeshCache.end()) {
		go->addMesh(cachedMesh->second, loadedMaterial);
		return;
	}

	// warning messages
	if (!mesh->HasNormals())
		std::cout << "Mesh: " << mesh->mName.C_Str() << " node: " << node->mName.C_Str() << ": cannot find normals, setting them to (0, 0, 0)\n";

	if (!mesh->HasTextureCoords(0))
		std::cout << "Mesh: " << mesh->mName.C_Str() << " node: " << node->mName.C_Str() << ": cannot find uv coords, setting them to (0, 0)\n";

	bool needsTangents = ((scene->mMaterials[mesh->mMaterialIndex]->GetTextureCount(aiTextureType_HEIGHT) != 0
		|| scene->mMaterials[mesh->mMaterialIndex]->GetTextureCount(aiTextureType_DISPLACEMENT)) && !mesh->HasTangentsAndBitangents());
	if (needsTangents)
		std::cout << "Mesh: " << mesh->mName.C_Str() << " node: " << node->mName.C_Str() << ": has bump map/parallax map but no tangent data, will be calculated\n";

    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texCoords;
	std::vector<float> tangents;
	std::vector<std::int32_t> influencingBones;
	std::vector<float> boneWeights;

    std::vector<Vertex> vertices;
	std::vector<glm::vec3> positionsForBoundingBox;
    std::vector<std::uint32_t> indices;

    // Load vertex data
    for (std::uint32_t i = 0; i < mesh->mNumVertices; ++i) {
        Vertex v;
		
		auto& bones = getInfluencingBones(i, mesh);
		auto& weights = getInfluencingBonesWeights(i, mesh);
		influencingBones.insert(influencingBones.end(), bones.begin(), bones.end());
		boneWeights.insert(boneWeights.end(), weights.begin(), weights.end());

        // position
        positions.insert(positions.end(), {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z});
        v.position = glm::vec3{mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};

        // normal, if not present 0, 0, 0 is used
        if (mesh->HasNormals()) {
            normals.insert(normals.end(), {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z});
            v.normal = glm::vec3{mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
        } else {
            normals.insert(normals.end(), {0.0f, 0.0f, 0.0f});
            v.normal = glm::vec3{0.0f, 0.0f, 0.0f};
        }

        // if texture coordinates are not available just put (0, 0)
        if (mesh->HasTextureCoords(0)) {
            texCoords.insert(texCoords.end(), {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y});
            v.texCoord = glm::vec2{mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
        } else {
            texCoords.insert(texCoords.end(), {0.0f, 0.0f});
            v.texCoord = glm::vec2{0.0f, 0.0f};
        }

		if (mesh->HasTangentsAndBitangents()) {
			aiVector3D& tangent = mesh->mTangents[i];
			tangents.insert(tangents.end(), { tangent.x, tangent.y, tangent.z });
			// TODO compute direction of bitangent as an int (+1 or -1) to pass to shader
		}

        vertices.push_back(v);
		positionsForBoundingBox.push_back(v.position);
    }

    // Load indices
    for (std::uint32_t i = 0; i < mesh->mNumFaces; ++i) {
        aiFace& face = mesh->mFaces[i];
		for (std::uint32_t j = 0; j < face.mNumIndices; j++) {
			indices.push_back(face.mIndices[j]);
		}
    }

	std::vector<int32_t> bitangentSign;
	if (needsTangents)
		computeTangentsAndBitangentSign(mesh, tangents, bitangentSign);

    MeshLoader loader;
    loader.loadData(positions.data(), positions.size(), 3);
    loader.loadData(normals.data(), normals.size(), 3);
    loader.loadData(texCoords.data(), texCoords.size(), 2);

	if (tangents.size() != 0)
		loader.loadData(tangents.data(), tangents.size(), 3);

	if (mesh->mNumBones != 0) { // add bone data only if this mesh needs it
		loader.loadData(influencingBones.data(), influencingBones.size(), 4, GL_ARRAY_BUFFER, GL_INT);
		loader.loadData(boneWeights.data(), boneWeights.size(), 4);
	}
    loader.loadData(indices.data(), indices.size(), 0, GL_ELEMENT_ARRAY_BUFFER, GL_UNSIGNED_INT, false);

    Mesh loadedMesh = loader.getMesh(vertices.size(), indices.size());
	loadedMesh.boundingBox = BoundingBox{ positionsForBoundingBox };

	loadedMesh.refCount.onRemove = [cacheName]() { GameObjectLoader::mMeshCache.erase(cacheName); };
	mMeshCache[cacheName] = loadedMesh;
	mMeshCache[cacheName].refCount.setWeak();

    go->addMesh(loadedMesh, loadedMaterial);
}

MaterialPtr GameObjectLoader::processMaterial(aiMesh* mesh, const aiScene* scene, const std::string& cacheName)
{
    BlinnPhongMaterialBuilder phongBuilder;
    aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

    aiColor3D color{0.f,0.f,0.f};
    if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, color))
        phongBuilder.setDiffuseColor(glm::vec3{color.r, color.g, color.b});

    if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_SPECULAR, color))
        phongBuilder.setSpecularColor(glm::vec3{color.r, color.g, color.b});

    //if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_TRANSPARENT, color))

    float shininess = 0.0f;
    phongBuilder.setShininess(shininess); // defaults to 0
    if (AI_SUCCESS == material->Get(AI_MATKEY_SHININESS, shininess))
        phongBuilder.setShininess(shininess);

	phongBuilder.setDiffuseMap(loadTexture(material, scene, aiTextureType_DIFFUSE, cacheName));
    phongBuilder.setSpecularMap(loadTexture(material, scene, aiTextureType_SPECULAR, cacheName));
	
	// add bump map only if available
	if (material->GetTextureCount(aiTextureType_HEIGHT) != 0)
		phongBuilder.setBumpMap(loadTexture(material, scene, aiTextureType_HEIGHT, cacheName));

	if (material->GetTextureCount(aiTextureType_DISPLACEMENT) != 0)
		phongBuilder.setParallaxMap(loadTexture(material, scene, aiTextureType_DISPLACEMENT, cacheName));

	// is this model animated?
	phongBuilder.setAnimated(mesh->mNumBones != 0);

    BlinnPhongMaterialPtr loadedMaterial = phongBuilder.build();

    int twosided = 0;
    if (AI_SUCCESS == material->Get(AI_MATKEY_TWOSIDED, twosided))
        loadedMaterial->isTwoSided = static_cast<bool>(twosided);

    // Another check to see if the material is transparent
    float opacity = 1.0f;
    if (AI_SUCCESS == material->Get(AI_MATKEY_OPACITY, opacity)) {
        loadedMaterial->opacity = opacity;
        loadedMaterial->isTwoSided = opacity < 1.0f;
    }

	// add animationController
	if (mesh->mNumBones != 0) 
		loadedMaterial->skeletalAnimationController = mSkeletalAnimationController;
	

    return loadedMaterial;
}

Texture GameObjectLoader::loadTexture(aiMaterial* material, const aiScene* scene, aiTextureType type, const std::string& meshCacheName)
{
    std::map<int, int> aiMapMode2glMapMode{
        {aiTextureMapMode_Wrap, GL_REPEAT},
        {aiTextureMapMode_Clamp, GL_CLAMP_TO_EDGE},
        {aiTextureMapMode_Mirror, GL_MIRRORED_REPEAT},
        {aiTextureMapMode_Decal, GL_REPEAT} // not supported
    };

    // Only one texture supported
    if(material->GetTextureCount(type) != 0) {
        aiString path;
        aiTextureMapMode mapModeU, mapModeV;
        material->Get(AI_MATKEY_TEXTURE(type, 0), path);
        material->Get(AI_MATKEY_MAPPINGMODE_U(type, 0), mapModeU);
        material->Get(AI_MATKEY_MAPPINGMODE_V(type, 0), mapModeV);

		GLenum mapModeS = GL_REPEAT;
		GLenum mapModeT = GL_REPEAT;
		auto mapModeSData = aiMapMode2glMapMode.find(mapModeU);
		if (mapModeSData != aiMapMode2glMapMode.end())
			mapModeS = mapModeSData->second;

		auto mapModeTData = aiMapMode2glMapMode.find(mapModeV);
		if (mapModeTData != aiMapMode2glMapMode.end())
			mapModeT = mapModeTData->second;

        const char* texturePath = path.C_Str();
        /* check whether or not this is an embedded texture. If that's the case
         * load it from memory. The name of embedded textures starts with
         * '*' followed by a number that can be used to index scene->mTextures
         * This texture is then converted to bytes read by Texture */
        if (texturePath[0] == '*') {
            aiTexture* texture = scene->mTextures[std::atoi(texturePath + 1)];
            std::uint8_t* textureData = reinterpret_cast<unsigned char*>(texture->pcData);
            if (texture->mHeight == 0)
				return Texture::loadFromMemoryCached(meshCacheName + std::string{ texturePath }, textureData, texture->mWidth, mapModeS, mapModeT);
            else
                return Texture::loadFromMemoryCached(meshCacheName + std::string{ texturePath }, textureData, texture->mWidth * texture->mHeight, mapModeS, mapModeT);

        } else {
			std::filesystem::path path{ texturePath };
			if (path.is_relative())
				path = mWorkingDir / path;

            return Texture::loadFromFile(path.string(),
                                 mapModeS,
                                 mapModeT);
        }
    }

    return Texture{};
}

void GameObjectLoader::decompose(const glm::mat4& mat, glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale)
{
	glm::vec3 skew;
	glm::vec4 perspective;
	glm::decompose(mat, outScale, outRot, outPos, skew, perspective);
}

void GameObjectLoader::findBones(const aiScene* scene)
{
	for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
		aiMesh* mesh = scene->mMeshes[i];
		for (unsigned int j = 0; j < mesh->mNumBones; j++) {
			aiBone* bone = mesh->mBones[j];
			Bone b;
			b.offset = convertMatrix(bone->mOffsetMatrix);

			// this is not very cache friendly but for now it is ok
			auto boneName = std::string{ bone->mName.C_Str() };
			auto boneIndex = mBones.size();
			mBoneName2Index[boneName] = boneIndex;
			mBones.push_back(b);

			// loads weights for the vertices influenced by this bone
			for (unsigned int k = 0; k < bone->mNumWeights; k++) {
				auto vertexIndex = bone->mWeights[k].mVertexId;
				float weight = bone->mWeights[k].mWeight;
				mVertexToInfluencingBones[std::make_pair(vertexIndex, mesh)].push_back(boneIndex);
				mVertexToInfluencingBonesWeights[std::make_pair(vertexIndex, mesh)].push_back(weight);
			}
		}
	}
}

void GameObjectLoader::buildBonesHierarchy(const aiNode* node)
{
	// build the bone hierarchy recursively
	if (isBone(node)) {
		std::uint32_t index = mBoneName2Index[node->mName.C_Str()];
		auto& bone = mBones[index];
		bone.name = node->mName.C_Str();
		decompose(convertMatrix(node->mTransformation), bone.position, bone.rotation, bone.scale);

		if (isBone(node->mParent))
			bone.parent = mBoneName2Index[node->mParent->mName.C_Str()];
		else
			bone.parent = -1;
		for (unsigned int i = 0; i < node->mNumChildren; ++i) {
			aiNode* child = node->mChildren[i];
			buildBonesHierarchy(child);
		}
	}
	else {
		for (unsigned int i = 0; i < node->mNumChildren; ++i)
			buildBonesHierarchy(node->mChildren[i]);
	}
}

bool GameObjectLoader::isBone(const aiNode* node)
{
	if (node == nullptr) return false;
	return mBoneName2Index.find(node->mName.C_Str()) != mBoneName2Index.end();
}

void GameObjectLoader::computeTangentsAndBitangentSign(const aiMesh* mesh, std::vector<float>& tangents, std::vector<std::int32_t> bitangentSign) const
{
	if (!mesh->HasTextureCoords(0)) {
		tangents.resize(3 * mesh->mNumVertices, 0.0f);
		bitangentSign.resize(mesh->mNumVertices, 0);
		std::cerr << "cannot calculate tangent space: texture coordinates are missing\n";
		return;
	}

	std::vector<glm::vec3> tangentSum;
	tangentSum.resize(mesh->mNumVertices, glm::vec3{ 0.0f });

	for (std::uint32_t i = 0; i < mesh->mNumFaces; ++i) {
		aiFace& face = mesh->mFaces[i];
		aiVector3D q = mesh->mVertices[face.mIndices[1]] - mesh->mVertices[face.mIndices[0]];
		aiVector3D p = mesh->mVertices[face.mIndices[2]] - mesh->mVertices[face.mIndices[0]];
		aiVector3D uv1 = mesh->mTextureCoords[0][face.mIndices[1]] - mesh->mTextureCoords[0][face.mIndices[0]];
		aiVector3D uv2 = mesh->mTextureCoords[0][face.mIndices[2]] - mesh->mTextureCoords[0][face.mIndices[0]];

		glm::mat3x2 pq;
		pq[0] = glm::vec2{ q.x, p.x };
		pq[1] = glm::vec2{ q.y, p.y };
		pq[2] = glm::vec2{ q.z, p.z };

		glm::mat2x2 uv;
		uv[0] = glm::vec2{ uv1.x, uv2.x };
		uv[1] = glm::vec2{ uv1.y, uv2.y };

		glm::mat3x2 tangentBitangent = glm::inverse(uv) * pq; // TODO: improve, do not use inverse

		glm::vec3 tangent = glm::row(tangentBitangent, 0);

		for (std::uint32_t j = 0; j < face.mNumIndices; j++)
			tangentSum[face.mIndices[j]] += tangent;
	}

	for (std::uint32_t j = 0; j < tangentSum.size(); j++)
		tangentSum[j] = glm::normalize(tangentSum[j]);

	std::for_each(tangentSum.begin(), tangentSum.end(), [&tangents](auto& t) {
		tangents.insert(tangents.end(), { t.x, t.y, t.z });
	}); 
}

std::vector<std::uint32_t>& GameObjectLoader::getInfluencingBones(std::uint32_t vertexIndex, aiMesh* mesh)
{
	auto& bones = mVertexToInfluencingBones[std::make_pair(vertexIndex, mesh)];
	if (bones.size() > 4) std::cerr << "more than 4 bones per vertex, not supported\n";
	bones.resize(MAX_BONES_PER_VERTEX, 0);

	return bones;
}

std::vector<float>& GameObjectLoader::getInfluencingBonesWeights(std::uint32_t vertexIndex, aiMesh* mesh)
{
	auto& weights = mVertexToInfluencingBonesWeights[std::make_pair(vertexIndex, mesh)];
	weights.resize(MAX_BONES_PER_VERTEX, 0.0f);

	return weights;
}

GameObjectEH GameObjectLoader::fromFile(const std::string& path)
{
	mFilePath = path;
	mWorkingDir = (std::filesystem::path{ path }).remove_filename();

	Assimp::Importer importer;
	importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
	const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Error loading mesh " << importer.GetErrorString() << "\n";
        return GameObjectEH{};
    }

	findBones(scene);
	buildBonesHierarchy(scene->mRootNode);

	// if bones where found this GameObject supports skeletal animation
	if (mBones.size() != 0) {
		mSkeletalAnimationController = std::make_shared<SkeletralAnimationControllerComponent>(GameObjectEH{}, mBones, mBoneName2Index);
		SkeletalAnimationLoader loader;
		auto animation = loader.fromAssimpScene(scene, mBoneName2Index);
		mSkeletalAnimationController->addAnimation("default", animation);
	}

    auto root = processNode(scene->mRootNode, scene);

	if (mSkeletalAnimationController) {
		mSkeletalAnimationController->gameObject = root;
		root->addComponent(mSkeletalAnimationController);
	}

	return root;
}
