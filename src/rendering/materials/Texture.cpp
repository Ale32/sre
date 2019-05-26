#include "Texture.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <iostream>
#include <glad/glad.h>

std::map<std::string, Texture> Texture::textureCache;

Texture Texture::loadFromFile(const std::string& path, int wrapS, int wrapT)
{
	auto cached = textureCache.find(path);
	if (cached != textureCache.end()) {
		std::cout << "found in cache\n";
		return cached->second;
	}

    stbi_set_flip_vertically_on_load(true);

    int width, height, cmp;
    std::uint8_t* data = stbi_load(path.c_str(), &width, &height, &cmp, STBI_rgb_alpha);
    Texture texture;
    if (data == nullptr) {
        std::cerr << "unable to load texture " << path << "\n";
        return texture;
    } else {
        texture = Texture::load(data, width, height, wrapS, wrapT);
        stbi_image_free(data);
    }

	texture.name = path;
	textureCache[path] = texture;
	textureCache[path].refCount.decrease();

    return texture;
}

Texture Texture::loadFromMemory(std::uint8_t* data, std::int32_t len, int wrapS, int wrapT)
{
    stbi_set_flip_vertically_on_load(true);

    int width, height, cmp;
    std::uint8_t* convertedData = stbi_load_from_memory(data, len, &width, &height, &cmp, STBI_rgb_alpha);
    return Texture::load(convertedData, width, height, wrapS, wrapT);
}

Texture Texture::loadCubamapFromFile(const std::map<std::string, std::string>& paths)
{
    std::map<std::string, int> side2glSide{
        {"front", GL_TEXTURE_CUBE_MAP_NEGATIVE_Z},
        {"back", GL_TEXTURE_CUBE_MAP_POSITIVE_Z},
        {"top", GL_TEXTURE_CUBE_MAP_POSITIVE_Y},
        {"bottom", GL_TEXTURE_CUBE_MAP_NEGATIVE_Y},
        {"right", GL_TEXTURE_CUBE_MAP_POSITIVE_X},
        {"left", GL_TEXTURE_CUBE_MAP_NEGATIVE_X}
    };

    std::uint32_t cubemap;
    glGenTextures(1, &cubemap);

    stbi_set_flip_vertically_on_load(false);

    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);
    int width, height, numCh;
    for (auto typePath = paths.begin(); typePath != paths.end(); ++typePath) {
        int side = 0;

        if (side2glSide.count(typePath->first) != 0) {
            side = side2glSide[typePath->first];
        } else {
            std::cout << "Unknown cubemap side " << typePath->first << "\n";
            continue;
        }

        std::uint8_t* data = stbi_load((typePath->second).c_str(), &width, &height, &numCh, STBI_rgb_alpha);
        if (data) {
            glTexImage2D(side, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        else
            std::cout << "unable to load texture " << typePath->second << "\n";
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return Texture{cubemap};
}

Texture Texture::load(std::uint8_t* data, int width, int height, int wrapS, int wrapT)
{
    if (data == nullptr)
        return Texture{0};

    std::uint32_t texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);

    glBindTexture(GL_TEXTURE_2D, 0);

    return Texture{texture};
}

Texture::Texture(std::uint32_t id) : mTextureId{id}
{

}

uint32_t Texture::getId()
{
    return mTextureId;
}

bool Texture::isValid() const
{
    return mTextureId != 0;
}

Texture::operator bool() const
{
    return isValid();
}

Texture::~Texture()
{
	if (refCount.shouldCleanUp() && mTextureId != 0) {
		glDeleteTextures(1, &mTextureId);
		mTextureId = 0;
	}
}
