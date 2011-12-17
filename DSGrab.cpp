// DSGrab Version 1.0.0

//The MIT License
//
//Copyright (c) 2011 Sahab Yazdani
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in
//all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.

// Win32
#include <windows.h>
#include <tchar.h>

// COM

/*
#pragma include_alias( "dxtrans.h", "qedit.h" )
#define __IDxtCompositor_INTERFACE_DEFINED__
#define __IDxtAlphaSetter_INTERFACE_DEFINED__
#define __IDxtJpeg_INTERFACE_DEFINED__
#define __IDxtKey_INTERFACE_DEFINED__
*/

#include <dshow.h>
#include "qedit.h"
#include <comutil.h>
#include <dvdmedia.h>

// GDI+
#include <gdiplus.h>
#include <gdipluspixelformats.h>

// std c++
#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <memory>

// boost
#include <boost/lexical_cast.hpp>

typedef std::basic_string<TCHAR> tstring;
typedef std::basic_stringstream<TCHAR> tstringstream;

#ifdef _UNICODE
#define tcout std::wcout
#define tcerr std::wcerr
#else
#define tcout std::cout
#define tcerr std::cerr
#endif // _UNICODE

static ULONG gdiplusToken;

// The following functions are defined in the DirectShow base class library.
// They are redefined here for convenience, because many applications do not
// need to link to the base class library.
// These functions were copied from: http://cppxml.googlecode.com/svn-history/r16/trunk/englishplayer/EnTranscription/dshowutil.h
// Which in turn were copied from: http://msdn.microsoft.com/en-us/library/windows/desktop/dd375432%28v=VS.85%29.aspx
#ifndef __STREAMS__

// FreeMediaType: Release the format block for a media type.
inline void FreeMediaType(AM_MEDIA_TYPE& mt)
{
    if (mt.cbFormat != 0)
    {
        CoTaskMemFree((PVOID)mt.pbFormat);
        mt.cbFormat = 0;
        mt.pbFormat = NULL;
    }
    if (mt.pUnk != NULL)
    {
        // Unecessary because pUnk should not be used, but safest.
        mt.pUnk->Release();
        mt.pUnk = NULL;
    }
}

// DeleteMediaType:
// Delete a media type structure that was created on the heap, including the
// format block)
inline void DeleteMediaType(AM_MEDIA_TYPE *pmt)
{
    if (pmt != NULL)
    {
        FreeMediaType(*pmt); 
        CoTaskMemFree(pmt);
    }
}


#endif

namespace Exception {
	class NoSuchDevice {
	};
	class COMError {
	public:
		COMError( HRESULT hr ) : hr(hr) {};
		HRESULT hr;
	};
	class NoSuchCLSID {
	};
	class BadExtension {
	public:
		BadExtension( tstring extension ) : extension(extension) {};
		tstring extension;
	};
}

class CaptureDevice {
public:
	typedef std::vector<IPin *> IPinCollection;

public:
	CaptureDevice( IBaseFilter *f );
	~CaptureDevice();

	void EnumerateDeviceCaps( );

	static void EnumerateCaptureDevices( std::map< tstring, IBaseFilter * > &filterMap );

	void SetResolution( LONG width, LONG height, WORD bitDepth = 0 );

	Gdiplus::Bitmap *GetSingleSnapshot( DWORD wait = 0 );
protected:
	IPin *FindVideoOutputPin( );

	Gdiplus::Bitmap *SerializeFrame( ISampleGrabber *sampleGrabber );
	static IPin *_findPin( IBaseFilter *f, PIN_DIRECTION direction );
private:
	IBaseFilter *inputFilter;
	IBaseFilter *outputFilter; SIZE outputDimensions;
	IPin *videoOutputPin;
};

CaptureDevice::CaptureDevice( IBaseFilter *f ) :
inputFilter(f), outputFilter(NULL) {
	outputDimensions.cx = 0;
	outputDimensions.cy = 0;
	try {
		videoOutputPin = FindVideoOutputPin( );
		if ( videoOutputPin == NULL ) throw Exception::NoSuchDevice();
	} catch ( ... ) {
		throw;
	}
}

CaptureDevice::~CaptureDevice() {
	// cleanup Interfaces
	videoOutputPin->Release();

	inputFilter->Release();
	if ( outputFilter!=NULL ) outputFilter->Release();
}

void CaptureDevice::EnumerateDeviceCaps() {
	HRESULT hr;

	IAMStreamConfig *streamConfig;
	int capCount, capSize;
	VIDEO_STREAM_CONFIG_CAPS caps;
	AM_MEDIA_TYPE *media_type;

	hr = videoOutputPin->QueryInterface( IID_IAMStreamConfig, reinterpret_cast< LPVOID * >( &streamConfig ) );
	if ( FAILED( hr ) ) {
		throw Exception::COMError( hr );
	}
	hr = streamConfig->GetNumberOfCapabilities( &capCount, &capSize );

	for ( int i=0;i<capCount;i++ ) {
		streamConfig->GetStreamCaps( i, &media_type, reinterpret_cast< BYTE * >( &caps ) );

		if ( media_type->majortype!=MEDIATYPE_Video ) continue;

		unsigned char bitDepth;
		if		( media_type->subtype == MEDIASUBTYPE_RGB1 ) { bitDepth = 1; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB4 ) { bitDepth = 4; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB8 ) { bitDepth = 8; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB555 ) { bitDepth = 15; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB565 ) { bitDepth = 16; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB24 ) { bitDepth = 24; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB32 ) { bitDepth = 32; }
		else { bitDepth = 0; }

		tcout << (i+1) << _T( ") Width: " ) << caps.InputSize.cx << 
			_T( "\tHeight:" ) << caps.InputSize.cy <<
			_T( "\tBit Depth:" ) << bitDepth <<
			std::endl;

		DeleteMediaType(media_type);
	}

	streamConfig->Release();
}

Gdiplus::Bitmap *CaptureDevice::GetSingleSnapshot( DWORD wait /* = 0 */ ) {
	using namespace Gdiplus;

	HRESULT hr;

	IGraphBuilder *graphBuilder;
	IVideoWindow *videoWindow;
	IMediaControl *control;
	IMediaEvent *mediaEvent;

	ISampleGrabber *sampleGrabber;

	if ( outputFilter == NULL ) {
		hr = ::CoCreateInstance( CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, reinterpret_cast< LPVOID * >( &outputFilter ) );
		hr = outputFilter->QueryInterface( IID_ISampleGrabber, reinterpret_cast< LPVOID * >( &sampleGrabber ) );

		IAMStreamConfig *streamConfig;
		AM_MEDIA_TYPE *sourceAM, am;

		hr = videoOutputPin->QueryInterface( IID_IAMStreamConfig, reinterpret_cast< LPVOID * >( &streamConfig ) );
		hr = streamConfig->GetFormat( &sourceAM );

		ZeroMemory( &am, sizeof( AM_MEDIA_TYPE ) );
		am.majortype = sourceAM->majortype;
		am.subtype = sourceAM->subtype;

		hr = sampleGrabber->SetMediaType( &am );

		DeleteMediaType(sourceAM);

		streamConfig->Release();
	} else {
		hr = outputFilter->QueryInterface( IID_ISampleGrabber, reinterpret_cast< LPVOID * >( &sampleGrabber ) );
	}

	hr = sampleGrabber->SetBufferSamples( TRUE );
	hr = sampleGrabber->SetOneShot( wait == 0 ? TRUE : FALSE );

	hr = ::CoCreateInstance( CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, reinterpret_cast< LPVOID * >( &graphBuilder ) );

	graphBuilder->AddFilter( inputFilter, L"Input" );
	graphBuilder->AddFilter( outputFilter, L"Output" );

	graphBuilder->Connect( videoOutputPin, _findPin( outputFilter, PINDIR_INPUT ) );
	hr = graphBuilder->Render( _findPin( outputFilter, PINDIR_OUTPUT ) );

	hr = graphBuilder->QueryInterface( IID_IVideoWindow, reinterpret_cast< LPVOID * >( &videoWindow ) );
	videoWindow->put_AutoShow( FALSE );
	videoWindow->Release();

	hr = graphBuilder->QueryInterface( IID_IMediaControl, reinterpret_cast< LPVOID * >( &control ) );
	control->Run();

	long eventCode;
	hr = graphBuilder->QueryInterface( IID_IMediaEvent, reinterpret_cast< LPVOID * >( &mediaEvent ) );
	hr = mediaEvent->WaitForCompletion( wait == 0 ? INFINITE : wait, &eventCode );
	mediaEvent->Release();

	Bitmap *target = SerializeFrame( sampleGrabber );

	control->Stop();
	control->Release();

	graphBuilder->Release();
	sampleGrabber->Release();

	return target;
}

Gdiplus::Bitmap *CaptureDevice::SerializeFrame( ISampleGrabber *sampleGrabber ) {
	using namespace Gdiplus;

	HRESULT hr;

	long bufferSize;
	hr = sampleGrabber->GetCurrentBuffer( &bufferSize, NULL );
	unsigned char *buffer = new unsigned char[bufferSize];
	hr = sampleGrabber->GetCurrentBuffer( &bufferSize, reinterpret_cast< long * >( buffer ) );

	AM_MEDIA_TYPE connectedMediaType;
	BITMAPINFOHEADER *bitmapHeader;
	sampleGrabber->GetConnectedMediaType( &connectedMediaType );

	if ( connectedMediaType.formattype == FORMAT_VideoInfo ) {
		VIDEOINFOHEADER *vih = reinterpret_cast< VIDEOINFOHEADER * >( connectedMediaType.pbFormat );
		bitmapHeader = &vih->bmiHeader;
	} else if ( connectedMediaType.formattype == FORMAT_VideoInfo2 ) {
		VIDEOINFOHEADER2 *vih = reinterpret_cast< VIDEOINFOHEADER2 * >( connectedMediaType.pbFormat );
		bitmapHeader = &vih->bmiHeader;
	} else {
		throw Exception::NoSuchDevice();
	}

	PixelFormat bitDepth;
	if		( connectedMediaType.subtype == MEDIASUBTYPE_RGB1 ) { bitDepth = PixelFormat1bppIndexed; }
	else if	( connectedMediaType.subtype == MEDIASUBTYPE_RGB4 ) { bitDepth = PixelFormat4bppIndexed; }
	else if	( connectedMediaType.subtype == MEDIASUBTYPE_RGB8 ) { bitDepth = PixelFormat8bppIndexed; }
	else if	( connectedMediaType.subtype == MEDIASUBTYPE_RGB555 ) { bitDepth = PixelFormat16bppRGB555; }
	else if	( connectedMediaType.subtype == MEDIASUBTYPE_RGB565 ) { bitDepth = PixelFormat16bppRGB565; }
	else if	( connectedMediaType.subtype == MEDIASUBTYPE_RGB24 ) { bitDepth = PixelFormat24bppRGB; }
	else if	( connectedMediaType.subtype == MEDIASUBTYPE_RGB32 ) { bitDepth = PixelFormat32bppRGB; }
	else { throw Exception::NoSuchDevice(); }

	Bitmap *target = new Bitmap( bitmapHeader->biWidth,
		bitmapHeader->biHeight,
		bitDepth );
	Rect rect( 0, 0, bitmapHeader->biWidth, bitmapHeader->biHeight );
	BitmapData bitmapData;
	Status s = target->LockBits( &rect, ImageLockModeWrite, bitDepth, &bitmapData );
	memcpy( reinterpret_cast< void * >( bitmapData.Scan0 ), reinterpret_cast< const void * >( buffer ), bufferSize );
	target->UnlockBits( &bitmapData );
	target->RotateFlip( RotateNoneFlipY ); // stream comes in upside down?

	// don't resize to 0, don't bother resizing to nothingness
	if ( outputDimensions.cx != 0 && outputDimensions.cy !=0 &&
		outputDimensions.cx!=bitmapHeader->biWidth && outputDimensions.cy!=bitmapHeader->biHeight ) {
		// scale the image to the desired size
		Bitmap *scaledTarget = new Bitmap( outputDimensions.cx, outputDimensions.cy );
		Graphics graphics( scaledTarget );
		graphics.DrawImage( target, 0, 0, outputDimensions.cx, outputDimensions.cy );

		delete target;

		target = scaledTarget;
	}

	delete buffer;

	return target;
}

// attempts to find the output pin that matches the type of input the caller gave
// if the method can't find any it throws an exception
// on error, throw an exception
void CaptureDevice::SetResolution( LONG width, LONG height, WORD bitDepth /* = 0 */ ) {
	using namespace std;

	HRESULT hr;

	IAMStreamConfig *streamConfig;
	int capCount, capSize;
	VIDEO_STREAM_CONFIG_CAPS caps;
	AM_MEDIA_TYPE *media_type;

	AM_MEDIA_TYPE *closestMediaType = NULL;
	WORD closestDeltaBitDepth = 0xff; // max value

	// error checking
	if ( width <= 0 || height <= 0 ) {
		return; // error
	}

	// enumerate all the caps, looking for a good match
	hr = videoOutputPin->QueryInterface( IID_IAMStreamConfig, reinterpret_cast< LPVOID * >( &streamConfig ) );
	if ( FAILED( hr ) ) {
		throw Exception::COMError( hr );
	}
	hr = streamConfig->GetNumberOfCapabilities( &capCount, &capSize );
	for ( int i=0;i<capCount;i++ ) {

		streamConfig->GetStreamCaps( i, &media_type, reinterpret_cast< BYTE * >( &caps ) );

		if ( media_type->majortype!=MEDIATYPE_Video ) continue;

// only uncompressed RGB is suitable input right now
		unsigned char bD;
		if		( media_type->subtype == MEDIASUBTYPE_RGB1 ) { bD = 1; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB4 ) { bD = 4; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB8 ) { bD = 8; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB555 ) { bD = 15; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB565 ) { bD = 16; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB24 ) { bD = 24; }
		else if	( media_type->subtype == MEDIASUBTYPE_RGB32 ) { bD = 32; }
		else { continue; }

		WORD deltaBitDepth = abs( bD - bitDepth );
		bool use = false;
		if ( caps.InputSize.cx == width && caps.InputSize.cy == height ) {
			if ( closestMediaType == NULL || ( deltaBitDepth < closestDeltaBitDepth && bitDepth!=0 ) ) {
				closestDeltaBitDepth = deltaBitDepth;
				closestMediaType = media_type;
				use = true;
			}
		}
	}

	if ( closestMediaType == NULL ) {
		streamConfig->Release();

		throw Exception::NoSuchDevice();
	} else {
		streamConfig->SetFormat( closestMediaType );
	}

	streamConfig->Release();
}

IPin *CaptureDevice::FindVideoOutputPin( ) {
	BOOL bFound = FALSE;
	IEnumPins *enumerator;
	IPin *retVal;

	if ( inputFilter==NULL ) {
		throw Exception::NoSuchDevice();
	}

	HRESULT hr = inputFilter->EnumPins(&enumerator);
	if (FAILED(hr))
		throw Exception::COMError( hr );
	while(enumerator->Next(1, &retVal, 0) == S_OK)
	{
		PIN_DIRECTION PinDirThis;
		retVal->QueryDirection(&PinDirThis);
		if ( PinDirThis == PINDIR_OUTPUT ) {
			// it's in the right direction...
			IAMStreamConfig *streamConfig;
			int capCount, capSize;

			hr = retVal->QueryInterface( IID_IAMStreamConfig, reinterpret_cast< LPVOID * >( &streamConfig ) );
			if ( FAILED( hr ) ) {
				throw Exception::COMError( hr );
			}
			hr = streamConfig->GetNumberOfCapabilities( &capCount, &capSize );
			if ( FAILED( hr ) ) {
				throw Exception::COMError( hr );
			}
			if ( capSize == sizeof( VIDEO_STREAM_CONFIG_CAPS ) ) {
				// is a video output pin
				streamConfig->Release();
				bFound = TRUE;

				break;
			}

			streamConfig->Release();
		}
		retVal->Release();
	}
	enumerator->Release();

	return bFound ? retVal : NULL;
}

IPin *CaptureDevice::_findPin( IBaseFilter *f, PIN_DIRECTION direction ) {
	BOOL bFound = FALSE;
	IEnumPins *enumerator;
	IPin *retVal;

	HRESULT hr = f->EnumPins(&enumerator);
	if (FAILED(hr))
		throw Exception::COMError( hr );
	while(enumerator->Next(1, &retVal, 0) == S_OK)
	{
		PIN_DIRECTION PinDirThis;
		retVal->QueryDirection(&PinDirThis);
		if ( PinDirThis == direction ) {
			// it's in the right direction...
			bFound = TRUE;
			break;
		}
		retVal->Release();
	}
	enumerator->Release();

	return bFound ? retVal : NULL;
}

CLSID GetEncoderClsid(const WCHAR* format)
{
	using namespace Gdiplus;

	CLSID pClsid;

   UINT  num = 0;          // number of image encoders
   UINT  size = 0;         // size of the image encoder array in bytes

   ImageCodecInfo* pImageCodecInfo = NULL;

   GetImageEncodersSize(&num, &size);
   if(size == 0)
	   throw Exception::COMError( 0 );

   pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
   if(pImageCodecInfo == NULL)
	   throw Exception::COMError( 0 );

   GetImageEncoders(num, size, pImageCodecInfo);

   for(UINT j = 0; j < num; ++j)
   {
      if( wcscmp(pImageCodecInfo[j].MimeType, format) == 0 )
      {
         pClsid = pImageCodecInfo[j].Clsid;
         free(pImageCodecInfo);
         return pClsid;
      }    
   }

   free(pImageCodecInfo);
   throw Exception::NoSuchCLSID();
}

CLSID CreateEncoderClsid( const tstring &path ) {
	CLSID pClsid;
	tstring extension = path.substr( path.find_last_of( _T( '.' ) ) + 1 );

	if ( extension == _T( "" ) ) {
		throw Exception::BadExtension( extension );
	}

	try {
		if ( extension == _T( "bmp" ) ) {
			pClsid = GetEncoderClsid( L"image/bmp" );
		} else if ( extension == _T( "jpg" ) ) {
			pClsid = GetEncoderClsid( L"image/jpeg" );
		} else if ( extension == _T( "png" ) ) {
			pClsid = GetEncoderClsid( L"image/png" );
		} else if ( extension == _T( "tif" ) ) {
			pClsid = GetEncoderClsid( L"image/tiff" );
		} else if ( extension == _T( "gif" ) ) {
			pClsid = GetEncoderClsid( L"image/gif" ) ;
		} else {
			throw Exception::BadExtension( extension );
		}
	} catch ( ... ) {
		throw;
	}

	return pClsid;
}

void CaptureDevice::EnumerateCaptureDevices( std::map< tstring, IBaseFilter * > &filterMap ) {
	HRESULT result;

	ICreateDevEnum *enumerator;
	IEnumMoniker *enumMoniker;
	IMoniker *moniker;
	ULONG fetched;

	result = ::CoCreateInstance( CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, reinterpret_cast< LPVOID * >( &enumerator ) );

	result = enumerator->CreateClassEnumerator( CLSID_VideoInputDeviceCategory, &enumMoniker, 0 );
	if ( result!=S_OK ) {
		enumerator->Release();

		throw Exception::COMError( result );
	}

	while ( enumMoniker->Next( 1, &moniker, &fetched )==S_OK ) {
		IPropertyBag *bag;
		VARIANT variant;

		result = moniker->BindToStorage( 0, 0, IID_IPropertyBag, reinterpret_cast< LPVOID * >( &bag ) );

		::VariantInit( &variant );

		bag->Read( L"FriendlyName", &variant, NULL );
		_bstr_t friendlyName( variant.bstrVal, true );
		
		bag->Release();
		::VariantClear( &variant );

		IBaseFilter *baseFilter;
		result = moniker->BindToObject( 0, 0, IID_IBaseFilter, reinterpret_cast< LPVOID * >( &baseFilter ) );
		
		filterMap[tstring( friendlyName )] = baseFilter;
		
		::VariantClear( &variant );

		moniker->Release();
	}
	enumMoniker->Release();
	enumerator->Release();
}

void parseCommandLine( tstring cmdLine, std::map<tstring, tstring> &collection ) {
	const TCHAR PARAM_PREFIX = _T( '\\' );
	const TCHAR PARAM_QUOTE = _T( '\"' );

	bool inQuotes = false;
	bool haveSlash = false;

	cmdLine.push_back( _T( ' ' ) );
	tstring *newToken = new tstring();
	for ( tstring::iterator i = cmdLine.begin(); i!=cmdLine.end(); i++ ) {
		switch ( *i ) {
			case _T( ' ' ):
				// the only real delimiter

				if ( !inQuotes ) {
					tstring::size_type colonPos = newToken->find( _T( ':' ) );
					if ( colonPos != tstring::npos ) {
						tstring partA = newToken->substr( 0, colonPos );
						tstring partB = newToken->substr( colonPos + 1 );
						if ( partB[0] == PARAM_QUOTE && partB[partB.length()-1] == PARAM_QUOTE ) {
							partB = partB.substr( 1, partB.length() - 2 );
						}

						collection[partA] = partB;
					} else {
						collection[*newToken] = tstring();
					}
					delete newToken;
					newToken = new tstring();
				} else {
					newToken->push_back( *i );
				}
				break;
			case PARAM_PREFIX:
				// \'s are special because they can negate the quotes

				if ( haveSlash ) {
					newToken->push_back( *i );
					haveSlash = false;
					break;
				} else {
					haveSlash = true;
				}

				break;
			case PARAM_QUOTE:
				// quotes are special cuz they can negate the space
				if ( haveSlash ) {
					haveSlash = false;
				} else {
					inQuotes = !inQuotes;
				}
				newToken->push_back( *i );

				break;
			default:
				if ( haveSlash ) {
					newToken->push_back( PARAM_PREFIX );
					haveSlash = false;
				}

				newToken->push_back( *i );

				break;
		}
	}
}

void tokenize(const tstring& str, std::vector<tstring>& tokens, const tstring& delimiters = _T( " " ) )
{
    // Skip delimiters at beginning.
    tstring::size_type lastPos = str.find_first_not_of(delimiters, 0);
    // Find first "non-delimiter".
    tstring::size_type pos     = str.find_first_of(delimiters, lastPos);

    while (tstring::npos != pos || tstring::npos != lastPos)
    {
        // Found a token, add it to the vector.
        tokens.push_back(str.substr(lastPos, pos - lastPos));
        // Skip delimiters.  Note the "not_of"
        lastPos = str.find_first_not_of(delimiters, pos);
        // Find next "non-delimiter"
        pos = str.find_first_of(delimiters, lastPos);
    }
}

void ShowHeader() {
	using namespace std;

/*
	::GetFileVersionInfoSize( lptstrFilename, lpdwHandle );
	::GetFileVersionInfo( lptstrFilename, dwHandle, dwLen, lpData );
	::VerQueryValue( pBlock, lpSubBlock, lplpBuffer, puLen );
*/

	tcout << _T( "DSGrab Version 1.0.0 By Sahab Yazdani" ) << endl;
	tcout << _T( "http://www.saliences.com/projects/dsgrab/index.html" ) << endl << endl;
}

class COMToken {
public:
	COMToken() {
		HRESULT hr = ::CoInitialize( NULL);
		if ( FAILED( hr ) ) {
			throw Exception::COMError( hr );
		}
	}
	~COMToken() {
		::CoUninitialize();
	}
};

void ShowUsageInformation() {
	using namespace std;

	tcout << _T( "[/list]:                Lists all available Capture Devices on the system" ) << endl;
	tcout << _T( "[/use:\"DEVICE NAME\"]:   Uses the Capture Device with name \"DEVICE NAME\" for" ) << endl;
	tcout << _T( "                        Capture" ) << endl;
	tcout << _T( "[/caps]:                Lists the output formats supported by the Capture" ) << endl;
	tcout << _T( "                        Device specified with /use" ) << endl;
	tcout << _T( "[/dim:WxHxBPP]:         Sets the capture format to one supported by the Capture" ) << endl;
	tcout << _T( "                        Device." ) << endl;
	tcout << _T( "[/resize:WxHxBPP]:      Resizes the captured frame to an image of size W by H" ) << endl;
	tcout << _T( "                        with BPP bits of colour." ) << endl;
	tcout << _T( "[/output:FILENAME]:     Sets the location where the resulting captured image" ) << endl;
	tcout << _T( "                        will be saved." ) << endl;
	tcout << _T( "[/wait:TIME]:           Specifies how long to wait before capturing the image" ) << endl;
	tcout << _T( "                        (in milliseconds)" ) << endl;
	tcout << _T( "[/silent]:              Supresses all output." ) << endl;
	tcout << _T( "[/help]:                Display this help screen." ) << endl;
	tcout << endl;
	tcout << _T( "Examples:" ) << endl;
	tcout << endl;
	tcout << _T( "List the Capture Devices on the system." ) << endl;
	tcout << _T( "\tDSGrab /list" ) << endl;
	tcout << endl;
	tcout << _T( "List the Capture Formats supported by the device \"ATI AVStream Analog Capture\"" ) << endl;
	tcout << _T( "\tDSGrab /use:\"ATI AVStream Analog Capture\" /caps" ) << endl;
	tcout << endl;
	tcout << _T( "Capture a frame of size 320 by 240 with 24 bits of colour from the device" ) << endl;
	tcout << _T( "ATI AVStream Analog Capture\" to the file \"capture.jpg\"" ) << endl;
	tcout << _T( "\tDSGrab /use:\"ATI AVStream Analog Capture\" /dim:320x240x24 /output:capture.jpg" ) << endl;
}

int _tmain( int argc, TCHAR *argv[] ) {
	using namespace Gdiplus;

	GdiplusStartupInput gdiplusStartup;

	try {
		COMToken comToken;

		Status s = GdiplusStartup( &gdiplusToken, &gdiplusStartup, NULL );

		std::map<tstring, tstring> cmdLineArgs;
		parseCommandLine( tstring( ::GetCommandLine() ), cmdLineArgs );

		std::map< tstring, IBaseFilter * > deviceMap;
		try {
			CaptureDevice::EnumerateCaptureDevices( deviceMap );
		} catch ( Exception::COMError ) {
			tcerr << _T( "COM Error enumerating Capture Devices." ) << std::endl; 
			return -1;
		}

		if ( cmdLineArgs.find( _T( "/silent" ) )==cmdLineArgs.end() ) {
			ShowHeader();
		}

		if ( cmdLineArgs.find( _T( "/help" ) )!=cmdLineArgs.end() ) {
			ShowUsageInformation();

			return 0;
		}

		if ( cmdLineArgs.find( _T( "/list" ) )!=cmdLineArgs.end() ) {
			tcout << "Available Capture Devices:" << std::endl;
			
			int counter = 1; 
			for ( std::map< tstring, IBaseFilter * >::iterator i = deviceMap.begin(); i!=deviceMap.end(); i++, counter++ ) {
				tcout << counter << _T( ") " ) << i->first << std::endl;
			}

			return 0;
		}

		if ( cmdLineArgs.find( _T( "/use" ) )!=cmdLineArgs.end() ) { 
			try {
				CaptureDevice device( deviceMap[cmdLineArgs[_T("/use")]] );

				if ( cmdLineArgs.find( _T( "/caps" ) )!=cmdLineArgs.end() ) {
					tcout << "List of Available Output formats for device \"" << cmdLineArgs[_T("/use")] << "\":" << std::endl;
					device.EnumerateDeviceCaps();

					return 0;
				}

				if ( cmdLineArgs.find( _T( "/dim" ) )!=cmdLineArgs.end() ) {
					std::vector<tstring> tokens;
					tokenize( cmdLineArgs[_T( "/dim" )], tokens, _T( "x" ) );
					if ( tokens.size() < 2 ) {
						tcerr << _T( "Not enough parameters for argument //dims." ) << std::endl;

						return -1;
					}

					try {
						LONG width = boost::lexical_cast<LONG>( tokens[0] );
						LONG height = boost::lexical_cast<LONG>( tokens[1] );

						try {
							if ( tokens.size() == 3 ) {							
								device.SetResolution( width, height, (WORD)( boost::lexical_cast<LONG>( tokens[2] ) ) );
							} else {
								device.SetResolution( width, height );
							}
						} catch ( Exception::NoSuchDevice ) {
							tcerr << _T( "The capture device doesn't support the selected image dimensions. Please use the \\caps parameter to see the support output resolutions." ) << std::endl;
						} catch ( Exception::COMError ) {
							// TODO: Error Recover
						}
					} catch ( boost::bad_lexical_cast ) {
						tcerr << _T( "The //dim parameter has not been formatted correctly." ) << std::endl;
						return -1;
					}
				}

				if ( cmdLineArgs.find( _T( "/output" ) )!=cmdLineArgs.end() ) {
					// output something to a bitmap
					DWORD wait = 0;
					if ( cmdLineArgs.find( _T( "/wait" ) )!=cmdLineArgs.end() ) {
						try {
							wait = boost::lexical_cast<DWORD>( cmdLineArgs[_T("/wait")]  );
						} catch ( boost::bad_lexical_cast ) {
							tcerr << _T( "The //wait parameter has not been formatted correctly." ) << std::endl;
							return -1;
						}
					}
					std::auto_ptr<Bitmap> bitmap( device.GetSingleSnapshot( wait ) );

					try {
						CLSID clsidEncoder = CreateEncoderClsid( cmdLineArgs[_T("/output")] );

						if ( cmdLineArgs.find( _T( "/resize" ) )!=cmdLineArgs.end() ) {
							std::vector<tstring> tokens;
							tokenize( cmdLineArgs[_T( "/resize" )], tokens, _T( "x" ) );
							if ( tokens.size() < 2 ) {
								tcerr << _T( "Not enough parameters for argument //resize." ) << std::endl;

								return -1;
							}

							LONG width = boost::lexical_cast<LONG>( tokens[0] );
							LONG height = boost::lexical_cast<LONG>( tokens[1] );
							PixelFormat bitDepth;

							if ( tokens.size() == 3 ) {
								WORD bD = boost::lexical_cast<WORD>( tokens[2] );

								switch ( bD ) {
									case 1:
										bitDepth = PixelFormat1bppIndexed;
										break;
									case 4:
										bitDepth = PixelFormat4bppIndexed;
										break;
									case 8:
										bitDepth = PixelFormat8bppIndexed;
										break;
									case 16:
										bitDepth = PixelFormat16bppRGB565;
										break;
									case 24:
										bitDepth = PixelFormat24bppRGB;
										break;
									case 32:
										bitDepth = PixelFormat32bppARGB;
									default:
										tcerr << _T( "Unsupported Bit Depth, defaulting to 32 bits." ) << std::endl;
										return -1;
								}
							} else {
								bitDepth = bitmap->GetPixelFormat();
							}

							std::auto_ptr<Bitmap> backSurface( new Bitmap( width, height, bitDepth ) );
							s = backSurface->SetResolution( bitmap->GetHorizontalResolution(), bitmap->GetVerticalResolution() );
							if ( s!=Ok ) {
								tcerr << _T( "Error setting output resolution." ) << std::endl;
								return -1;
							}

							Graphics transformer( backSurface.get() );
							s = transformer.SetInterpolationMode( InterpolationModeHighQualityBicubic );
							if ( s!=Ok ) {
								tcerr << _T( "Error setting Interpolation Mode." ) << std::endl;
								return -1;
							}
							s = transformer.DrawImage( bitmap.get(), 
								Rect( 0, 0, width, height ),
								0, 0, bitmap->GetWidth(), bitmap->GetHeight(),
								UnitPixel,
								NULL, NULL, NULL );
							if ( s!=Ok ) {
								tcerr << _T( "Error resizing output image." ) << std::endl;
								return -1;
							}

							s = backSurface->Save( cmdLineArgs[_T("/output")].c_str(), &clsidEncoder, NULL );
						} else {
							s = bitmap->Save( cmdLineArgs[_T("/output")].c_str(), &clsidEncoder, NULL );
						}

						if ( s!=Ok ) {
							tcerr << _T( "Error saving output image." ) << std::endl;
							return -1;
						}
					} catch ( Exception::NoSuchCLSID ) {
						tcerr << _T( "Cannot convert to the image format specified." ) << std::endl;
						return -1;
					} catch ( Exception::BadExtension e ) {
						tcerr << _T( "The extension " ) << e.extension << _T( " is not support by DSGrab." ) << std::endl;
						return -1;
					} catch ( boost::bad_lexical_cast ) {
						tcerr << _T( "The //resize parameter has not been formatted correctly." ) << std::endl;
						return -1;
					}
				}
			} catch ( Exception::NoSuchDevice ) {
				tcerr << _T( "No Capture Device with the name \"" ) << cmdLineArgs[_T("/use")] << _T( "\" exists. Use the /list argument to find the list of capture devices on your computer." ) << std::endl;

				return -1;
			} catch ( Exception::COMError ) {
				tcerr << _T( "COM Error initializing Capture Device." ) << std::endl;

				return -1;
			}
		}
	} catch ( Exception::COMError ) {
		return -1;
	}

	return 0;
}