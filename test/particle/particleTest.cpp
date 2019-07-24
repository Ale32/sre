#include "Engine.h"
#include "MeshLoader.h"
#include "BlinnPhongMaterial.h"
#include "PropMaterial.h"

#include "FreeCameraComponent.h"
#include "DirectionalLight.h"
#include "MeshCreator.h"
#include "GameObjectLoader.h"
#include "FXAA.h"
#include "Bloom.h"
#include "runTest.h"
#include "ParticleEmitter.h"
#include "SkyboxMaterial.h"

#include <iostream>


void addParticles(const GameObjectEH eh) {
	EmitterSettings settings;
	settings.velocityMin = glm::vec3{ 0.0f, -1.0f, 0.0f };
	settings.velocityMax = glm::vec3{ 0.0f, 0.0f, 0.0f };
	settings.positionOffsetMin = -glm::vec3{ 1.0f, 0.0f, 1.0f };
	settings.positionOffsetMax = glm::vec3{ 1.0f, 0.5f, 1.0f };
	settings.minDuration = 500.0f;
	settings.maxDuration = 1500.0f;
	settings.finalScaleMin = 0.0f;
	settings.finalScaleMax = 0.1f;
	settings.initialRotationMin = glm::radians(-30.0f);
	settings.initialRotationMax = glm::radians(30.0f);
	settings.finalRotationMin = glm::radians(-30.0f);
	settings.finalRotationMax = glm::radians(30.0f);
	settings.initialGravityScaleMin = -0.3f;
	settings.initialGravityScaleMax = -1.0f;
	settings.finalGravityScaleMin = -1.3f;
	settings.finalGravityScaleMax = -1.9f;
	settings.dfactor = GL_ONE;

	eh->addComponent(std::make_shared<ParticleEmitter>(eh, 500));

	eh->getComponent<ParticleEmitter>()->setTextureAtlas(Texture::loadFromFile("test_data/particle/fire.png"), 25, 5, 5);
	eh->getComponent<ParticleEmitter>()->start(300.0f);
	eh->getComponent<ParticleEmitter>()->settings = settings;
}

#ifdef particleTest
int main(int argc, char* argv[]) {
    Engine::init();

    Engine::renderSys.createWindow(1280, 720);
	Engine::renderSys.effectManager.addEffect(std::make_shared<FXAA>());
 	Engine::renderSys.effectManager.addEffect(std::make_shared<Bloom>());


	Engine::renderSys.effectManager.enableEffects();
	Engine::renderSys.shadowMappingSettings.useFastShader = true;
	Engine::renderSys.shadowMappingSettings.width = 500;
	Engine::renderSys.shadowMappingSettings.height = 500;
	Engine::renderSys.shadowMappingSettings.depth = 500;

    auto camera = Engine::gameObjectManager.createGameObject();
    camera->name = "camera";
    camera->transform.moveBy(glm::vec3{0.0f, 0.0f, 30.0f});
    Engine::renderSys.camera = camera;

    auto cam = std::make_shared<FreeCameraComponent>(camera);
    camera->addComponent(cam);
    camera->transform.setRotation(glm::quat{glm::vec3{0, glm::radians(180.0f), 0}});

	auto sponza = GameObjectLoader().fromFile("test_data/bloom/sponza.fbx");
	sponza->transform.setScale(glm::vec3{ 0.1f });

	for (const auto& eh : sponza->transform.findAll("firePos"))
		addParticles(eh);

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

	auto human = GameObjectLoader().fromFile("C:/Users/damia/Desktop/warrior_idle.dae");
	auto animationController = human->getComponent<SkeletralAnimationControllerComponent>();
	animationController->playAnimation("default");
	animationController->getAnimation("default").loopDirection = SkeletalAnimation::LoopDirection::REPEAT;

	human->transform.scaleBy(glm::vec3{ 10.0f });

	auto light3 = Engine::gameObjectManager.createGameObject(MeshCreator::cube(), std::make_shared<PropMaterial>());
	light3->name = "light3";
	light3->addComponent(std::make_shared<DirectionalLight>(light3));
	light3->transform.setPosition(glm::vec3{ 50.0f, 150.0f, -30.0f });
	Engine::renderSys.addLight(light3);
	light3->getComponent<Light>()->setCastShadowMode(Light::ShadowCasterMode::STATIC);
	light3->getComponent<Light>()->diffuseColor = glm::vec3{ .9f, .9f, .9f };
	light3->getComponent<Light>()->specularColor = glm::vec3{ .9f, .9f, .9f };
	light3->transform.scaleBy(glm::vec3{ 0.2f, 0.2f, 0.2f });
	light3->transform.rotateBy(glm::angleAxis(glm::radians(55.0f), light3->transform.right()));
	light3->transform.rotateBy(glm::angleAxis(glm::radians(-15.0f), light3->transform.up()));

    auto light = Engine::gameObjectManager.createGameObject(MeshCreator::cube(), std::make_shared<PropMaterial>());
	light->name = "light";
    light->addComponent(std::make_shared<PointLight>(light));
    light->transform.setPosition(glm::vec3{0.0f, 3.0f, 0.0f});
    Engine::renderSys.addLight(light);
    light->getComponent<Light>()->diffuseColor = glm::vec3{ 1.0f, 1.0f, 1.0f };
    light->getComponent<Light>()->specularColor = glm::vec3{ 1.0f, 1.0f, 1.0f };
    light->transform.scaleBy(glm::vec3{0.2f, 0.2f, 0.2f});


    auto gizmo = MeshCreator::axisGizmo();
    gizmo->transform.setParent(light3);
	gizmo->transform.setLocalRotation(glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f });
    gizmo->transform.setPosition(light3->transform.getPosition());

    Engine::start();

    return 0;
}
#endif // LOAD_HIERARCHIES
