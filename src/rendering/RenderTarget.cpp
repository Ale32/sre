#include "RenderTarget.h"
#include <glad/glad.h>
#include <iostream>

void RenderTarget::create(std::uint32_t width, std::uint32_t height)
{
	mColorBuffer = Texture::load(nullptr, width, height, GL_REPEAT, GL_REPEAT, false, GL_RGBA);
	mDepthBuffer = Texture::load(nullptr, width, height, GL_REPEAT, GL_REPEAT, false, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, GL_DEPTH24_STENCIL8);


	createWith(mColorBuffer, mDepthBuffer);
}

void RenderTarget::createWith(const Texture& colorBuffer, const Texture& depthBuffer)
{
	mWidth = colorBuffer.getWidth();
	mHeight = colorBuffer.getHeight();

	mColorBuffer = colorBuffer;
	mDepthBuffer = depthBuffer;

	glGenFramebuffers(1, &mFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, mFbo);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer.getId(), 0);

	if (depthBuffer)
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthBuffer.getId(), 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "RenderTarget frame buffer is incomplete\n";

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::uint32_t RenderTarget::getFbo() const
{
	return mFbo;
}

std::uint32_t RenderTarget::getWidth() const
{
	return mWidth;
}

std::uint32_t RenderTarget::getHeight() const
{
	return mHeight;
}

const Texture& RenderTarget::getColorBuffer() const
{
	return mColorBuffer;
}

const Texture& RenderTarget::getDepthBuffer() const
{
	return mDepthBuffer;
}

RenderTarget::~RenderTarget()
{
	if (mRefCoutner.shouldCleanUp() && mFbo != 0)
		glDeleteFramebuffers(1, &mFbo);
}