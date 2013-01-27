// DSGrab Version 1.5.0

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
#define NOMINMAX
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

// std c++
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <memory>
#include <exception>

// GDI+
using std::min;
using std::max;
#pragma warning(push)
#pragma warning(disable : 4244)
#include <gdiplus.h>
#include <gdipluspixelformats.h>
#pragma warning(pop)

// boost
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

typedef std::basic_string<TCHAR> tstring;
typedef std::basic_stringstream<TCHAR> tstringstream;
typedef boost::program_options::basic_command_line_parser<TCHAR> tcommand_line_parser;

#ifdef _UNICODE
#define tcout std::wcout
#define tcerr std::wcerr
#define tvalue boost::program_options::wvalue
#else
#define tcout std::cout
#define tcerr std::cerr
#define tvalue boost::program_options::value
#endif // _UNICODE

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

#endif // __STREAMS__

namespace Exception {
	class NoSuchDevice {};
	class COMError {
	public:
		COMError( HRESULT hr ) : hr(hr) {};
		HRESULT hr;
	};
	class GdiPlusError {};

	class NoSuchCLSID {};
	class BadExtension {
	public:
		BadExtension( tstring extension ) : extension(extension) {};
		tstring extension;
	};

	class CommandLineError {
	public:
		CommandLineError( tstring error ) : error(error) {};
		tstring error;
	};

	class ResizeError {
	public:
		ResizeError( tstring error ) : error(error) {};
		tstring error;
	};

	class SaveError {};
}

class CaptureDevice {
public:
	typedef std::vector<IPin *> IPinCollection;

public:
	CaptureDevice( IBaseFilter *f );
	~CaptureDevice();

	static void EnumerateCaptureDevices( std::map< tstring, std::pair< IBaseFilter *, int > > &filterMap );

	void SetResolution( LONG width, LONG height );

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
		am.subtype = MEDIASUBTYPE_RGB24;

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

	if ( connectedMediaType.subtype != MEDIASUBTYPE_RGB24 ) {
		throw Exception::NoSuchDevice();
	}

	Bitmap *target = new Bitmap( bitmapHeader->biWidth,
		bitmapHeader->biHeight,
		PixelFormat24bppRGB );
	Rect rect( 0, 0, bitmapHeader->biWidth, bitmapHeader->biHeight );
	BitmapData bitmapData;
	Status s = target->LockBits( &rect, ImageLockModeWrite, PixelFormat24bppRGB, &bitmapData );
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

WORD GetBitDepthFromMediaSubType( GUID &subtype ) {
	if		( subtype == MEDIASUBTYPE_RGB1 ) { return 1; }
	else if	( subtype == MEDIASUBTYPE_RGB4 ) { return 4; }
	else if	( subtype == MEDIASUBTYPE_RGB8 ) { return 8; }
	else if	( subtype == MEDIASUBTYPE_RGB555 ) { return 15; }
	else if	( subtype == MEDIASUBTYPE_RGB565 ) { return 16; }
	else if	( subtype == MEDIASUBTYPE_RGB24 ) { return 24; }
	else if	( subtype == MEDIASUBTYPE_RGB32 ) { return 32; }
	else { return 24; } // the assumption is that the sub-type (typically in the YU family) can automatically be cast to 24-bit RGB
}

// attempts to find the output pin that matches the type of input the caller gave
// if the method can't find any it throws an exception
// on error, throw an exception
void CaptureDevice::SetResolution( LONG desiredWidth, LONG desiredHeight ) {
	using namespace std;

	HRESULT hr;

	IAMStreamConfig *streamConfig;
	int capCount, capSize;
	VIDEO_STREAM_CONFIG_CAPS caps;
	AM_MEDIA_TYPE *media_type;

	AM_MEDIA_TYPE *closestMediaType = NULL;

	LONG desiredResolution, bestResolutionDifference;

	if ( desiredWidth == 0 && desiredHeight == 0 ) {
		desiredResolution = std::numeric_limits<LONG>::max();
	} else {
		desiredResolution = desiredWidth * desiredHeight * 24;
		assert( desiredResolution > 0 );
	}

	// enumerate all the caps, looking for a good match
	hr = videoOutputPin->QueryInterface( IID_IAMStreamConfig, reinterpret_cast< LPVOID * >( &streamConfig ) );
	if ( FAILED( hr ) ) {
		throw Exception::COMError( hr );
	}
	hr = streamConfig->GetNumberOfCapabilities( &capCount, &capSize );
	if ( FAILED( hr ) ) {
		throw Exception::COMError( hr );
	}
	for ( int i=0;i<capCount;i++ ) {
		hr = streamConfig->GetStreamCaps( i, &media_type, reinterpret_cast< BYTE * >( &caps ) );
		if ( FAILED( hr ) ) {
			throw Exception::COMError( hr );
		}

		if ( media_type->majortype!=MEDIATYPE_Video ) continue;

		LONG diff = abs( caps.InputSize.cx * caps.InputSize.cy * GetBitDepthFromMediaSubType( media_type->subtype ) - desiredResolution );
		if ( closestMediaType == NULL || diff < bestResolutionDifference ) {
			bestResolutionDifference = diff;
			closestMediaType = media_type;
		} else {
			DeleteMediaType( media_type );
		}
	}

	if ( closestMediaType == NULL ) {
		streamConfig->Release();

		throw Exception::NoSuchDevice();
	} else {
		streamConfig->SetFormat( closestMediaType );
		DeleteMediaType( closestMediaType );
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

	if ( extension.empty() ) {
		throw Exception::BadExtension( extension );
	}

	try {
		if ( boost::iequals(extension, tstring(_T("bmp")))) {
			pClsid = GetEncoderClsid( L"image/bmp" );
		} else if (boost::iequals(extension, tstring(_T("jpg")))) {
			pClsid = GetEncoderClsid( L"image/jpeg" );
		} else if (boost::iequals(extension, tstring(_T("png")))) {
			pClsid = GetEncoderClsid( L"image/png" );
		} else if (boost::iequals(extension, tstring(_T("tif")))) {
			pClsid = GetEncoderClsid( L"image/tiff" );
		} else if (boost::iequals(extension, tstring(_T("gif")))) {
			pClsid = GetEncoderClsid( L"image/gif" ) ;
		} else {
			throw Exception::BadExtension( extension );
		}
	} catch (...) {
		throw;
	}

	return pClsid;
}

void CaptureDevice::EnumerateCaptureDevices( std::map< tstring, std::pair< IBaseFilter *, int > > &filterMap ) {
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

	int counter = 0;

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
		
		filterMap[tstring( friendlyName )] = std::make_pair(baseFilter, counter);
		
		::VariantClear( &variant );

		moniker->Release();
	}
	enumMoniker->Release();
	enumerator->Release();
}

void ShowHeader() {
	using namespace std;

/*
	::GetFileVersionInfoSize( lptstrFilename, lpdwHandle );
	::GetFileVersionInfo( lptstrFilename, dwHandle, dwLen, lpData );
	::VerQueryValue( pBlock, lpSubBlock, lplpBuffer, puLen );
*/

	tcout << _T( "DSGrab Version 1.5.0 By Sahab Yazdani" ) << endl;
	tcout << _T( "http://www.saliences.com/projects/dsgrab/index.html" ) << endl << endl;
}

class COMToken {
public:
	COMToken() {
		HRESULT hr = ::CoInitialize( NULL );
		if ( FAILED( hr ) ) {
			throw Exception::COMError( hr );
		}
	}
	~COMToken() {
		::CoUninitialize();
	}
};

class GdiPlusToken {
public:
	GdiPlusToken(ULONG token);
	~GdiPlusToken();
private:
	ULONG gdiplusToken;
};

GdiPlusToken::GdiPlusToken(ULONG token) : gdiplusToken(token) {
}

GdiPlusToken::~GdiPlusToken() {
	Gdiplus::GdiplusShutdown(gdiplusToken);
}

std::auto_ptr<GdiPlusToken> InitializeGdiPlus() {
	using namespace Gdiplus;
	using std::exception;
	using std::auto_ptr;

	ULONG gdiplusToken;
	GdiplusStartupInput gdiplusStartup;
	
	Status s = GdiplusStartup( &gdiplusToken, &gdiplusStartup, NULL );

	if ( s!=Ok ) {
		throw exception();
	}

	auto_ptr<GdiPlusToken> token( new GdiPlusToken(gdiplusToken) );
	return token;
}

struct GrabParameters {
	tstring outputFile;

	bool manualDevice;
	tstring device;

	long width;
	long height;

	int wait;

	bool silent;

	bool listDevices;
};

void ShowUsageSamples() {
	using std::endl;

	tcout << _T( "Examples:" ) << endl;
	tcout << endl;
	tcout << _T( "List the Capture Devices on the system." ) << endl;
	tcout << _T( "\tDSGrab --list" ) << endl;
	tcout << endl;
	tcout << _T( "Capture a frame with the maximum native resolution from the default capture device" ) << endl;
	tcout << _T( "\tDSGrab capture.jpg" ) << endl;
	tcout << endl;
	tcout << _T( "Capture a frame of size 320 by 240 from the default capture device" ) << endl;
	tcout << _T( "\tDSGrab -r 320x240 capture.jpg" ) << endl;
	tcout << endl;
}

std::auto_ptr<GrabParameters> ParseArguments( int argc, TCHAR *argv[] ) {
	using namespace boost::program_options;

	using boost::split;
	using boost::is_any_of;
	using boost::lexical_cast;

	using std::auto_ptr;
	using std::string;
	using std::cout;
	using std::cerr;
	using std::endl;
	using std::exception;
	using std::vector;

	options_description requiredOpts( "Required Options" );
	requiredOpts.add_options()
		( "output-file,O", tvalue< tstring >()->required(), "Name of output file" );

	options_description helpOpts( "Help Options" );
	helpOpts.add_options()( "help", "Help!" );

	options_description listOpts( "List Options" );
	listOpts.add_options()( "list", "List all compatible capture devices" );

	options_description basicOpts( "Basic Options" );
	basicOpts.add_options()
		( "device,d", tvalue< tstring >(), "The capture device to use. By default, the first avaiable capture device is used." )
		( "resolution,r", tvalue< tstring >()->default_value( _T( "0x0" ), "" ), "The desired output resolution. The program will automatically choose the closest matching resolution from the device." );

	options_description advancedOpts( "Advanced Options" );
	advancedOpts.add_options()
		( "wait,w", tvalue< int >()->default_value( 0, "" ), "Determines the wait period before taking a snapshot." )
		( "silent,s", "Supresses all output" );

	positional_options_description positional;
	positional.add( "output-file", 1 );

	options_description all( "All Options" );
	all
		.add( requiredOpts )
		.add( basicOpts )
		.add( advancedOpts );

	// parse the parameters
	try {
		variables_map help_variables;
		store( tcommand_line_parser( argc, argv ).options( helpOpts ).allow_unregistered().run(), help_variables );
		notify( help_variables );

		if ( help_variables.count( "help" ) > 0 ) {
			ShowUsageSamples();
			cout << listOpts << endl;
			cout << requiredOpts << endl;
			cout << basicOpts << endl;
			cout << advancedOpts << endl;

			return auto_ptr<GrabParameters>( NULL );
		}

		variables_map list_variables;
		store( tcommand_line_parser( argc, argv ).options( listOpts ).allow_unregistered().run(), list_variables );
		notify( list_variables );

		if (list_variables.count( "list" ) > 0 ) {
			auto_ptr<GrabParameters> p( new GrabParameters() );
			p->listDevices = true;
			return p;
		}

		variables_map vm;
		store( tcommand_line_parser( argc, argv ).options( all ).positional( positional ).run(), vm );
		notify( vm );

		auto_ptr<GrabParameters> params( new GrabParameters() );

		params->outputFile = vm["output-file"].as<tstring>();

		if (vm.count("device")!=0) {
			params->manualDevice = true;
			params->device = vm["device"].as<tstring>();
		} else {
			params->manualDevice = false;
		}

		vector<tstring> tokens;
		split(tokens, vm["resolution"].as<tstring>(), is_any_of(_T("xX")));
		if ( tokens.size() != 2 ) {
			throw Exception::CommandLineError( _T( "\"resolution\" argument is malformed" ) );
		}
		try {
			params->width = lexical_cast<long>( tokens[0] );
			params->height = lexical_cast<long>( tokens[1] );
		} catch ( boost::bad_lexical_cast ) {
			throw Exception::CommandLineError( _T( "\"resolution\" argument is malformed" ) );
		}

		params->wait = vm["wait"].as<int>();

		params->silent = vm.count("silent") > 0;
		params->listDevices = false;

		return params;
	} catch ( exception e ) {
		tcerr << e.what() << endl;
		tcout << endl;
		ShowUsageSamples();
		tcout << endl;
		tcout << "For more information invoke help via \"dsgrab --help\"." << endl;

		throw exception();
	}
}

std::auto_ptr<Gdiplus::Bitmap> ResizeBitmap( std::auto_ptr<Gdiplus::Bitmap> &bitmap, LONG desiredWidth, LONG desiredHeight ) {
	using namespace Gdiplus;
	using std::exception;

	std::auto_ptr<Bitmap> backSurface( new Bitmap( desiredWidth, desiredHeight, PixelFormat24bppRGB ) );
	Status s = backSurface->SetResolution( bitmap->GetHorizontalResolution(), bitmap->GetVerticalResolution() );
	if ( s!=Ok ) {
		throw Exception::ResizeError( _T( "Error setting output resolution." ) );
	}

	Graphics transformer( backSurface.get() );
	s = transformer.SetInterpolationMode( InterpolationModeHighQualityBicubic );
	if ( s!=Ok ) {
		throw Exception::ResizeError( _T( "Error setting Interpolation Mode." ) );
	}
	s = transformer.DrawImage( bitmap.get(), 
		Rect( 0, 0, desiredWidth, desiredHeight ),
		0, 0, bitmap->GetWidth(), bitmap->GetHeight(),
		UnitPixel,
		NULL, NULL, NULL );
	if ( s!=Ok ) {
		throw Exception::ResizeError( _T( "Error resizing output image." ) );
	}

	return backSurface;
}

void SaveBitmap(std::auto_ptr<Gdiplus::Bitmap> &bitmap, tstring outputFile ) {
	using namespace Gdiplus;
	using std::exception;

	CLSID clsidEncoder = CreateEncoderClsid( outputFile );
	Status s = bitmap->Save( outputFile.c_str(), &clsidEncoder, NULL );

	if (s != Ok ) {
		throw Exception::SaveError();
	}
}

int _tmain( int argc, TCHAR *argv[] ) {
	using namespace Gdiplus;
	using std::auto_ptr;
	using std::exception;

	try {
		COMToken comToken;

		auto_ptr<GrabParameters> parameters;
		auto_ptr<GdiPlusToken> gdiplusToken;
	
		try {
			parameters = ParseArguments( argc, argv );
			if (parameters.get() == NULL) {
				return 0;
			}
		} catch ( Exception::CommandLineError e ) {
			tcerr << e.error << std::endl;
			return -1;
		} catch ( ... ) {
			return -1;
		} 

		try {
			gdiplusToken = InitializeGdiPlus();
		} catch (exception e) {
			tcerr << _T( "Error initializing Gdi Plus" ) << std::endl;
			return -1;
		}

		std::map< tstring, std::pair< IBaseFilter *, int > > deviceMap;
		try {
			CaptureDevice::EnumerateCaptureDevices( deviceMap );
		} catch ( Exception::COMError ) {
			tcerr << _T( "COM Error enumerating Capture Devices." ) << std::endl; 
			return -1;
		}

		if ( !parameters->silent ) {
			ShowHeader();
		}

		if ( parameters->listDevices ) {
			tcout << "Available Capture Devices:" << std::endl;
			
			int counter = 1; 
			for ( std::map< tstring, std::pair< IBaseFilter *, int > >::iterator i = deviceMap.begin(); i!=deviceMap.end(); i++, counter++ ) {
				tcout << counter << _T( ") " ) << i->first << std::endl;
			}

			return 0;
		}

		try {
			std::auto_ptr<CaptureDevice> device;
			if ( parameters->manualDevice ) {
				device = std::auto_ptr<CaptureDevice>( new CaptureDevice( deviceMap[parameters->device].first ) );
			} else {
				for ( std::map< tstring, std::pair< IBaseFilter *, int > >::iterator i = deviceMap.begin(); i!=deviceMap.end(); i++ ) {
					if (i->second.second==0) {
						device = std::auto_ptr<CaptureDevice>( new CaptureDevice( i->second.first ) );
						break;
					}
				}
			}

			device->SetResolution(parameters->width, parameters->height);
			std::auto_ptr<Bitmap> bitmap( device->GetSingleSnapshot( parameters->wait ) );

			if ( ( parameters->width == 0 && parameters->height == 0 ) || // if no desired size was given
				( bitmap->GetWidth() == parameters->width && bitmap->GetHeight() == parameters->height ) ) // if the sizes match
			{
				SaveBitmap( bitmap, parameters->outputFile );
			} else {
				std::auto_ptr<Bitmap> resizedBitmap = ResizeBitmap( bitmap, parameters->width, parameters->height );
				SaveBitmap( resizedBitmap, parameters->outputFile );
			}
		} catch ( Exception::ResizeError e ) {
			tcerr << _T( "The following error occurred resizing the image: \"" ) << e.error << _T( "\"" ) << std::endl;
			return -1;
		} catch ( Exception::SaveError ) {
			tcerr << _T( "Error saving output image." ) << std::endl;
			return -1;
		} catch ( Exception::NoSuchCLSID ) {
			tcerr << _T( "Cannot convert to the image format specified." ) << std::endl;
			return -1;
		} catch ( Exception::BadExtension e ) {
			tcerr << _T( "The extension " ) << e.extension << _T( " is not supported by DSGrab." ) << std::endl;
			return -1;
		} catch ( Exception::NoSuchDevice ) {
			tcerr << _T( "No Capture Device with the name \"" ) << parameters->device << _T( "\" exists. Use the --list argument to list the compatible capture devices on your computer." ) << std::endl;
			return -1;
		} catch ( Exception::COMError ) {
			tcerr << _T( "COM Error initializing Capture Device." ) << std::endl;
			return -1;
		}
	} catch ( Exception::COMError ) {
		return -1;
	}

	return 0;
}