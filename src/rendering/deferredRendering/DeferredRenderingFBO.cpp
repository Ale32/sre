#include "DeferredRenderingFBO.h"
#include <iostream>

void DeferredRenderingFBO::init(std::uint32_t width, std::uint32_t height)
{
	mDiffuseBuffer		= Texture::load(nullptr, width, height, GL_REPEAT, GL_REPEAT, false, GL_RGBA);
	mSpecularBuffer		= Texture::load(nullptr, width, height, GL_REPEAT, GL_REPEAT, false, GL_RGBA, GL_FLOAT, GL_RGBA16F);
	mPositionBuffer		= Texture::load(nullptr, width, height, GL_REPEAT, GL_REPEAT, false, GL_RGB, GL_FLOAT, GL_RGB16F);
	mNormalBuffer		= Texture::load(nullptr, width, height, GL_REPEAT, GL_REPEAT, false, GL_RGB, GL_FLOAT, GL_RGB16F);
	mNonDeferredBuffer	= Texture::load(nullptr, width, height, GL_REPEAT, GL_REPEAT, false, GL_RGB);
	mDepthBuffer		= Texture::load(nullptr, width, height, GL_REPEAT, GL_REPEAT, false, GL_DEPTH_COMPONENT, GL_FLOAT);

	glGenFramebuffers(1, &mFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, mFbo);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mDiffuseBuffer.getId(), 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, mSpecularBuffer.getId(), 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, mPositionBuffer.getId(), 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, mNormalBuffer.getId(), 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, mNonDeferredBuffer.getId(), 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepthBuffer.getId(), 0);

	unsigned int attachments[]{ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4 };
	glDrawBuffers(5, attachments);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "Deferred rendering buffer is incomplete\n";
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::uint32_t DeferredRenderingFBO::getFBO() const
{
	return mFbo;
}

const Texture& DeferredRenderingFBO::getDiffuseBuffer() const
{
	return mDiffuseBuffer;
}

const Texture& DeferredRenderingFBO::getSpecularBuffer() const
{
	return mSpecularBuffer;
}

const Texture& DeferredRenderingFBO::getPositionBuffer() const
{
	return mPositionBuffer;
}

const Texture& DeferredRenderingFBO::getNormalBuffer() const
{
	return mNormalBuffer;
}

const Texture& DeferredRenderingFBO::getDepthBuffer() const
{
	return mDepthBuffer;
}

const Texture& DeferredRenderingFBO::getNonDeferredBuffer() const
{
	return mNonDeferredBuffer;
}

DeferredRenderingFBO::~DeferredRenderingFBO()
{
	glDeleteFramebuffers(1, &mFbo);
}
