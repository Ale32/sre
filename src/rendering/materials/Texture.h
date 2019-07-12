#ifndef TEXTURE_H
#define TEXTURE_H
#include "RefCount.h"
#include <cstdint>
#include <string>
#include <glad/glad.h>
#include <map>

/**
 * A drawable image.
 * Textures can be used to display images or as drawing targets */
class Texture {
	public:
		RefCount refCount;

    private:
		static std::map<std::string, Texture> textureCache;

        std::uint32_t mTextureId = 0;

        Texture(std::uint32_t id);

		int mWidth = 0;
		int mHeight = 0;

    public:
        /**
          * Creates an invalid texture.
          * Use one of the static load* methods to load a texture. */
        Texture() = default;

        /**
          * Loads and returns the texture at a given path.
          * Textures are also cached using their path as the cache key.
		  *
          * @param path the path of the file to load
          * @return the loaded texture
          */
        static Texture loadFromFile(const std::string& path, int wrapS = GL_REPEAT, int wrapT = GL_REPEAT);

        /**
          * Loads a texture from a buffer in memory.
		  * This texture are not cached. If you can 
		  * specify an identifier to cache textures loaded from
		  * memory use loadFromMemoryCached().
          * @param data a pointer to the buffer
          * @param length of the buffer
          * @return the loaded texture */
        static Texture loadFromMemory(std::uint8_t* data, std::int32_t len, int wrapS = GL_REPEAT, int wrapT = GL_REPEAT);
		
		/**
		 * Loads a texture from a buffer in memory and caches it.
		 * @sa loadFromMemory()
		 * @param cacheKey the name used to cache this texture
		 * @param data a pointer to the buffer
		 * @param length of the buffer
		 * @return the loaded texture */
		static Texture loadFromMemoryCached(const std::string& cacheKey, std::uint8_t* data, std::int32_t len, int wrapS = GL_REPEAT, int wrapT = GL_REPEAT);

		/**
		 * Creates a new Texture.
		 * @param data pixel data of the image
		 * @param width width of the texture
		 * @param height height of the texture
		 * @param wrapS repeat mode on x axis
		 * @param wrapT repeat mode on y axis
		 * @param mipmap should use mipmap?
		 * @param format pixel data format format (GL_RGB, GL_RGBA, etc)
		 * @param type the type of data stored in the texture
		 * @param internalFormat the format of the data stored in the texture (if GL_REPEAT then it will be the same as format)
		 */
		static Texture load(std::uint8_t* data, int width, int height,
			int wrapS = GL_REPEAT, int wrapT = GL_REPEAT, bool mipmap = true, int format = GL_RGBA, int type = GL_UNSIGNED_BYTE, int internalFormat = GL_REPEAT);

        /**
          * Loads a cubmap from file.
          * The paths parameter is a map in which the key is the side of the cube
          * (front, bottom, top, back, left, right) and the values are the corresponding paths
          * of the images for those sides.
          * @param paths the paths of the images composing the cubemap.
          * @return a new cubemap */
        static Texture loadCubamapFromFile(const std::map<std::string, std::string>& paths);

        std::string nameInShader;

        /**
          * Returns the texture id for this texture.
          * Needed for rendering */
        std::uint32_t getId() const;

		/**
		 * @return the width of the texure
		 */
		int getWidth() const;

		/**
		 * @return the height of the texture
		 */
		int getHeight() const;

        /**
          * Checks whether this is a valid (usable) texture
          * @return whether the texture is valid or not. */
        bool isValid() const;

        /**
          * Checks whether this is a valid (usable) texture
          * @return whether the texture is valid or not.
          * @sa isValid */
        operator bool() const;

		virtual ~Texture();
};

#endif // TEXTURE_H
