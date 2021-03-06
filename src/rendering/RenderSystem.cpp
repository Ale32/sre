#include "rendering/RenderSystem.h"
#include "rendering/materials/Shader.h"
#include "rendering/light/Light.h"
#include "Engine.h"
#include "rendering/mesh/MeshLoader.h"
#include "rendering/materials/ShadowMapMaterial.h"
#include "rendering/mesh/MeshCreator.h"
#include "rendering/light/DirectionalLight.h"
#include "debugUtils/debug.h"
#include "cameras/CameraComponent.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <exception>
#include <algorithm>
#include <iostream>
#include <memory>
#include <map>

#include <nvToolsExt.h>

// is openGL debug enabled
bool DEBUG = false;

RenderSystem::RenderSystem()
{

}

void RenderSystem::createWindow(std::uint32_t width, std::uint32_t height)
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		std::cout << "Cannot init SDL " << SDL_GetError() << "\n";
		std::terminate();
	}

	SDL_GL_LoadLibrary(nullptr); // use default OpenGL
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

	if (DEBUG)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	//	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
	//	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

	mWindow = SDL_CreateWindow("sre", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
	if (mWindow == nullptr) {
		std::cout << "Cannot create window " << SDL_GetError() << "\n";
		std::terminate();
	}

	SDL_GL_CreateContext(mWindow);

	// Use v-sync
	//SDL_GL_SetSwapInterval(1);

	if (!gladLoadGLLoader(SDL_GL_GetProcAddress)) {
		std::cout << "Failed to initialize GLAD\n";
		std::terminate();
	}

	initGL(width, height);

	if (DEBUG) {
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(glDebugOutput, nullptr);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
	}

	initDeferredRendering();
	effectTarget.create(width, height);
	effectManager.init();
	fogSettings.init();
	shadowMappingSettings.init();

	Engine::particleRenderer.init();
    Engine::uiRenderer.init();

	// simple shader for shadow mapping
	mShadowMapMaterial = std::make_shared<ShadowMapMaterial>();
	mPointShadowMaterial = std::make_shared<PointShadowMaterial>();

	// create a default camera
	GameObjectEH defaultCamera = Engine::gameObjectManager.createGameObject();
	std::shared_ptr<CameraComponent> cameraComponent = std::make_shared<CameraComponent>(defaultCamera, 0.785f, 0.1f, 1000.0f);
	defaultCamera->addComponent(cameraComponent);

	defaultCamera->name = "defaultCamera";
	setCamera(defaultCamera);
}

void RenderSystem::initGL(std::uint32_t width, std::uint32_t height)
{
	mInvertView = glm::mat4{
			glm::vec4{ -1, 0, 0, 0 },
			glm::vec4{ 0, 1, 0, 0 },
			glm::vec4{ 0, 0, -1, 0 },
			glm::vec4{ 0, 0, 0, 1 } };

	/* Uniform buffer object set up for common matrices */
	glGenBuffers(1, &mUboCommonMat);
	glBindBuffer(GL_UNIFORM_BUFFER, mUboCommonMat);
	// 3 matrices: view, projection, projection * view and a vec4 for the clipping plane
	glBufferData(GL_UNIFORM_BUFFER, 3 * sizeof(glm::mat4) + sizeof(glm::vec4), nullptr, GL_STREAM_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, COMMON_MAT_UNIFORM_BLOCK_INDEX, mUboCommonMat);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);


	/* Uniform buffer object set up for lights */
	glGenBuffers(1, &mUboLights);
	glBindBuffer(GL_UNIFORM_BUFFER, mUboLights);
	// 16 numLights, 208 size of a light array element
	glBufferData(GL_UNIFORM_BUFFER, 16 + 208 * MAX_LIGHT_NUMBER, nullptr, GL_STREAM_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, LIGHT_UNIFORM_BLOCK_INDEX, mUboLights);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	/* Uniform buffer object set up for camera */
	glGenBuffers(1, &mUboCamera);
	glBindBuffer(GL_UNIFORM_BUFFER, mUboCamera);

	// size for camera position and direction
	glBufferData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::vec4), nullptr, GL_STREAM_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, CAMERA_UNIFORM_BLOCK_INDEX, mUboCamera);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	/* General OpenGL settings */
	glViewport(0, 0, width, height);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_STENCIL_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
}

void RenderSystem::initDeferredRendering()
{
	deferredRenderingFBO.init(getScreenWidth(), getScreenHeight());

	mDirectionalLightDeferred.init({ "shaders/deferred_rendering/directionalLightVS.glsl" },
		{ "shaders/Light.glsl", "shaders/ShadowMappingCalculation.glsl", "shaders/deferred_rendering/directionalLightFS.glsl" },
		{ "DiffuseData", "SpecularData", "PositionData", "NormalData", "shadowMap" },
		{
			{ "Lights", RenderSystem::LIGHT_UNIFORM_BLOCK_INDEX },
			{ "Camera", RenderSystem::CAMERA_UNIFORM_BLOCK_INDEX },
			{ "ShadowMapParams", RenderSystem::SHADOWMAP_UNIFORM_BLOCK_INDEX } 
		});

	mDirectionalLightDeferredPBR.init({ "shaders/pbr/directionalLightVS.glsl" },
		{ "shaders/Light.glsl", "shaders/ShadowMappingCalculation.glsl", "shaders/pbr/PBRLightCalculation.glsl", "shaders/pbr/directionalLightFS.glsl" },
		{ "DiffuseData", "PBRData", "PositionData", "NormalData", "shadowMap" },
		{
			{ "Lights", RenderSystem::LIGHT_UNIFORM_BLOCK_INDEX },
			{ "Camera", RenderSystem::CAMERA_UNIFORM_BLOCK_INDEX },
			{ "ShadowMapParams", RenderSystem::SHADOWMAP_UNIFORM_BLOCK_INDEX }
		});

	MeshLoader loader;
	float verts[]{ -1, -1, -1, 1, 1, 1, 1, -1 };
	float texCoords[]{ 0, 0, 0, 1, 1, 1, 1, 0 };
	std::uint32_t indices[]{ 0, 2, 1, 0, 3, 2 };
	loader.loadData(verts, 8, 2);
	loader.loadData(texCoords, 8, 2);
	loader.loadData(indices, 6, 0, GL_ELEMENT_ARRAY_BUFFER, GL_UNSIGNED_INT, false);
	mScreenMesh = loader.getMesh(0, 6);

	mPointLightDeferred.init({ "shaders/deferred_rendering/pointLightVS.glsl" },
		{ "shaders/Light.glsl", "shaders/PointShadowCalculation.glsl", "shaders/deferred_rendering/pointLightFS.glsl" },
		{ "DiffuseData", "SpecularData", "PositionData", "NormalData", "shadowCube" },
		{
			{ "Lights", RenderSystem::LIGHT_UNIFORM_BLOCK_INDEX },
			{ "Camera", RenderSystem::CAMERA_UNIFORM_BLOCK_INDEX },
		});

	mPointLightDeferredPBR.init({ "shaders/pbr/pointLightVS.glsl" },
		{ "shaders/Light.glsl", "shaders/PointShadowCalculation.glsl", "shaders/pbr/PBRLightCalculation.glsl", "shaders/pbr/pointLightFS.glsl" },
		{ "DiffuseData", "PBRData", "PositionData", "NormalData", "shadowCube" }, 
		{
			{ "Lights", RenderSystem::LIGHT_UNIFORM_BLOCK_INDEX },
			{ "Camera", RenderSystem::CAMERA_UNIFORM_BLOCK_INDEX },
		});

	mPointLightSphere = MeshCreator::sphere(1.0f, 10, 10, false, false);

	mPointLightDeferredStencil = Shader::loadFromFile({ "shaders/Light.glsl", "shaders/deferred_rendering/pointLightSphereStencilPassVS.glsl" },
		std::vector<std::string>{},
		{ "shaders/deferred_rendering/pointLightSphereStencilPassFS.glsl" });
	mPointLightDeferredStencil.use();
	mPointLightDeferredStencil.bindUniformBlock("Lights", RenderSystem::LIGHT_UNIFORM_BLOCK_INDEX);
	mPointLightStencilLightIndexLocation = mPointLightDeferredStencil.getLocationOf("lightIndex");
	mPointLightStencilScaleLocation = mPointLightDeferredStencil.getLocationOf("scale");
}

void RenderSystem::updateLights()
{
	std::size_t numLight = std::min((std::size_t)MAX_LIGHT_NUMBER, mLights.size());
	glBindBuffer(GL_UNIFORM_BUFFER, mUboLights);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(std::size_t), (void*)&numLight);
	for (std::size_t i = 0; i < numLight; i++) {
		int base = 16 + 208 * i;
		LightPtr lightComponent = mLights[i]->getComponent<Light>();
		if (lightComponent == nullptr) continue;

		std::uint32_t lightType = static_cast<std::uint32_t>(lightComponent->mType);
		bool castShadow = lightComponent->getShadowCasterMode() != Light::ShadowCasterMode::NO_SHADOWS;

		glm::vec3 attenuation = glm::vec3{
			lightComponent->attenuationConstant,
			lightComponent->attenuationLinear,
			lightComponent->attenuationQuadratic
		};

		glm::vec2 spotAngles{
			glm::cos(lightComponent->innerAngle),
			glm::cos(lightComponent->outerAngle)
		};

		Transform& transform = mLights[i]->transform;

		// type
		glBufferSubData(GL_UNIFORM_BUFFER, base, sizeof(std::uint32_t), (void *)(&lightType));

		// position
		glBufferSubData(GL_UNIFORM_BUFFER, base + 16, sizeof(glm::vec3), glm::value_ptr(transform.getPosition()));

		// direction
		glBufferSubData(GL_UNIFORM_BUFFER, base + 32, sizeof(glm::vec3), glm::value_ptr(transform.forward()));

		// ambient
		glBufferSubData(GL_UNIFORM_BUFFER, base + 48, sizeof(glm::vec3), glm::value_ptr(lightComponent->ambientColor));

		// diffuse
		glBufferSubData(GL_UNIFORM_BUFFER, base + 64, sizeof(glm::vec3), glm::value_ptr(lightComponent->diffuseColor));

		// specular
		glBufferSubData(GL_UNIFORM_BUFFER, base + 80, sizeof(glm::vec3), glm::value_ptr(lightComponent->specularColor));

		// attenuation
		glBufferSubData(GL_UNIFORM_BUFFER, base + 96, sizeof(glm::vec3), glm::value_ptr(attenuation));

		// spot light angles
		glBufferSubData(GL_UNIFORM_BUFFER, base + 112, sizeof(glm::vec2), glm::value_ptr(spotAngles));

		// to light space matrix
		if (castShadow) {
			// 			glm::mat4 lightProjection = glm::mat4{ 0.0f };
			// 			lightProjection[0][0] = 2.0f / shadowMappingSettings.width;
			// 			lightProjection[1][1] = 2.0f / shadowMappingSettings.height;
			// 			lightProjection[2][2] = -2.0f / shadowMappingSettings.depth;
			// 			lightProjection[3][3] = 1.0f;
			glm::mat4 lightProjection = glm::ortho(-shadowMappingSettings.width / 2, shadowMappingSettings.width / 2,
				-shadowMappingSettings.height / 2, shadowMappingSettings.height / 2,
				0.1f, shadowMappingSettings.depth);

			glm::mat4 toLightSpace = lightProjection * getViewMatrix(transform);

			glBufferSubData(GL_UNIFORM_BUFFER, base + 128, sizeof(glm::mat4), glm::value_ptr(toLightSpace));
		}

		// cast shadow
		glBufferSubData(GL_UNIFORM_BUFFER, base + 192, sizeof(bool), (void *)(&castShadow));
	}
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}


void RenderSystem::updateCamera()
{
	glm::vec3 cameraPosition{ 0.0f };
	glm::vec3 cameraDirection{ 0.0f, 0.0f, 1.0f };
	if (mCamera) {
		cameraPosition = mCamera->transform.getPosition();
		cameraDirection = mCamera->transform.forward();
	}

	glBindBuffer(GL_UNIFORM_BUFFER, mUboCamera);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::vec3), glm::value_ptr(cameraPosition));
	glBufferSubData(GL_UNIFORM_BUFFER, 16, sizeof(glm::vec3), glm::value_ptr(cameraDirection));
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void RenderSystem::updateMatrices(const glm::mat4* projection, const glm::mat4* view)
{
	glm::mat4 projectionView = (*projection) * (*view);
	glBindBuffer(GL_UNIFORM_BUFFER, mUboCommonMat);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(*projection));
	glBufferSubData(GL_UNIFORM_BUFFER, 1 * sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(*view));
	glBufferSubData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(projectionView));

	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

glm::mat4 RenderSystem::getViewMatrix(const Transform& transform)
{
	glm::mat4 view = glm::mat4{ 1.0f };
	view = glm::translate(glm::mat4{ 1.0f }, -transform.getPosition());
	glm::mat4 inverseRotation = glm::transpose(glm::toMat4(transform.getRotation()));
	view = inverseRotation * view;

	/* Inverts the view so that we watch in the direction of camera->transform.forward() */
	return mInvertView * view;
}

const glm::mat4 RenderSystem::getProjectionMatrix() const
{
	return mProjection;
}

void RenderSystem::prepareRendering(const RenderTarget* target)
{
	if (shadowMappingSettings.isShadowRenderingEnabled())
		renderShadows();

	// view port might be changed during shadow rendering
	glViewport(0, 0, target->getWidth(), target->getHeight());
	glEnable(GL_DEPTH_TEST);

	// clear all the buffers
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// do not clear stencil buffer, we are just cleaning the screen now
	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Camera calculations */
	glm::mat4 view = glm::mat4{ 1.0f };
	if (mCamera)
		view = getViewMatrix(mCamera->transform);

	/* Sets the camera matrix to a UBO so that it is shared */
	updateMatrices(&mProjection, &view);

	updateLights();
	updateCamera();
}

void RenderSystem::prepareDeferredRendering()
{
	// bind the deferred rendering frame buffer
	glBindFramebuffer(GL_FRAMEBUFFER, deferredRenderingFBO.getFBO());

	// set up the stencil test so that only the parts affected by deferred rendering
	// are actually lit in the deferred rendering directional light pass pass
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS, DEFERRED_STENCIL_MARK, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	glStencilMask(0xFF);

	// clean the buffers of the deferredRenderingFBO
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	// blending is disabled
	glDisable(GL_BLEND);
}

void RenderSystem::preparePBRRendering()
{
	// sets the actual mark on the stencil buffer
	glStencilFunc(GL_ALWAYS, PBR_STENCIL_MARK, 0xFF);
}

void RenderSystem::renderScene(const RenderTarget* target, RenderPhase phase)
{
	//nvtxRangePushA("Frame");

	auto targetToUse = target;
	if (target == nullptr) // target null means render to screen
		targetToUse = &effectTarget;

	//nvtxRangePushA("Prepare deferred");
	prepareRendering(targetToUse);

	prepareDeferredRendering();
	//nvtxRangePop();

	//nvtxRangePushA("Render deferred");
	render(RenderPhase::DEFERRED_RENDERING | phase);
	//nvtxRangePop();

	preparePBRRendering();
	render(RenderPhase::PBR | phase);

	// no need to render lights and stuff if render target is not valid
	if (!targetToUse->isValid()) return;

	//nvtxRangePushA("finalize deferred");
	finalizeDeferredRendering(targetToUse);
	//nvtxRangePop();

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//nvtxRangePushA("Render forward");
	render(RenderPhase::FORWARD_RENDERING | phase);
	//nvtxRangePop();

	// render particles
	Engine::particleRenderer.render();

	// render to screen only if no target specified
	if (target == nullptr) {
		//nvtxRangePushA("finalize rendering");
		finalizeRendering();
        
        Engine::uiRenderer.render();

        SDL_GL_SwapWindow(mWindow);

		//nvtxRangePop();
	}

	//nvtxRangePop();
}

void RenderSystem::render(int phase)
{
	mRenderPhase = phase;
	Engine::gameObjectRenderer.render();
}

void RenderSystem::finalizeDeferredRendering(const RenderTarget* target)
{
	/* Depth and stencil information is needed in the forward rendering pass (see renderScene).
	 * Therefore, we need to copy the depth and stencil information created during the deferred
	 * shader pass into the currently bound fbo. */
	//glBindFramebuffer(GL_READ_FRAMEBUFFER, deferredRenderingFBO.getFBO());
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->getFbo());
	glBlitFramebuffer(
		0, 0, deferredRenderingFBO.getWidth(), deferredRenderingFBO.getHeight(),
		0, 0, deferredRenderingFBO.getWidth(), deferredRenderingFBO.getHeight(),
		GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST
	);
	//glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, target->getFbo());

	// clear the color buffer of the currently bound fbo (can be either effects fbo or default (0) fbo)
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// we are rendering to a texture now (or screen)
	// there is no need of using a depth buffer
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	// stop writing into the stencil buffer
	glStencilMask(0);
	glDisable(GL_STENCIL_TEST);

	// bind texture used by directional and point light passes
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, deferredRenderingFBO.getDiffuseBuffer().getId());
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, deferredRenderingFBO.getAdditionalBuffer().getId());
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, deferredRenderingFBO.getPositionBuffer().getId());
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, deferredRenderingFBO.getNormalBuffer().getId());

	// perform directional light pass (include shadows)
	directionalLightPass(DEFERRED_STENCIL_MARK, mDirectionalLightDeferred);
	directionalLightPass(PBR_STENCIL_MARK, mDirectionalLightDeferredPBR);

	// perform point light pass (include shadows)
	pointLightPass(DEFERRED_STENCIL_MARK, mPointLightDeferred);
	pointLightPass(PBR_STENCIL_MARK, mPointLightDeferredPBR);

	// unbind textures
	for (int i = 3; i >= 0; --i) {
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
}

void RenderSystem::directionalLightPass(GLuint mark, DeferredLightShader& shaderWrapper)
{
	glBindVertexArray(mScreenMesh.mVao);

	// enable stencil test so that this operation is only carried
	// out for those pixels actually drawn during deferred rendering
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_EQUAL, mark, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

	// for multiple lights
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_ONE, GL_ONE);

	shaderWrapper.shader.use();

	for (std::size_t i = 0; i < mLights.size(); i++) {
		const auto& light = mLights[i]->getComponent<Light>();
		if (light->getType() != Light::Type::DIRECTIONAL)
			continue;

		// can cast safely now
		const DirectionalLight* directionalLight = static_cast<const DirectionalLight*>(light.get());

		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D, directionalLight->getShadowMapTarget().getDepthBuffer().getId());

		shaderWrapper.setLightIndex(i);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void *)0);

		glBindTexture(GL_TEXTURE_2D, 0);
	}

	glDisable(GL_BLEND);
	glDisable(GL_STENCIL_TEST);
}

void RenderSystem::stencilPass(int lightIndex, float radius)
{
	// different for every sphere
	glClear(GL_STENCIL_BUFFER_BIT);

	// both faces of the sphere must be rendered
	glDisable(GL_CULL_FACE);
	// depth test must be enabled
	glEnable(GL_DEPTH_TEST);

	glStencilFunc(GL_ALWAYS, 0, 0x00);
	// increment if back face of sphere is behind something
	glStencilOpSeparate(GL_BACK, GL_REPLACE, GL_INCR_WRAP, GL_KEEP);
	// decrement if front face of sphere is before something
	glStencilOpSeparate(GL_FRONT, GL_REPLACE, GL_DECR_WRAP, GL_KEEP);

	// do not write this sphere on the color buffer
	glDrawBuffer(GL_NONE);
	mPointLightDeferredStencil.use();
	mPointLightDeferredStencil.setFloat(mPointLightStencilScaleLocation, radius);
	mPointLightDeferredStencil.setInt(mPointLightStencilLightIndexLocation, lightIndex);

	glBindVertexArray(mPointLightSphere.mVao);
	glDrawElements(GL_TRIANGLES, mPointLightSphere.mIndicesNumber, GL_UNSIGNED_INT, (void *)0);

	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
}

void RenderSystem::pointLightPass(GLuint mark, DeferredLightShader& shaderWrapper)
{
	/* This mask prevents the two most significant bits from being deleted 
	 * This mask is used so that the stencil buffer can be
	 * cleared without losing the information on pbr vs. blinn-phong */
	glStencilMask(0x3F);  
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_STENCIL_BUFFER_BIT);
	glEnable(GL_STENCIL_TEST);

	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_ONE, GL_ONE);

	for (std::size_t i = 0; i < mLights.size(); i++) {
		const auto& light = mLights[i]->getComponent<Light>();
		if (light->getType() != Light::Type::POINT)
			continue;

		const PointLight* pointLight = static_cast<const PointLight*>(light.get());

		float radius = pointLight->getRadius();

		stencilPass(i, radius);

		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_CUBE_MAP, pointLight->getPointShadowTarget().getDepthBuffer().getId());

		/* mark is used to shader only the pixel of this phase. +1 is needed to identify those pixels
		   that are not inside a light sphere */
		glStencilFunc(GL_EQUAL, mark + 1, 0xFF);
		shaderWrapper.shader.use();

		shaderWrapper.setLightIndex(i);
		shaderWrapper.setLightRadius(radius);
		glBindVertexArray(mScreenMesh.mVao);
		glDrawElements(GL_TRIANGLES, mPointLightSphere.mIndicesNumber, GL_UNSIGNED_INT, (void *)0);

		glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	}

	glDisable(GL_BLEND);
	glDisable(GL_STENCIL_TEST);
}

void RenderSystem::finalizeRendering()
{
	effectManager.update();
	effectManager.mPostProcessingShader.use();

	glBindFramebuffer(GL_FRAMEBUFFER, 0); // unbind effects frame buffer

	glViewport(0, 0, getScreenWidth(), getScreenHeight());

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	glBindVertexArray(mScreenMesh.mVao);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, effectTarget.getColorBuffer().getId());


	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, effectTarget.getDepthBuffer().getId());
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void *)0);

	glEnable(GL_DEPTH_TEST);
}

void RenderSystem::renderShadows()
{
	for (const auto& lightGO : mLights) {
		const auto& light = lightGO->getComponent<Light>();

		if (light->getShadowCasterMode() == Light::ShadowCasterMode::NO_SHADOWS || !light->needsShadowUpdate())  continue;

		if (light->getType() == Light::Type::DIRECTIONAL)
			renderDirectionalLightShadows(static_cast<const DirectionalLight*>(light.get()), lightGO->transform);
		else if (light->getType() == Light::Type::POINT)
			renderPointLightShadows(static_cast<const PointLight*>(light.get()), lightGO->transform);
	}
}

void RenderSystem::renderDirectionalLightShadows(const DirectionalLight* light, const Transform& lightTransform)
{
	// 	glm::mat4 lightProjection = glm::mat4{ 0.0f };
	// 	lightProjection[0][0] = 2.0f / shadowMappingSettings.width;
	// 	lightProjection[1][1] = 2.0f / shadowMappingSettings.height;
	// 	lightProjection[2][2] = -2.0f / shadowMappingSettings.depth;
	// 	lightProjection[3][3] = 1.0f;
	glm::mat4 lightProjection = glm::ortho(-shadowMappingSettings.width / 2, shadowMappingSettings.width / 2,
		-shadowMappingSettings.height / 2, shadowMappingSettings.height / 2,
		0.1f, shadowMappingSettings.depth);

	glm::mat4 lightView = getViewMatrix(lightTransform);

	updateMatrices(&lightProjection, &lightView);

	glViewport(0, 0, shadowMappingSettings.mapWidth, shadowMappingSettings.mapHeight);
	glBindFramebuffer(GL_FRAMEBUFFER, light->getShadowMapTarget().getFbo());
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT);

	if (shadowMappingSettings.useFastShader)
		Engine::gameObjectRenderer.forceMaterial(mShadowMapMaterial);
	render(RenderPhase::SHADOW_MAPPING);
	if (shadowMappingSettings.useFastShader)
		Engine::gameObjectRenderer.forceMaterial(nullptr);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderSystem::renderPointLightShadows(const PointLight* light, const Transform& lightTransform)
{
	const glm::vec3& lightPos = lightTransform.getPosition();
	float aspect = (float)1024 / (float)1024;
	float near = 1.0f;
	float farPlane = light->getRadius();
	glm::mat4 projection = glm::perspective(glm::radians(90.0f), aspect, near, farPlane);

	std::vector<glm::mat4> transforms{
		projection * glm::lookAt(lightPos, lightPos + glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)),
		projection * glm::lookAt(lightPos, lightPos + glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)),
		projection * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 1.0, 0.0), glm::vec3(0.0, 0.0, 1.0)),
		projection * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, -1.0)),
		projection * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, -1.0, 0.0)),
		projection * glm::lookAt(lightPos, lightPos + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0))
	};

	mPointShadowMaterial->setTransformations(transforms, farPlane, lightPos);

	Engine::gameObjectRenderer.forceMaterial(mPointShadowMaterial);

	glViewport(0, 0, 1024, 1024); // TODO use settings
	glBindFramebuffer(GL_FRAMEBUFFER, light->getPointShadowTarget().getFbo());
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT);

	render(RenderPhase::SHADOW_MAPPING);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	Engine::gameObjectRenderer.forceMaterial(nullptr);
}

void RenderSystem::addLight(const GameObjectEH& light)
{
	if (light->getComponent<Light>() == nullptr) {
		std::cerr << "Adding a light without a Light component, discarded\n";
		return;
	}

	mLights.push_back(light);
}

std::int32_t RenderSystem::getScreenWidth() const
{
	std::int32_t w;
	SDL_GetWindowSize(mWindow, &w, nullptr);
	return w;
}

void RenderSystem::setCamera(const GameObjectEH& camera)
{
	std::shared_ptr<CameraComponent> cameraComponent = camera->getComponent<CameraComponent>();
	if (!camera || !camera->getComponent<CameraComponent>()) {
		std::cerr << "Setting a camera without a CameraComponent, discarded\n";
		return;
	}

	mCamera = camera;

	// re-compute the projection matrix
	mProjection = glm::perspective(cameraComponent->getFOV(), static_cast<float>(getScreenWidth()) / getScreenHeight(),
		cameraComponent->getNearPlaneDistance(), cameraComponent->getFarPlaneDistance());
}

GameObjectEH RenderSystem::getCamera() const
{
	return mCamera;
}

std::int32_t RenderSystem::getScreenHeight() const
{
	std::int32_t h;
	SDL_GetWindowSize(mWindow, nullptr, &h);
	return h;
}
/*
float RenderSystem::getNearPlane() const
{
	return mNearPlane;
}

float RenderSystem::getFarPlane() const
{
	return mFarPlane;
}

float RenderSystem::getVerticalFov() const
{
	return mVerticalFov;
}*/

int RenderSystem::getRenderPhase() const
{
	return mRenderPhase;
}

void RenderSystem::enableClipPlane() const
{
	glEnable(GL_CLIP_DISTANCE0);
}

void RenderSystem::disableClipPlane() const
{
	glDisable(GL_CLIP_DISTANCE0);
}

void RenderSystem::setClipPlane(const glm::vec4& clipPlane) const
{
	glBindBuffer(GL_UNIFORM_BUFFER, mUboCommonMat);
	// it is after 3 matrices (projection, view, projectionView)
	glBufferSubData(GL_UNIFORM_BUFFER, 3 * sizeof(glm::mat4), sizeof(glm::vec4), glm::value_ptr(clipPlane));
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void RenderSystem::copyTexture(const Texture& src, RenderTarget& dst, const Shader& shader, bool clear)
{
	shader.use();

	glBindFramebuffer(GL_FRAMEBUFFER, dst.getFbo());

	glViewport(0, 0, dst.getWidth(), dst.getHeight());

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	if (clear) glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	glBindVertexArray(mScreenMesh.mVao);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, src.getId());

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void *)0);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glEnable(GL_DEPTH_TEST);

	glViewport(0, 0, getScreenWidth(), getScreenHeight());
}

void RenderSystem::cleanUp()
{
	// Delete uniform buffers
	glDeleteBuffers(1, &mUboCommonMat);
	glDeleteBuffers(1, &mUboLights);
	mShadowMapMaterial = nullptr;
	mPointShadowMaterial = nullptr;

	effectManager.cleanUp();

	// cleans shaders
	mPointLightDeferred.cleanUp();
	mPointLightDeferredPBR.cleanUp();
	mPointLightDeferredStencil = Shader();
	mDirectionalLightDeferred.cleanUp();
	mDirectionalLightDeferredPBR.cleanUp();

	// Destroys the window and quit SDL
	SDL_DestroyWindow(mWindow);
	SDL_Quit();
}