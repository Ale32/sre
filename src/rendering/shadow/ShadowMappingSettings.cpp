#include "ShadowMappingSettings.h"
#include "RenderSystem.h"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>


void ShadowMappingSettings::init()
{
	/* Uniform buffer object set up for fog */
	glGenBuffers(1, &mShadowUbo);
	glBindBuffer(GL_UNIFORM_BUFFER, mShadowUbo);
	// 16 bytes: a vec2
	glBufferData(GL_UNIFORM_BUFFER, 16, nullptr, GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, RenderSystem::SHADOWMAP_UNIFORM_BLOCK_INDEX, mShadowUbo);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	updateUbo();
}

void ShadowMappingSettings::updateUbo()
{
	glBindBuffer(GL_UNIFORM_BUFFER, mShadowUbo);
	glm::vec2 shadowParams{ depth, mFadeRange };
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::vec2), glm::value_ptr(shadowParams));

	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void ShadowMappingSettings::setShadowDistance(float shadowDistance)
{
	depth = shadowDistance;
	updateUbo();
}

void ShadowMappingSettings::setShadowFadeRange(float range)
{
	mFadeRange = range;
	updateUbo();
}

ShadowMappingSettings::~ShadowMappingSettings()
{
	glDeleteBuffers(1, &mShadowUbo);
}
