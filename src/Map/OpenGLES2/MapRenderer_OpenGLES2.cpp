#include "MapRenderer_OpenGLES2.h"

#include <assert.h>

#include <QtMath>

#include "IMapRenderer.h"
#include "IMapTileProvider.h"
#include "IMapBitmapTileProvider.h"
#include "IMapElevationDataProvider.h"
#include "OsmAndCore/Logging.h"
#include "OsmAndCore/Utilities.h"

OsmAnd::MapRenderer_OpenGLES2::P_glTexStorage2DEXT_PROC OsmAnd::MapRenderer_OpenGLES2::glTexStorage2DEXT = nullptr;

OsmAnd::MapRenderer_OpenGLES2::MapRenderer_OpenGLES2()
    : glesExtensions(_glesExtensions)
{
}

OsmAnd::MapRenderer_OpenGLES2::~MapRenderer_OpenGLES2()
{
}

GLenum OsmAnd::MapRenderer_OpenGLES2::validateResult()
{
    auto result = glGetError();
    if(result == GL_NO_ERROR)
        return result;

    const char* errorString = nullptr;
    switch(result)
    {
        case GL_NO_ERROR:
            errorString = "no error";
            break;
        case GL_INVALID_ENUM:
            errorString = "invalid enumerant";
            break;
        case GL_INVALID_VALUE:
            errorString = "invalid value";
            break;
        case GL_INVALID_OPERATION:
            errorString = "invalid operation";
            break;
        case GL_OUT_OF_MEMORY:
            errorString = "out of memory";
            break;
        default:
            errorString = "(unknown)";
            break;
    }
    LogPrintf(LogSeverityLevel::Error, "OpenGLES2 error 0x%08x : %s\n", result, errorString);

    return result;
}

void OsmAnd::MapRenderer_OpenGLES2::initializeRendering()
{
    MapRenderer_BaseOpenGL::initializeRendering();

    const auto glVersionString = glGetString(GL_VERSION);
    GL_CHECK_RESULT;
    LogPrintf(LogSeverityLevel::Info, "Using OpenGLES2 version %s\n", glVersionString);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, reinterpret_cast<GLint*>(&_maxTextureSize));
    GL_CHECK_RESULT;
    LogPrintf(LogSeverityLevel::Info, "OpenGLES2 maximal texture size %dx%d\n", _maxTextureSize, _maxTextureSize);

    GLint maxTextureUnitsInFragmentShader;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTextureUnitsInFragmentShader);
    GL_CHECK_RESULT;
    LogPrintf(LogSeverityLevel::Info, "OpenGLES2 maximal texture units in fragment shader %d\n", maxTextureUnitsInFragmentShader);
    assert(maxTextureUnitsInFragmentShader >= (IMapRenderer::TileLayerId::IdsCount - IMapRenderer::RasterMap));

    GLint maxTextureUnitsInVertexShader;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &maxTextureUnitsInVertexShader);
    GL_CHECK_RESULT;
    LogPrintf(LogSeverityLevel::Info, "OpenGLES2 maximal texture units in vertex shader %d\n", maxTextureUnitsInVertexShader);
    assert(maxTextureUnitsInVertexShader >= IMapRenderer::RasterMap);

    GLint maxFragmentUniformVectors;
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &maxFragmentUniformVectors);
    GL_CHECK_RESULT;
    LogPrintf(LogSeverityLevel::Info, "OpenGLES2 maximal 4-component parameters in fragment shader %d\n", maxFragmentUniformVectors);

    GLint maxVertexUniformVectors;
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &maxVertexUniformVectors);
    GL_CHECK_RESULT;
    LogPrintf(LogSeverityLevel::Info, "OpenGLES2 maximal 4-component parameters in vertex shader %d\n", maxVertexUniformVectors);

    const auto& glesExtensionsString = QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)));
    GL_CHECK_RESULT;
    LogPrintf(LogSeverityLevel::Info, "OpenGLES2 extensions: %s\n", qPrintable(glesExtensionsString));
    _glesExtensions = glesExtensionsString.split(QRegExp("\\s+"), QString::SkipEmptyParts);
    if(!_glesExtensions.contains("GL_OES_vertex_array_object"))
    {
        LogPrintf(LogSeverityLevel::Error, "This device does not support required 'GL_OES_vertex_array_object' extension");
        return false;
    }
    if(!_glesExtensions.contains("GL_OES_texture_float"))
    {
        LogPrintf(LogSeverityLevel::Error, "This device does not support required 'GL_OES_texture_float' extension");
        return false;
    }
    _isSupported_EXT_unpack_subimage = _glesExtensions.contains("GL_EXT_unpack_subimage");
    _isSupported_EXT_texture_storage = _glesExtensions.contains("GL_EXT_texture_storage");
    _isSupported_APPLE_texture_max_level = _glesExtensions.contains("GL_APPLE_texture_max_level");
#if !defined(OSMAND_TARGET_OS_ios)
    if(_isSupported_EXT_texture_storage && !glTexStorage2DEXT)
    {
        glTexStorage2DEXT = static_cast<P_glTexStorage2DEXT_PROC>(eglGetProcAddress(glTexStorage2DEXT));
        assert(glTexStorage2DEXT);
    }
#endif // !OSMAND_TARGET_OS_ios

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    GL_CHECK_RESULT;

    glClearDepthf(1.0f);
    GL_CHECK_RESULT;

    glEnable(GL_DEPTH_TEST);
    GL_CHECK_RESULT;

    glDepthFunc(GL_LEQUAL);
    GL_CHECK_RESULT;
}

void OsmAnd::MapRenderer_OpenGLES2::releaseRendering()
{
    MapRenderer_BaseOpenGL::releaseRendering();
}

void OsmAnd::MapRenderer_OpenGLES2::allocateTexture2D( GLenum target, GLsizei levels, GLsizei width, GLsizei height, GLenum sourceFormat, GLenum sourcePixelDataType )
{
    if(_isSupported_EXT_texture_storage)
    {
        GL_CHECK_PRESENT(glTexStorage2DEXT);

        GLenum textureFormat = GL_INVALID_ENUM;
        if(sourceFormat == GL_RGBA && sourcePixelDataType == GL_UNSIGNED_BYTE)
        {
            //TODO: here in theory we can handle forcing texture to be 16bit RGB5A1
            textureFormat = GL_RGBA8;
        }
        else if(sourceFormat == GL_RGBA && sourcePixelDataType == GL_UNSIGNED_SHORT_4_4_4_4)
        {
            textureFormat = GL_RGBA4;
        }
        else if(sourceFormat == GL_RGB && sourcePixelDataType == GL_UNSIGNED_SHORT_5_6_5)
        {
            textureFormat = GL_RGB5;
        }
        else if(sourceFormat == GL_LUMINANCE && sourcePixelDataType == GL_FLOAT)
        {
            textureFormat = GL_R32F;
        }

        glTexStorage2DEXT(target, levels, textureFormat, width, height);
    }
    else
    {
        GL_CHECK_PRESENT(glTexImage2D);

        size_t pixelSize = 0;
        if(sourceFormat == GL_RGBA && sourcePixelDataType == GL_UNSIGNED_BYTE)
        {
            pixelSize = 4;
        }
        else if(sourceFormat == GL_RGBA && sourcePixelDataType == GL_UNSIGNED_SHORT_4_4_4_4)
        {
            pixelSize = 2;
        }
        else if(sourceFormat == GL_RGB && sourcePixelDataType == GL_UNSIGNED_SHORT_5_6_5)
        {
            pixelSize = 2;
        }
        else if(sourceFormat == GL_LUMINANCE && sourcePixelDataType == GL_FLOAT)
        {
            pixelSize = 4;
        }

        uint8_t* dummyBuffer = new uint8_t[width * height * pixelSize];
        glTexImage2D(target, 0, sourceFormat, width, height, 0, sourceFormat, sourcePixelDataType, dummyBuffer);
        delete[] dummyBuffer;

#if defined(OSMAND_TARGET_OS_ios)
        // Limit MipMap max level if possible
        if(_isSupported_APPLE_texture_max_level)
        {
            GL_CHECK_PRESENT(glTexParameteri);
            glTexParameteri(target, GL_TEXTURE_MAX_LEVEL_APPLE, levels - 1);
            GL_CHECK_RESULT;
        }
#endif //OSMAND_TARGET_OS_ios
    }
}
