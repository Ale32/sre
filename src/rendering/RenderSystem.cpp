#include "RenderSystem.h"
#include "Shader.h"
#include "Light.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <exception>
#include <algorithm>
#include <iostream>
#include <map>
#include "PhongMaterial.h"

RenderSystem::RenderSystem() : mGameObjectsHL{mGameObjects}, camera{&mGameObjectsHL, 0, 0}
{
    camera = createGameObject();
    camera->name = "defaultCamera";
}

void RenderSystem::createWindow(std::uint32_t width, std::uint32_t height, float fovy, float nearPlane, float farPlane)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cout << "Cannot init sdl " << SDL_GetError() << "\n";
        std::terminate();
    }

    SDL_GL_LoadLibrary(nullptr); // use default opengl
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    mWindow = SDL_CreateWindow("opengl", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (mWindow == nullptr) {
        std::cout << "Cannot create window " << SDL_GetError() << "\n";
        std::terminate();
    }

    SDL_GL_CreateContext(mWindow);

    // Use v-sync
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGLLoader(SDL_GL_GetProcAddress)) {
        std::cout << "Failed to initialize GLAD\n";
        std::terminate();
    }

    initGL(width, height, fovy, nearPlane, farPlane);
}

void RenderSystem::initGL(std::uint32_t width, std::uint32_t height, float fovy, float nearPlane, float farPlane)
{
    mProjection = glm::perspective(fovy, static_cast<float>(width)/height, nearPlane, farPlane);

    /* Uniform buffer object set up for common matrices */
    glGenBuffers(1, &mUboCommonMat);
    glBindBuffer(GL_UNIFORM_BUFFER, mUboCommonMat);
    glBufferData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, COMMON_MAT_UNIFORM_BLOCK_INDEX, mUboCommonMat);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindBuffer(GL_UNIFORM_BUFFER, mUboCommonMat);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(mProjection));
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    /* Uniform buffer object set up for lights */
    glGenBuffers(1, &mUboLights);
    glBindBuffer(GL_UNIFORM_BUFFER, mUboLights);
    // 16 numLights, 112 size of a light array element
    glBufferData(GL_UNIFORM_BUFFER, 16 + 128 * MAX_LIGHT_NUMBER, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, LIGHT_UNIFORM_BLOCK_INDEX, mUboLights);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);


    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
}

void RenderSystem::update()
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    render();

    SDL_GL_SwapWindow(mWindow);
}

void RenderSystem::updateLights()
{
    std::size_t numLight = std::min((std::size_t)MAX_LIGHT_NUMBER, mLights.size());
    glBindBuffer(GL_UNIFORM_BUFFER, mUboLights);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(std::size_t), (void*)&numLight);
    for (std::size_t i = 0; i < numLight; i++) {
        int base = 16 + 128 * i;
        LightPtr lightComponent = mLights[i]->getComponent<Light>();
        if (lightComponent == nullptr) continue;

        std::uint32_t lightType = static_cast<std::uint32_t>(lightComponent->type);
        glm::vec3 attenuation = glm::vec3{
            lightComponent->attenuationConstant,
            lightComponent->attenuationLinear,
            lightComponent->attenuationQuadratic
        };

        glm::vec2 spotAngles{
            glm::cos(lightComponent->innerAngle),
            glm::cos(lightComponent->outerAngle)
        };

        // type
        glBufferSubData(GL_UNIFORM_BUFFER, base, sizeof(std::uint32_t), (void *)(&lightType));

        // position
        glBufferSubData(GL_UNIFORM_BUFFER, base + 16, sizeof(glm::vec3), glm::value_ptr(mLights[i]->transform.getPosition()));

        // direction
        glBufferSubData(GL_UNIFORM_BUFFER, base + 32, sizeof(glm::vec3), glm::value_ptr(mLights[i]->transform.forward()));

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
    }
}

void RenderSystem::render()
{
    /* Camera calculations */
    glm::mat4 view = glm::mat4{1.0f};
    if (camera) {
        view = glm::translate(glm::mat4{1.0f}, -camera->transform.getPosition());
        glm::mat4 inverseRotation = glm::transpose(glm::toMat4(camera->transform.getRotation()));
        view = inverseRotation * view;
    }

    /* Inverts the view so that we watch in the direction of camera->transform.forward() */
    glm::mat4 invert{
        glm::vec4{-1, 0, 0, 0},
        glm::vec4{0, 1, 0, 0},
        glm::vec4{0, 0, -1, 0},
        glm::vec4{0, 0, 0, 1}
    };
    view = invert * view;


    /* Sets the camera matrix to a ubo so that it is shared */
    glBindBuffer(GL_UNIFORM_BUFFER, mUboCommonMat);
    glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(view));
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    updateLights();

    std::map<float, std::pair<Mesh, MaterialPtr>> orderedRender;
    for (auto const& go : mGameObjects) {
        for (std::size_t meshIndex = 0; meshIndex < go.mMeshes.size(); meshIndex++) {
            MaterialPtr material = go.mMaterials[meshIndex];
            Mesh mesh = go.mMeshes[meshIndex];
            if (material) {
                Material* mat = material.get();
                mat->use();

                mat->shader.setMat4("model", go.transform.modelToWorld());
            }

            if (material->needsOrderedRendering()) {
                float order = material->renderOrder(go.transform.getPosition());
                orderedRender[order] = std::make_pair(mesh, material);
                continue;
            }

            draw(mesh, material);
        }
    }

    // render meshed that need to be rendered in a specific order
    for (auto ord = orderedRender.rbegin(); ord != orderedRender.rend(); ++ord) {
        auto data = ord->second;
        auto mesh = data.first;
        auto material = data.second;

        draw(mesh, material);
    }
}

void RenderSystem::draw(Mesh mesh, MaterialPtr material)
{
    material->use();
    glBindVertexArray(mesh.mVao);

    if (mesh.mUsesIndices)
        glDrawElements(mesh.mDrawMode, mesh.mIndicesNumber, GL_UNSIGNED_INT, (void *)0);
    else
        glDrawArrays(mesh.mDrawMode, 0, mesh.mVertexNumber);

    glBindVertexArray(0);

    if (material) material->after();
}

GameObjectEH RenderSystem::createGameObject(const Mesh& mesh, MaterialPtr material)
{
    std::uint32_t index, gen;
    mGameObjectsHL.add(GameObject(mesh, material), index, gen);
    return GameObjectEH(&mGameObjectsHL, index, gen);
}

GameObjectEH RenderSystem::createGameObject()
{
    std::uint32_t index, gen;
    mGameObjectsHL.add(GameObject(), index, gen);
    return GameObjectEH(&mGameObjectsHL, index, gen);
}

void RenderSystem::remove(const GameObjectEH& go)
{
    /* GameObjects form a hierarchy through their transform objects. Removing GameObjects recursively
     * (a parent calls remove on its children) is, however, impossible. In fact, while we are operating on
     * a GameObject living in a vector (mGameObjects) we also change this vector by deleting its children. When
     * control goes back to the parent GameObject the vector it is living in might be moved somewhere else in memory
     * thus invalidating pointers (among which the this pointer). The solution here is to batch delete a node and
     * all its children. Another solution might be to delete the parent first and then its children */

    go->transform.removeParent(); // breaks the hierarchy here
    std::vector<GameObjectEH> toRemove{go};

    // fills this vector with all the GameObjects in the hierarchy
    for (std::size_t i = 0; i < toRemove.size(); ++i) {
        const auto& children = (toRemove[i])->transform.getChildren();
        toRemove.insert(toRemove.end(), children.begin(), children.end());
    }

    // cleans up and removes all the GameObjects in the hierarchy
    for (auto& rem : toRemove) {
        rem->cleanUp();
        mGameObjectsHL.remove(rem.mHandleIndex, rem.mGeneration);
    }
}

void RenderSystem::addLight(const GameObjectEH& light)
{
    if (light->getComponent<Light>() == nullptr) {
        std::cerr << "Adding a light without a Light component, discarded\n";
        return;
    }

    mLights.push_back(light);
}

RenderSystem::~RenderSystem()
{
    // no need to remove them, just clean up what they
    // allocated.
    for (auto& go : mGameObjects)
        go.cleanUp();

    // Delete uniform buffers
    glDeleteBuffers(1, &mUboCommonMat);
    glDeleteBuffers(1, &mUboLights);

    // Destroys the window and quit sdl
    SDL_DestroyWindow(mWindow);
    SDL_Quit();
}
