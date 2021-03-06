/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2014 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "OgreStableHeaders.h"
#include "OgreImage.h"
#include "OgreImageCodec.h"
#include "OgreImageResampler.h"

namespace Ogre {
    ImageCodec::~ImageCodec() {
    }

    void ImageCodec::decode(const DataStreamPtr& input, const Any& output) const
    {
        OGRE_IGNORE_DEPRECATED_BEGIN
        Codec::DecodeResult res = decode(input);
        OGRE_IGNORE_DEPRECATED_END

        auto pData = static_cast<ImageCodec::ImageData*>(res.second.get());

        Image* dest = any_cast<Image*>(output);
        dest->mWidth = pData->width;
        dest->mHeight = pData->height;
        dest->mDepth = pData->depth;
        dest->mBufSize = pData->size;
        dest->mNumMipmaps = pData->num_mipmaps;
        dest->mFlags = pData->flags;
        dest->mFormat = pData->format;
        // Just use internal buffer of returned memory stream
        dest->mBuffer = res.first->getPtr();
        // Make sure stream does not delete
        res.first->setFreeOnClose(false);
    }

    DataStreamPtr ImageCodec::encode(const Any& input) const
    {
        Image* src = any_cast<Image*>(input);

        auto imgData = std::make_shared<ImageCodec::ImageData>();
        imgData->format = src->getFormat();
        imgData->height = src->getHeight();
        imgData->width = src->getWidth();
        imgData->depth = src->getDepth();
        imgData->size = src->getSize();
        imgData->num_mipmaps = src->getNumMipmaps();

        // Wrap memory, be sure not to delete when stream destroyed
        auto wrapper = std::make_shared<MemoryDataStream>(src->getData(), src->getSize(), false);
        OGRE_IGNORE_DEPRECATED_BEGIN
        return encode(wrapper, imgData);
        OGRE_IGNORE_DEPRECATED_END
    }
    void ImageCodec::encodeToFile(const Any& input, const String& outFileName) const
    {
        Image* src = any_cast<Image*>(input);

        auto imgData = std::make_shared<ImageCodec::ImageData>();
        imgData->format = src->getFormat();
        imgData->height = src->getHeight();
        imgData->width = src->getWidth();
        imgData->depth = src->getDepth();
        imgData->size = src->getSize();
		imgData->num_mipmaps = src->getNumMipmaps();

        // Wrap memory, be sure not to delete when stream destroyed
        auto wrapper = std::make_shared<MemoryDataStream>(src->getData(), src->getSize(), false);
        OGRE_IGNORE_DEPRECATED_BEGIN
        encodeToFile(wrapper, outFileName, imgData);
        OGRE_IGNORE_DEPRECATED_END
    }

    //-----------------------------------------------------------------------------
    Image::Image(PixelFormat format, uint32 width, uint32 height, uint32 depth, uchar* buffer, bool autoDelete)
        : mWidth(0),
        mHeight(0),
        mDepth(0),
        mBufSize(0),
        mNumMipmaps(0),
        mFlags(0),
        mFormat(format),
        mBuffer( NULL ),
        mAutoDelete( true )
    {
        if (format == PF_UNKNOWN)
            return;

        size_t size = calculateSize(0, 1,  width, height, depth, mFormat);

        if (size == 0)
            return;

        if (!buffer)
            buffer = OGRE_ALLOC_T(uchar, size, MEMCATEGORY_GENERAL);
        loadDynamicImage(buffer, width, height, depth, format, autoDelete);
    }

    //-----------------------------------------------------------------------------
    Image::Image( const Image &img )
        : mBuffer( NULL ),
        mAutoDelete( true )
    {
        // call assignment operator
        *this = img;
    }

    //-----------------------------------------------------------------------------
    Image::~Image()
    {
        freeMemory();
    }
    //---------------------------------------------------------------------
    void Image::freeMemory()
    {
        //Only delete if this was not a dynamic image (meaning app holds & destroys buffer)
        if( mBuffer && mAutoDelete )
        {
            OGRE_FREE(mBuffer, MEMCATEGORY_GENERAL);
            mBuffer = NULL;
        }

    }

    //-----------------------------------------------------------------------------
    Image & Image::operator = ( const Image &img )
    {
        freeMemory();
        mWidth = img.mWidth;
        mHeight = img.mHeight;
        mDepth = img.mDepth;
        mFormat = img.mFormat;
        mBufSize = img.mBufSize;
        mFlags = img.mFlags;
        mPixelSize = img.mPixelSize;
        mNumMipmaps = img.mNumMipmaps;
        mAutoDelete = img.mAutoDelete;
        //Only create/copy when previous data was not dynamic data
        if( img.mBuffer && mAutoDelete )
        {
            mBuffer = OGRE_ALLOC_T(uchar, mBufSize, MEMCATEGORY_GENERAL);
            memcpy( mBuffer, img.mBuffer, mBufSize );
        }
        else
        {
            mBuffer = img.mBuffer;
        }

        return *this;
    }

    void Image::setTo(const ColourValue& col)
    {
        OgreAssert(mBuffer, "image is empty");
        if(col == ColourValue::ZERO)
        {
            memset(mBuffer, 0, getSize());
            return;
        }

        uchar rawCol[4 * sizeof(float)]; // max packed size currently is 4*float
        PixelUtil::packColour(col, mFormat, rawCol);
        for(size_t p = 0; p < mBufSize; p += mPixelSize)
        {
            memcpy(mBuffer + p, rawCol, mPixelSize);
        }
    }

    //-----------------------------------------------------------------------------
    Image & Image::flipAroundY()
    {
        if( !mBuffer )
        {
            OGRE_EXCEPT( 
                Exception::ERR_INTERNAL_ERROR,
                "Can not flip an uninitialised texture",
                "Image::flipAroundY" );
        }
        
        mNumMipmaps = 0; // Image operations lose precomputed mipmaps

        ushort y;
        switch (mPixelSize)
        {
        case 1:
            for (y = 0; y < mHeight; y++)
            {
                std::reverse(mBuffer + mWidth * y, mBuffer + mWidth * (y + 1));
            }
            break;

        case 2:
            for (y = 0; y < mHeight; y++)
            {
                std::reverse((ushort*)mBuffer + mWidth * y, (ushort*)mBuffer + mWidth * (y + 1));
            }
            break;

        case 3:
            typedef uchar uchar3[3];
            for (y = 0; y < mHeight; y++)
            {
                std::reverse((uchar3*)mBuffer + mWidth * y, (uchar3*)mBuffer + mWidth * (y + 1));
            }
            break;

        case 4:
            for (y = 0; y < mHeight; y++)
            {
                std::reverse((uint*)mBuffer + mWidth * y, (uint*)mBuffer + mWidth * (y + 1));
            }
            break;

        default:
            OGRE_EXCEPT( 
                Exception::ERR_INTERNAL_ERROR,
                "Unknown pixel depth",
                "Image::flipAroundY" );
            break;
        }

        return *this;

    }

    //-----------------------------------------------------------------------------
    Image & Image::flipAroundX()
    {
        if( !mBuffer )
        {
            OGRE_EXCEPT( 
                Exception::ERR_INTERNAL_ERROR,
                "Can not flip an uninitialised texture",
                "Image::flipAroundX" );
        }
        
        mNumMipmaps = 0; // Image operations lose precomputed mipmaps
        PixelUtil::bulkPixelVerticalFlip(getPixelBox());

        return *this;
    }

    //-----------------------------------------------------------------------------
    Image& Image::loadDynamicImage( uchar* pData, uint32 uWidth, uint32 uHeight,
        uint32 depth,
        PixelFormat eFormat, bool autoDelete, 
        size_t numFaces, uint32 numMipMaps)
    {

        freeMemory();
        // Set image metadata
        mWidth = uWidth;
        mHeight = uHeight;
        mDepth = depth;
        mFormat = eFormat;
        mPixelSize = static_cast<uchar>(PixelUtil::getNumElemBytes( mFormat ));
        mNumMipmaps = numMipMaps;
        mFlags = 0;
        // Set flags
        if (PixelUtil::isCompressed(eFormat))
            mFlags |= IF_COMPRESSED;
        if (mDepth != 1)
            mFlags |= IF_3D_TEXTURE;
        if(numFaces == 6)
            mFlags |= IF_CUBEMAP;
        if(numFaces != 6 && numFaces != 1)
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS, 
            "Number of faces currently must be 6 or 1.", 
            "Image::loadDynamicImage");

        mBufSize = calculateSize(numMipMaps, numFaces, uWidth, uHeight, depth, eFormat);
        mBuffer = pData;
        mAutoDelete = autoDelete;

        return *this;

    }

    //-----------------------------------------------------------------------------
    Image & Image::loadRawData(
        const DataStreamPtr& stream,
        uint32 uWidth, uint32 uHeight, uint32 uDepth,
        PixelFormat eFormat,
        size_t numFaces, uint32 numMipMaps)
    {

        size_t size = calculateSize(numMipMaps, numFaces, uWidth, uHeight, uDepth, eFormat);
        if (size != stream->size())
        {
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS, 
                "Stream size does not match calculated image size", 
                "Image::loadRawData");
        }

        uchar *buffer = OGRE_ALLOC_T(uchar, size, MEMCATEGORY_GENERAL);
        stream->read(buffer, size);

        return loadDynamicImage(buffer,
            uWidth, uHeight, uDepth,
            eFormat, true, numFaces, numMipMaps);

    }
    //-----------------------------------------------------------------------------
    Image & Image::load(const String& strFileName, const String& group)
    {

        String strExt;

        size_t pos = strFileName.find_last_of('.');
        if( pos != String::npos && pos < (strFileName.length() - 1))
        {
            strExt = strFileName.substr(pos+1);
        }

        DataStreamPtr encoded = ResourceGroupManager::getSingleton().openResource(strFileName, group);
        return load(encoded, strExt);

    }
    //-----------------------------------------------------------------------------
    void Image::save(const String& filename)
    {
        if( !mBuffer )
        {
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS, "No image data loaded", 
                "Image::save");
        }

        String strExt;
        size_t pos = filename.find_last_of('.');
        if( pos == String::npos )
            OGRE_EXCEPT(
            Exception::ERR_INVALIDPARAMS, 
            "Unable to save image file '" + filename + "' - invalid extension.",
            "Image::save" );

        while( pos != filename.length() - 1 )
            strExt += filename[++pos];

        Codec * pCodec = Codec::getCodec(strExt);
        if (!pCodec)
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
                        "Unable to save image file '" + filename + "' - invalid extension.");

        pCodec->encodeToFile(this, filename);
    }
    //---------------------------------------------------------------------
    DataStreamPtr Image::encode(const String& formatextension)
    {
        if( !mBuffer )
        {
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS, "No image data loaded", 
                "Image::encode");
        }

        Codec * pCodec = Codec::getCodec(formatextension);
        if (!pCodec)
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
                        "Unable to encode image data as '" + formatextension + "' - invalid extension.");

        return pCodec->encode(this);
    }
    //-----------------------------------------------------------------------------
    Image & Image::load(const DataStreamPtr& stream, const String& type )
    {
        freeMemory();

        Codec * pCodec = 0;
        if (!type.empty())
        {
            // use named codec
            pCodec = Codec::getCodec(type);
        }
        else
        {
            // derive from magic number
            // read the first 32 bytes or file size, if less
            size_t magicLen = std::min(stream->size(), (size_t)32);
            char magicBuf[32];
            stream->read(magicBuf, magicLen);
            // return to start
            stream->seek(0);
            pCodec = Codec::getCodec(magicBuf, magicLen);

            if (!pCodec)
                OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
                            "Unable to load image: Image format is unknown. Unable to identify codec. "
                            "Check it or specify format explicitly.");
        }

        pCodec->decode(stream, this);

        // compute the pixel size
        mPixelSize = static_cast<uchar>(PixelUtil::getNumElemBytes( mFormat ));
        // make sure we delete
        mAutoDelete = true;

        return *this;
    }
    //---------------------------------------------------------------------
    String Image::getFileExtFromMagic(const DataStreamPtr stream)
    {
        // read the first 32 bytes or file size, if less
        size_t magicLen = std::min(stream->size(), (size_t)32);
        char magicBuf[32];
        stream->read(magicBuf, magicLen);
        // return to start
        stream->seek(0);
        Codec* pCodec = Codec::getCodec(magicBuf, magicLen);

        if(pCodec)
            return pCodec->getType();
        else
            return BLANKSTRING;

    }
    //-----------------------------------------------------------------------------
    size_t Image::getSize() const
    {
        return mBufSize;
    }

    //-----------------------------------------------------------------------------
    uint32 Image::getNumMipmaps() const
    {
        return mNumMipmaps;
    }

    //-----------------------------------------------------------------------------
    bool Image::hasFlag(const ImageFlags imgFlag) const
    {
        return (mFlags & imgFlag) != 0;
    }

    //-----------------------------------------------------------------------------
    uint32 Image::getDepth() const
    {
        return mDepth;
    }
    //-----------------------------------------------------------------------------
    uint32 Image::getWidth() const
    {
        return mWidth;
    }

    //-----------------------------------------------------------------------------
    uint32 Image::getHeight() const
    {
        return mHeight;
    }
    //-----------------------------------------------------------------------------
    size_t Image::getNumFaces(void) const
    {
        if(hasFlag(IF_CUBEMAP))
            return 6;
        return 1;
    }
    //-----------------------------------------------------------------------------
    size_t Image::getRowSpan() const
    {
        return mWidth * mPixelSize;
    }

    //-----------------------------------------------------------------------------
    PixelFormat Image::getFormat() const
    {
        return mFormat;
    }

    //-----------------------------------------------------------------------------
    uchar Image::getBPP() const
    {
        return mPixelSize * 8;
    }

    //-----------------------------------------------------------------------------
    bool Image::getHasAlpha(void) const
    {
        return PixelUtil::getFlags(mFormat) & PFF_HASALPHA;
    }
    //-----------------------------------------------------------------------------
    void Image::applyGamma( uchar *buffer, Real gamma, size_t size, uchar bpp )
    {
        if( gamma == 1.0f )
            return;

        OgreAssert( bpp == 24 || bpp == 32, "only 24/32-bit supported");

        uint stride = bpp >> 3;
        
        uchar gammaramp[256];
        const Real exponent = 1.0f / gamma;
        for(int i = 0; i < 256; i++) {
            gammaramp[i] = static_cast<uchar>(Math::Pow(i/255.0f, exponent)*255+0.5f);
        }

        for( size_t i = 0, j = size / stride; i < j; i++, buffer += stride )
        {
            buffer[0] = gammaramp[buffer[0]];
            buffer[1] = gammaramp[buffer[1]];
            buffer[2] = gammaramp[buffer[2]];
        }
    }
    //-----------------------------------------------------------------------------
    void Image::resize(ushort width, ushort height, Filter filter)
    {
        OgreAssert(mAutoDelete, "resizing dynamic images is not supported");
        OgreAssert(mDepth == 1, "only 2D formats supported");

        // reassign buffer to temp image, make sure auto-delete is true
        Image temp(mFormat, mWidth, mHeight, 1, mBuffer, true);
        // do not delete[] mBuffer!  temp will destroy it

        // set new dimensions, allocate new buffer
        mWidth = width;
        mHeight = height;
        mBufSize = PixelUtil::getMemorySize(mWidth, mHeight, 1, mFormat);
        mBuffer = OGRE_ALLOC_T(uchar, mBufSize, MEMCATEGORY_GENERAL);
        mNumMipmaps = 0; // Loses precomputed mipmaps

        // scale the image from temp into our resized buffer
        Image::scale(temp.getPixelBox(), getPixelBox(), filter);
    }
    //-----------------------------------------------------------------------
    void Image::scale(const PixelBox &src, const PixelBox &scaled, Filter filter) 
    {
        assert(PixelUtil::isAccessible(src.format));
        assert(PixelUtil::isAccessible(scaled.format));
        MemoryDataStreamPtr buf; // For auto-delete
        PixelBox temp;
        switch (filter) 
        {
        default:
        case FILTER_NEAREST:
            if(src.format == scaled.format) 
            {
                // No intermediate buffer needed
                temp = scaled;
            }
            else
            {
                // Allocate temporary buffer of destination size in source format 
                temp = PixelBox(scaled.getWidth(), scaled.getHeight(), scaled.getDepth(), src.format);
                buf.reset(OGRE_NEW MemoryDataStream(temp.getConsecutiveSize()));
                temp.data = buf->getPtr();
            }
            // super-optimized: no conversion
            switch (PixelUtil::getNumElemBytes(src.format)) 
            {
            case 1: NearestResampler<1>::scale(src, temp); break;
            case 2: NearestResampler<2>::scale(src, temp); break;
            case 3: NearestResampler<3>::scale(src, temp); break;
            case 4: NearestResampler<4>::scale(src, temp); break;
            case 6: NearestResampler<6>::scale(src, temp); break;
            case 8: NearestResampler<8>::scale(src, temp); break;
            case 12: NearestResampler<12>::scale(src, temp); break;
            case 16: NearestResampler<16>::scale(src, temp); break;
            default:
                // never reached
                assert(false);
            }
            if(temp.data != scaled.data)
            {
                // Blit temp buffer
                PixelUtil::bulkPixelConversion(temp, scaled);
            }
            break;

        case FILTER_LINEAR:
        case FILTER_BILINEAR:
            switch (src.format) 
            {
            case PF_L8: case PF_R8: case PF_A8: case PF_BYTE_LA:
            case PF_R8G8B8: case PF_B8G8R8:
            case PF_R8G8B8A8: case PF_B8G8R8A8:
            case PF_A8B8G8R8: case PF_A8R8G8B8:
            case PF_X8B8G8R8: case PF_X8R8G8B8:
                if(src.format == scaled.format) 
                {
                    // No intermediate buffer needed
                    temp = scaled;
                }
                else
                {
                    // Allocate temp buffer of destination size in source format 
                    temp = PixelBox(scaled.getWidth(), scaled.getHeight(), scaled.getDepth(), src.format);
                    buf.reset(OGRE_NEW MemoryDataStream(temp.getConsecutiveSize()));
                    temp.data = buf->getPtr();
                }
                // super-optimized: byte-oriented math, no conversion
                switch (PixelUtil::getNumElemBytes(src.format)) 
                {
                case 1: LinearResampler_Byte<1>::scale(src, temp); break;
                case 2: LinearResampler_Byte<2>::scale(src, temp); break;
                case 3: LinearResampler_Byte<3>::scale(src, temp); break;
                case 4: LinearResampler_Byte<4>::scale(src, temp); break;
                default:
                    // never reached
                    assert(false);
                }
                if(temp.data != scaled.data)
                {
                    // Blit temp buffer
                    PixelUtil::bulkPixelConversion(temp, scaled);
                }
                break;
            case PF_FLOAT32_RGB:
            case PF_FLOAT32_RGBA:
                if (scaled.format == PF_FLOAT32_RGB || scaled.format == PF_FLOAT32_RGBA)
                {
                    // float32 to float32, avoid unpack/repack overhead
                    LinearResampler_Float32::scale(src, scaled);
                    break;
                }
                // else, fall through
            default:
                // non-optimized: floating-point math, performs conversion but always works
                LinearResampler::scale(src, scaled);
            }
            break;
        }
    }

    //-----------------------------------------------------------------------------    

    ColourValue Image::getColourAt(size_t x, size_t y, size_t z) const
    {
        ColourValue rval;
        PixelUtil::unpackColour(&rval, mFormat, getData(x, y, z));
        return rval;
    }

    //-----------------------------------------------------------------------------    
    
    void Image::setColourAt(ColourValue const &cv, size_t x, size_t y, size_t z)
    {
        PixelUtil::packColour(cv, mFormat, getData(x, y, z));
    }

    //-----------------------------------------------------------------------------    

    PixelBox Image::getPixelBox(size_t face, size_t mipmap) const
    {
        // Image data is arranged as:
        // face 0, top level (mip 0)
        // face 0, mip 1
        // face 0, mip 2
        // face 1, top level (mip 0)
        // face 1, mip 1
        // face 1, mip 2
        // etc
        if(mipmap > getNumMipmaps())
            OGRE_EXCEPT( Exception::ERR_NOT_IMPLEMENTED,
            "Mipmap index out of range",
            "Image::getPixelBox" ) ;
        if(face >= getNumFaces())
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS, "Face index out of range",
            "Image::getPixelBox");
        // Calculate mipmap offset and size
        uint8 *offset = mBuffer;
        // Base offset is number of full faces
        uint32 width = getWidth(), height=getHeight(), depth=getDepth();
        size_t numMips = getNumMipmaps();

        // Figure out the offsets 
        size_t fullFaceSize = 0;
        size_t finalFaceSize = 0;
        uint32 finalWidth = 0, finalHeight = 0, finalDepth = 0;
        for(size_t mip=0; mip <= numMips; ++mip)
        {
            if (mip == mipmap)
            {
                finalFaceSize = fullFaceSize;
                finalWidth = width;
                finalHeight = height;
                finalDepth = depth;
            }
            fullFaceSize += PixelUtil::getMemorySize(width, height, depth, getFormat());

            /// Half size in each dimension
            if(width!=1) width /= 2;
            if(height!=1) height /= 2;
            if(depth!=1) depth /= 2;
        }
        // Advance pointer by number of full faces, plus mip offset into
        offset += face * fullFaceSize;
        offset += finalFaceSize;
        // Return subface as pixelbox
        PixelBox src(finalWidth, finalHeight, finalDepth, getFormat(), offset);
        return src;
    }
    //-----------------------------------------------------------------------------    
    size_t Image::calculateSize(size_t mipmaps, size_t faces, uint32 width, uint32 height, uint32 depth, 
        PixelFormat format)
    {
        size_t size = 0;
        for(size_t mip=0; mip<=mipmaps; ++mip)
        {
            size += PixelUtil::getMemorySize(width, height, depth, format)*faces; 
            if(width!=1) width /= 2;
            if(height!=1) height /= 2;
            if(depth!=1) depth /= 2;
        }
        return size;
    }
    //---------------------------------------------------------------------
    Image & Image::loadTwoImagesAsRGBA(const String& rgbFilename, const String& alphaFilename,
        const String& groupName, PixelFormat fmt)
    {
        Image rgb, alpha;

        rgb.load(rgbFilename, groupName);
        alpha.load(alphaFilename, groupName);

        return combineTwoImagesAsRGBA(rgb, alpha, fmt);

    }
    //---------------------------------------------------------------------
    Image& Image::loadTwoImagesAsRGBA(const DataStreamPtr& rgbStream,
                                      const DataStreamPtr& alphaStream, PixelFormat fmt,
                                      const String& rgbType, const String& alphaType)
    {
        Image rgb, alpha;

        rgb.load(rgbStream, rgbType);
        alpha.load(alphaStream, alphaType);

        return combineTwoImagesAsRGBA(rgb, alpha, fmt);

    }
    //---------------------------------------------------------------------
    Image & Image::combineTwoImagesAsRGBA(const Image& rgb, const Image& alpha, PixelFormat fmt)
    {
        // the images should be the same size, have the same number of mipmaps
        if (rgb.getWidth() != alpha.getWidth() ||
            rgb.getHeight() != alpha.getHeight() ||
            rgb.getDepth() != alpha.getDepth())
        {
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS, 
                "Images must be the same dimensions", "Image::combineTwoImagesAsRGBA");
        }
        if (rgb.getNumMipmaps() != alpha.getNumMipmaps() ||
            rgb.getNumFaces() != alpha.getNumFaces())
        {
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS, 
                "Images must have the same number of surfaces (faces & mipmaps)", 
                "Image::combineTwoImagesAsRGBA");
        }
        // Format check
        if (PixelUtil::getComponentCount(fmt) != 4)
        {
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS, 
                "Target format must have 4 components", 
                "Image::combineTwoImagesAsRGBA");
        }
        if (PixelUtil::isCompressed(fmt) || PixelUtil::isCompressed(rgb.getFormat()) 
            || PixelUtil::isCompressed(alpha.getFormat()))
        {
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS, 
                "Compressed formats are not supported in this method", 
                "Image::combineTwoImagesAsRGBA");
        }

        freeMemory();

        mWidth = rgb.getWidth();
        mHeight = rgb.getHeight();
        mDepth = rgb.getDepth();
        mFormat = fmt;
        mNumMipmaps = rgb.getNumMipmaps();
        size_t numFaces = rgb.getNumFaces();

        // Set flags
        mFlags = 0;
        if (mDepth != 1)
            mFlags |= IF_3D_TEXTURE;
        if(numFaces == 6)
            mFlags |= IF_CUBEMAP;

        mBufSize = calculateSize(mNumMipmaps, numFaces, mWidth, mHeight, mDepth, mFormat);

        mPixelSize = static_cast<uchar>(PixelUtil::getNumElemBytes( mFormat ));

        mBuffer = static_cast<uchar*>(OGRE_MALLOC(mBufSize, MEMCATEGORY_GENERAL));

        // make sure we delete
        mAutoDelete = true;


        for (size_t face = 0; face < numFaces; ++face)
        {
            for (uint8 mip = 0; mip <= mNumMipmaps; ++mip)
            {
                // convert the RGB first
                PixelBox srcRGB = rgb.getPixelBox(face, mip);
                PixelBox dst = getPixelBox(face, mip);
                PixelUtil::bulkPixelConversion(srcRGB, dst);

                // now selectively add the alpha
                PixelBox srcAlpha = alpha.getPixelBox(face, mip);
                uchar* psrcAlpha = srcAlpha.data;
                uchar* pdst = dst.data;
                for (size_t d = 0; d < mDepth; ++d)
                {
                    for (size_t y = 0; y < mHeight; ++y)
                    {
                        for (size_t x = 0; x < mWidth; ++x)
                        {
                            ColourValue colRGBA, colA;
                            // read RGB back from dest to save having another pointer
                            PixelUtil::unpackColour(&colRGBA, mFormat, pdst);
                            PixelUtil::unpackColour(&colA, alpha.getFormat(), psrcAlpha);

                            // combine RGB from alpha source texture
                            colRGBA.a = (colA.r + colA.g + colA.b) / 3.0f;

                            PixelUtil::packColour(colRGBA, mFormat, pdst);
                            
                            psrcAlpha += PixelUtil::getNumElemBytes(alpha.getFormat());
                            pdst += PixelUtil::getNumElemBytes(mFormat);

                        }
                    }
                }
                

            }
        }

        return *this;

    }
    //---------------------------------------------------------------------

    
}
