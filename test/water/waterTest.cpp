#include "Engine.h"
#include "rendering/mesh/MeshLoader.h"
#include "rendering/materials/BlinnPhongMaterial.h"
#include "rendering/materials/PropMaterial.h"
#include "rendering/materials/SkyboxMaterial.h"

#include "cameras/FreeCameraComponent.h"
#include "rendering/light/Light.h"
#include "rendering/mesh/MeshCreator.h"
#include "gameobject/GameObjectLoader.h"
#include "terrain/TerrainGenerator.h"
#include "terrain/HeightMapTerrainHeightProvider.h"
#include "resourceManagment/RefCount.h"
#include "rendering/effects/FXAA.h"
#include "rendering/materials/MultiTextureBlinnPhongMaterial.h"
#include "rendering/shadow/ShadowOnVisibleSceneComponent.h"
#include "rendering/materials/WaterMaterial.h"
#include "rendering/light/PointLight.h"
#include "../test/runTest.h"
#include "rendering/effects/GammaCorrection.h"

#include <iostream>
#include <stdlib.h>

#ifdef waterTest
int main(int argc, char* argv[]) {
    Engine::init();

    Engine::renderSys.createWindow(1280, 720);

	auto gammaPost = std::make_shared<GammaCorrection>();
	gammaPost->setGamma(1.8f);
	gammaPost->setExposure(1.0f);
	Engine::renderSys.effectManager.addEffect(gammaPost);

    auto camera = Engine::gameObjectManager.createGameObject();
    camera->name = "camera";
    camera->transform.moveBy(glm::vec3{0.0f, 0.0f, 30.0f});
    Engine::renderSys.camera = camera;

    auto cam = std::make_shared<FreeCameraComponent>(camera);
    camera->addComponent(cam);
    camera->transform.setRotation(glm::quat{glm::vec3{0, glm::radians(180.0f), 0}});

	auto water = Engine::gameObjectManager.createGameObject(MeshCreator::plane(),
		std::make_shared<WaterMaterial>(-5.0f, Texture::loadFromFile("test_data/water/dudv.png"),
			Texture::loadFromFile("test_data/water/normal.png")));

	water->transform.moveBy(glm::vec3{ 35, -5, -5 });
	water->transform.scaleBy(glm::vec3{ 90.0f });
	water->transform.rotateBy(glm::angleAxis(glm::radians(-90.0f), water->transform.right()));

    auto skyTexture = Texture::loadCubemapFromFile({
                    {"front", "test_data/skybox/front.tga"},
                    {"back", "test_data/skybox/back.tga"},
                    {"top", "test_data/skybox/top.tga"},
                    {"bottom", "test_data/skybox/bottom.tga"},
                    {"left", "test_data/skybox/left.tga"},
                    {"right", "test_data/skybox/right.tga"},
                });
    auto skyboxMaterial = std::make_shared<SkyboxMaterial>(skyTexture);
    auto box = Engine::gameObjectManager.createGameObject(MeshCreator::cube(), skyboxMaterial);

	auto multiTextured = std::make_shared<MultiTextureBlinnPhongMaterial>();
	multiTextured->baseTexture = Texture::loadFromFile("test_data/multiple_textures_blinn/grass.jpg");
	multiTextured->baseTextureBump = Texture::loadFromFile("test_data/multiple_textures_blinn/grass_bump.png");
	multiTextured->greenTexture = Texture::loadFromFile("test_data/multiple_textures_blinn/path.jpg");
	multiTextured->greenTextureSpecular = Texture::loadFromFile("test_data/multiple_textures_blinn/path.jpg");
	multiTextured->greenTextureBump = Texture::loadFromFile("test_data/multiple_textures_blinn/path_bump.jpg");
	multiTextured->redTexture = Texture::loadFromFile("test_data/multiple_textures_blinn/ground.jpg");
	multiTextured->redTextureSpecular = Texture::loadFromFile("test_data/multiple_textures_blinn/ground.jpg");
	multiTextured->redTextureBump = Texture::loadFromFile("test_data/multiple_textures_blinn/ground_bump.jpg");
	multiTextured->blendTexture = Texture::loadFromFile("test_data/multiple_textures_blinn/blend.png");
	multiTextured->greenShininess = 8.0f;

	HeightMapTerrainHeightProvider hProvider{ "test_data/multiple_textures_blinn/height.jpg", -10, 10 };
	TerrainGenerator generator{ 500, 500, 500, 500 };
	generator.includeTangentSpace(true);
	auto terrain = Engine::gameObjectManager.createGameObject(generator.createTerrain(hProvider), multiTextured);

	for (int i = 0; i < 50; i++) {
		auto tree = GameObjectLoader().fromFile("test_data/multiple_textures_blinn/trees.fbx");
		int x = (rand() % 500) - 250;
		int z = (rand() % 500) - 250;
		float y = hProvider.get((x + 250) / 500.0f, (z + 250) / 500.0f);
		tree->transform.setPosition(glm::vec3{ x, y, z });
	}

	for (int i = 0; i < 50; i++) {
		auto tree = GameObjectLoader().fromFile("test_data/multiple_textures_blinn/trees2.fbx");
		int x = (rand() % 500) - 250;
		int z = (rand() % 500) - 250;
		float y = hProvider.get((x + 250) / 500.0f, (z + 250) / 500.0f);
		tree->transform.setPosition(glm::vec3{ x, y, z });
	}

	for (int i = 0; i < 50; i++) {
		auto bush = GameObjectLoader().fromFile("test_data/multiple_textures_blinn/bush.fbx");
		int x = (rand() % 500) - 250;
		int z = (rand() % 500) - 250;
		float y = hProvider.get((x + 250) / 500.0f, (z + 250) / 500.0f);
		bush->transform.setPosition(glm::vec3{ x, y -.5f, z });
		bush->transform.scaleBy(glm::vec3{ 0.3f });
	}


	Engine::renderSys.effectManager.enableEffects();
	Engine::renderSys.effectManager.addEffect(std::make_shared<FXAA>());
	Engine::renderSys.shadowMappingSettings.setShadowStrength(0.3f);
	Engine::renderSys.shadowMappingSettings.useFastShader = false;

	auto light3 = Engine::gameObjectManager.createGameObject(MeshCreator::cube(), std::make_shared<PropMaterial>());
	light3->name = "light3";
	light3->addComponent(std::make_shared<DirectionalLight>(light3));
	light3->transform.setPosition(glm::vec3{ 0.0f, 0.0f, 15.0f });
	Engine::renderSys.addLight(light3);
	light3->getComponent<Light>()->diffuseColor = glm::vec3{ 230, 230, 230 } / 255.0f;
	light3->getComponent<Light>()->setCastShadowMode(Light::ShadowCasterMode::DYNAMIC);
	light3->getComponent<Light>()->specularColor = glm::vec3{ 230, 230, 230 } / 255.0f;
	light3->getComponent<Light>()->innerAngle = glm::radians(25.0f);
	light3->getComponent<Light>()->outerAngle = glm::radians(28.0f);
	light3->addComponent(std::make_shared<ShadowOnVisibleSceneComponent>(light3));
	light3->transform.scaleBy(glm::vec3{ 0.2f, 0.2f, 0.2f });

    auto gizmo = MeshCreator::axisGizmo();
    gizmo->transform.setParent(light3);
	gizmo->transform.setLocalPosition(glm::vec3{ 0.0f });
    light3->transform.setLocalRotation(glm::quat{glm::vec3{glm::radians(65.0f), 0.0f, 0.0f}});

    Engine::start();

    return 0;
}
#endif // LOAD_HIERARCHIES