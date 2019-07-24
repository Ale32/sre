#include "Engine.h"
#include "MeshLoader.h"
#include "BlinnPhongMaterial.h"
#include "PropMaterial.h"
#include "SkyboxMaterial.h"

#include "FreeCameraComponent.h"
#include "Light.h"
#include "MeshCreator.h"
#include "GameObjectLoader.h"
#include "TerrainGenerator.h"
#include "HeightMapTerrainHeightProvider.h"

#include <runTest.h>

#include "RefCount.h"
#include "GammaCorrection.h"

#include <iostream>
#include <random>

#ifdef textureCache

struct Remover : public Component, public EventListener {
	CrumbPtr crumb;
	SDL_Keycode killKey;

	Remover(const GameObjectEH& go, SDL_Keycode kk) : Component(go), killKey{ kk } {
		crumb = Engine::eventManager.addListenerFor(SDL_KEYDOWN, this, true);
	}

	void onEvent(SDL_Event e) override {
		if (e.key.keysym.sym == killKey)
			Engine::gameObjectManager.remove(gameObject);
	}
};

int main(int argc, char* argv[]) {
    Engine::init();

    Engine::renderSys.createWindow(1280, 720);

    auto camera = Engine::gameObjectManager.createGameObject();
    camera->name = "camera";
    camera->transform.moveBy(glm::vec3{0.0f, 0.0f, 30.0f});
    Engine::renderSys.camera = camera;

    auto cam = std::make_shared<FreeCameraComponent>(camera);
    camera->addComponent(cam);
    camera->transform.setRotation(glm::quat{glm::vec3{0, glm::radians(180.0f), 0}});


	auto tb1 = GameObjectLoader().fromFile("test_data/texture_cache/table.obj");
	tb1->addComponent(std::make_shared<Remover>(tb1, SDLK_1));

	auto tb2 = GameObjectLoader().fromFile("test_data/texture_cache/table.obj");
	tb2->transform.setPosition(glm::vec3{ 0, 0, -10 });
	tb2->addComponent(std::make_shared<Remover>(tb2, SDLK_2));

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

	Engine::renderSys.effectManager.enableEffects();
	Engine::renderSys.effectManager.addEffect(std::make_shared<GammaCorrection>());

    auto light = Engine::gameObjectManager.createGameObject(MeshCreator::cube(), std::make_shared<PropMaterial>());
    light->name = "light";
    light->addComponent(std::make_shared<Light>(light));
    light->transform.setPosition(glm::vec3{0.0f, 3.0f, 0.0f});
    Engine::renderSys.addLight(light);
    light->getComponent<Light>()->diffuseColor = glm::vec3{1.0f, 1.0f, 1.0f};
    light->getComponent<Light>()->specularColor = glm::vec3{1.0f, 1.0f, 1.0f};
    light->transform.scaleBy(glm::vec3{0.2f, 0.2f, 0.2f});

    auto light2 = Engine::gameObjectManager.createGameObject(MeshCreator::cube(), std::make_shared<PropMaterial>());
    light2->name = "light2";
    light2->addComponent(std::make_shared<Light>(light2));
    Engine::renderSys.addLight(light2);
    light2->getComponent<Light>()->diffuseColor = glm::vec3{1.0f, 1.0f, 1.0f};
    light2->getComponent<Light>()->specularColor = glm::vec3{1.0f, 1.0f, 1.0f};

    light2->transform.scaleBy(glm::vec3{0.2f, 0.2f, 0.2f});

    auto light3 = Engine::gameObjectManager.createGameObject(MeshCreator::cube(), std::make_shared<PropMaterial>());
    light3->name = "light3";
    light3->addComponent(std::make_shared<Light>(light3, Light::Type::DIRECTIONAL));
    light3->transform.setPosition(glm::vec3{0.0f, 0.0f, 15.0f});
    Engine::renderSys.addLight(light3);
    light3->getComponent<Light>()->diffuseColor = glm::vec3{1.0f, 1.0f, 1.0f};
    light3->getComponent<Light>()->specularColor = glm::vec3{1.0f, 1.0f, 1.0f};
    light3->getComponent<Light>()->innerAngle = glm::radians(25.0f);
    light3->getComponent<Light>()->outerAngle = glm::radians(28.0f);
    light3->transform.scaleBy(glm::vec3{0.2f, 0.2f, 0.2f});


    auto gizmo = MeshCreator::axisGizmo();
    gizmo->transform.setParent(light3);
    gizmo->transform.setPosition(light3->transform.getPosition());
    light3->transform.setRotation(glm::quat{glm::vec3{glm::radians(90.0f), 0.0f, 0.0f}});

    Engine::start();

    return 0;
}
#endif // LOAD_HIERARCHIES
