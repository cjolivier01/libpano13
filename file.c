/* Panorama_Tools	-	Generate, Edit and Convert Panoramic Images
   Copyright (C) 1998,1999 - Helmut Dersch  der@fh-furtwangen.de
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/*------------------------------------------------------------*/

// Functions to read and write Photoshop, and write tiffs


#include "filter.h"





#define WRITEUCHAR( theChar ) 		ch = theChar; count = 1; mywrite(fnum,count,&ch);


#define WRITESHORT( theShort )		svar = theShort; d = data; SHORTNUMBER( svar, d ); \
									count = 2; mywrite	(fnum,count,data);
 

#define WRITELONG( theLong ) 		var = theLong; d = data; LONGNUMBER( var, d ); \
									count = 4; mywrite	(fnum,count,data);

#define READLONG( theLong ) 				count = 4; myread(src,count,data);	\
											d = data; NUMBERLONG( var, d ); 	\
											theLong = var;
									
#define READSHORT( theShort )				count = 2; myread(src,count,data);	\
											d = data; NUMBERSHORT( svar, d ); 	\
											theShort = svar;

#define READUCHAR( theChar ) 				count = 1; myread(src,count,&ch); theChar = ch;	
											


// local functions

static int 				writeImageDataPlanar	( Image *im, file_spec fnum );
static int 				readImageDataPlanar		(Image *im, file_spec fnum ) ;
static int 				ParsePSDHeader			( char *header, Image *im );
static int 				writeChannelData		( Image *im, file_spec fnum, int channel, PTRect *r );
static int 				writeLayerAndMask		( Image *im, file_spec fnum );
static void 			getImageRectangle		( Image *im, PTRect *r );
static int 				fileCopy				( file_spec src, file_spec dest, int numBytes, unsigned char *buf);
static void 			orAlpha					( unsigned char* alpha, unsigned char *buf, Image *im, PTRect *r );
static void 			writeWhiteBackground	( int width, int height, file_spec fnum );
static int				addLayer				( Image *im, file_spec src, file_spec fnum , stBuf *sB);





#define PSDHLENGTH 26

// Save image as single layer PSD file
// Image is background
int writePSD(Image *im, fullPath *sfile )
{
	file_spec	fnum;
	char	data[12], *d;
	short	svar;
	long	count;
	unsigned long	var;
	unsigned char  ch;
	int		channels, BitsPerChannel;
	
	GetChannels( im, channels );
	GetBitsPerChannel( im, BitsPerChannel );
	
	if(  myopen( sfile, write_bin, fnum )   )
	{	
		PrintError("Error Writing Image File");
		return -1;
	}

	// Write PSD Header
	WRITELONG( '8BPS' );
	WRITESHORT( 1 );
	WRITELONG( 0 ); WRITESHORT( 0 );

	WRITEUCHAR( 0 );
	WRITEUCHAR( channels );// No of channels
		
	WRITELONG( im->height );
	WRITELONG( im->width  );
			
	WRITESHORT( BitsPerChannel ); 			// BitsPerChannel
	
	switch( im->dataformat )
	{
		case _Lab:	WRITESHORT( 9 );
					break;
		case _RGB:	WRITESHORT( 3 );
					break;
		default:	WRITESHORT( 3 ); 			
	}
	WRITELONG( 0 );				// Color Mode
	WRITELONG( 0 ); 			// Image Resources
			
		
	WRITELONG( 0 );  			// Layer & Mask	
	
	// Image data 
	
	writeImageDataPlanar( im,  fnum );

	myclose (fnum );
	return 0;
}


// Save image as single layer PSD file
// Image is layer in front of white background
int writePSDwithLayer(Image *im, fullPath *sfile )
{
	file_spec	fnum;
	char	data[12], *d;
	long	count;
	unsigned long	var;
	unsigned char  ch;
	short	svar;
	int		BitsPerChannel;

	TwoToOneByte( im ); // Multilayer image format doesn't support 16bit channels
	
	GetBitsPerChannel( im, BitsPerChannel );
	
	if(  myopen( sfile, write_bin, fnum )   )
	{	
		PrintError("Error Writing Image File");
		return -1;
	}

	// Write PSD Header
	WRITELONG( '8BPS' );
	WRITESHORT( 1 );
	WRITELONG( 0 ); WRITESHORT( 0 );

	WRITEUCHAR( 0 );
	WRITEUCHAR( 3 );			// No of channels; Background always white, 3 channels
		
	WRITELONG( im->height );
	WRITELONG( im->width  );
			
	WRITESHORT( BitsPerChannel ); 			// BitsPerChannel
	switch( im->dataformat )
	{
		case _Lab:	WRITESHORT( 9 );
					break;
		case _RGB:	WRITESHORT( 3 );
					break;
		default:	WRITESHORT( 3 ); 			
	}
	WRITELONG( 0 );				// Color Mode
	WRITELONG( 0 ); 			// Image Resources
			
	writeLayerAndMask( im, fnum );

	writeWhiteBackground( im->width * BitsPerChannel/8, im->height, fnum );
		

	myclose (fnum );
	return 0;
}


// Add image as additional layer into PSD-file
int addLayerToFile( Image *im, fullPath* sfile, fullPath* dfile, stBuf *sB)
{
	file_spec	src;
	file_spec   fnum;
	Image		sim;			//  background image
	char    	header[128], *h;
	long		count, len, i, srcCount = 0, result = 0;
	unsigned char **buf;
	unsigned long var;
	char		data[12], *d;
	int			BitsPerChannel;

	TwoToOneByte( im ); // Multilayer image format doesn't support 16bit channels
	
	GetBitsPerChannel( im, BitsPerChannel );
	

	if( myopen( sfile,read_bin, src ) )
	{	
		PrintError("Error Opening Image File");
		return -1;
	}
		
	// Read psd header

	h = header;
	count = PSDHLENGTH;
	
	myread( src,count,h); srcCount += count;
	if( count != PSDHLENGTH )
	{
		PrintError("Error Reading Image File");
		myclose( src );
		return -1;
	}
	
	if( ParsePSDHeader( header, &sim ) != 0 )
	{
		PrintError("Wrong File Format");
		myclose( src );
		return -1;
	}
	
	// Check if image can be inserted
	if( sim.width != im->width || sim.height != im->height )
	{	
		PrintError("Can't add layer: Images have different size");
		return -1;
	}
	
	// Read (and ignore) Color mode data
	READLONG( len ); srcCount += (4 + len);
	count = 1;
	for( i=0; i<len; i++ )
	{
		myread(src,count,h);
	}

	// Read (and ingnore) Image resources
	READLONG( len ); srcCount += (4 + len);
	count = 1;
	for( i=0; i<len; i++ )
	{
		myread(src,count,h);
	}

	myclose( src );

	if( myopen( sfile, read_bin, src ) )
	{	
		PrintError("Error Opening Image File");
		return -1;
	}

	if( myopen( dfile, write_bin, fnum ) )
	{	
		PrintError("Error Opening Image File");
		return -1;
	}

	// Read and write Fileheader
	buf = (unsigned char**)mymalloc( srcCount );
	if( buf == NULL )
	{
		PrintError("Not enough memory");
		result = -1;
		goto _addLayerToFile_exit;
	}
	fileCopy( src, fnum, srcCount, *buf );	
	myfree( (void**)buf );

	// Add one layer	
    if( addLayer( im, src, fnum, sB ) != 0 )
	{
		result = -1;
		goto _addLayerToFile_exit;
	}

	
	writeWhiteBackground( sim.width * BitsPerChannel/8, sim.height, fnum );
	

_addLayerToFile_exit:
	myclose( src ); myclose( fnum );
	return result;
}




static int writeImageDataPlanar( Image *im, file_spec fnum )
{
	register int 			x,y,idy, bpp;
	unsigned char 			**channel;
	register unsigned char 	*ch, *idata;
	int						color;
	long					count;
	short					svar;
	char					data[12], *d;
	int						BitsPerChannel, channels;
	
	GetBitsPerChannel( im, BitsPerChannel );
	GetChannels( im,channels);
	
	bpp = im->bitsPerPixel / 8;
			


	// Write Compression info
	
	WRITESHORT( 0 ); // Raw data


	// Buffer to hold data in one channel
	
	count = im->width * im->height * BitsPerChannel / 8;
	
	channel = (unsigned char**)mymalloc( count );
	
	if( channel == NULL )
	{
		PrintError("Not Enough Memory");
		return -1;
	}

	if( BitsPerChannel ==  8 )
	{
		for( color = 0; color<3; color++)
		{
			ch = *channel; idata = &(*im->data)[color + channels - 3];
		
			for(y=0; y<im->height;y++)
			{
				idy = y * im->bytesPerLine;
				for(x=0; x<im->width;x++)
				{
					*ch++ = idata [ idy + x * bpp ];
				}
			}
	
			mywrite( fnum, count, *channel );
		}
	}
	else // 16
	{
		for( color = 0; color<3; color++)
		{
			ch = *channel; idata = &(*im->data)[2*(color + channels - 3)];
		
			for(y=0; y<im->height;y++)
			{
				idy = y * im->bytesPerLine;
				for(x=0; x<im->width;x++)
				{
					*ch++ = idata [ idy + x * bpp ];
					*ch++ = idata [ idy + x * bpp + 1];
				}
			}
	
			mywrite( fnum, count, *channel );
		}
	}
	
		

	if( im->bitsPerPixel == 32 )
	{
		// Write 1byte alpha channel
		
		ch = *channel; idata = &(*im->data)[0];
		
		for(y=0; y<im->height;y++)
		{
			idy = y * im->bytesPerLine;
			for(x=0; x<im->width;x++)
				{
					*ch++ = idata [ idy + x * bpp ];
				}
		}
	
		mywrite( fnum, count, *channel );
	}
	else if( im->bitsPerPixel == 64 )
	{
		// Write 2byte alpha channel
		
		ch = *channel; idata = &(*im->data)[0];
		
		for(y=0; y<im->height;y++)
		{
			idy = y * im->bytesPerLine;
			for(x=0; x<im->width;x++)
				{
					*ch++ = idata [ idy + x * bpp ];
					*ch++ = idata [ idy + x * bpp + 1];
				}
		}
	
		mywrite( fnum, count, *channel );
	}
	
	myfree( (void**)channel );
	return 0;
}


// Write white background, RLE-compressed

static void writeWhiteBackground( int width, int height, file_spec fnum )
{
	short 			svar;
	long 			count,  w8, w;
	char 			data[12], *d, scanline[256];
	
	int numChannels = 3, i, bytecount, dim = height*numChannels;
	
	WRITESHORT( 1 );  	// RLE compressed

	w8 = width;
	
	d = scanline;
	// Set up scanline
	for(w=w8; w>128; w-=128)
	{
		*d++ = -127; *d++ = (char)255;
	}

	switch(w)
	{
		case 0: break;
		case 1: *d++=0; 		*d++ = (char)255;
				break;
		default: *d++=1-w; 		*d++ = (char)255;
				break;
	}
	
	bytecount = d - scanline;
	
	// Scanline counts (rows*channels)
	for(i=0; i < dim; i++)
	{
		WRITESHORT( bytecount );
	}

	// RLE compressed data
	count = bytecount;
	for(i=0; i < dim; i++)
	{
		mywrite( fnum, count, scanline );
	}
	
}

// image is allocated, but not image data
// mode = 0: only load image struct
// mode = 1: also allocate and load data

int readPSD(Image *im, fullPath *sfile, int mode)
{
	file_spec	src;
	char    header[128], *h;
	long	count, len, i;
	unsigned long var;
	char	data[12], *d;
	
	
	if( myopen( sfile, read_bin, src ) )
	{	
		PrintError("Error Opening Image File");
		return -1;
	}
		
	// Read psd header

	h = header;
	count = PSDHLENGTH;
	
	myread( src,count,h);
	if( count != PSDHLENGTH )
	{
		PrintError("Error Reading Image File");
		myclose( src );
		return -1;
	}
	
	if( ParsePSDHeader( header, im ) != 0 )
	{
		PrintError("Wrong File Format");
		myclose( src );
		return -1;
	}

	if( mode == 0 )
	{
		myclose( src );
		return 0;
	}
	
	im->data = (unsigned char**) mymalloc( im->dataSize );
	if( im->data == NULL )
	{
		PrintError("Not enough memory to read image");
		myclose( src );
		return -1;
	}
	// Read (and ingnore) Color mode data
	READLONG( len );
	count = 1;
	for( i=0; i<len; i++ )
		myread(src,count,h);

	// Read (and ingnore) Image resources
	READLONG( len );
	count = 1;
	for( i=0; i<len; i++ )
		myread(src,count,h);
		
	// Read (and ingnore) Layer mask info
	READLONG( len );
	count = 1;
	for( i=0; i<len; i++ )
		myread(src,count,h);
		
	
	if( readImageDataPlanar( im, src ) != 0 )
	{
		PrintError("Error reading image");
		myclose( src );
		return -1;
	}
	
	myclose (src );
	return 0;
}

static int ParsePSDHeader( char *header, Image *im )
{
	register char *h = header;
	short s;
	int channels;
	
	if( *h++ != '8' || *h++ != 'B' || *h++ != 'P' || *h++ != 'S' ||
		*h++ != 0   || *h++ != 1   ||
		*h++ != 0	|| *h++ != 0   || *h++ != 0	  || *h++ != 0	 || *h++ != 0	|| *h++ != 0 )
		return -1;
	
	NUMBERSHORT( s, h );
	
	channels = s;
	
	if( channels < 3 ) //!= 3 && channels != 4 )
	{
		PrintError( "Number of channels must be 3 or larger" );
		return -1;
	}
	if( channels > 4 ) channels = 4;
	
	NUMBERLONG( im->height, h );
	NUMBERLONG( im->width,  h );
	
	
	NUMBERSHORT( s, h );
	

	if( s!= 8 && s!= 16)
	{
		PrintError( "Depth must be 8 or 16 Bits per Channel" );
		return -1;
	}

	im->bitsPerPixel = s * channels;
	
	NUMBERSHORT( s, h );
	
	switch( s )
	{
		case 3: 	im->dataformat = _RGB; break;
		case 9: 	im->dataformat = _Lab; break;
		default:	PrintError( "Color mode must be RGB or Lab" );return -1;
	}
	
	im->bytesPerLine = im->width * im->bitsPerPixel/8;
	im->dataSize = im->height * im->bytesPerLine;
	
	return 0;
}


static int readImageDataPlanar(Image *im, file_spec src ) 
{
	register int 			x,y,idy, bpp;
	unsigned char 			**channel = NULL;
	register unsigned char 	*h, *idata;
	int						result = 0, i, chnum,BitsPerChannel, channels;
	long					count;
	short					svar;
	char 					data[12], *d ;
	
	GetBitsPerChannel( im, BitsPerChannel );
	GetChannels( im, channels );
	bpp = im->bitsPerPixel / 8;




	// Read Compression info
	

	READSHORT( svar );	
	if( svar!= 0 )
	{
		PrintError("Image data must not be compressed");
		return -1;
	}
	
	// Allocate memory for one channel
	
	count = im->width * im->height * BitsPerChannel/8 ;
	channel = (unsigned char**)mymalloc( count );
	
	if( channel == NULL )
	{
		PrintError("Not Enough Memory");
		return -1;
	}

	
	for(i = 0; i < channels; i++)		// Read each channel
	{
		chnum = i + channels - 3; 
		if(chnum == 4) chnum = 0;	// Order: r g b (alpha)
		

		myread(src,count,*channel);
		if( count != im->width * im->height * BitsPerChannel/8)
		{ 
			PrintError("Error Reading Image Data");
			result = -1;
			goto readImageDataPlanar_exit;
		}
		
		h = *channel; 
		
		if( BitsPerChannel == 8 )
		{
			idata = &(*im->data)[chnum];
			for(y=0; y<im->height;y++)
			{
				idy = y * im->bytesPerLine;
				for(x=0; x<im->width;x++)
				{
					idata [ idy + x * bpp ] = *h++;
				}
			}
		}
		else // 16
		{
			idata = &(*im->data)[chnum*2];
			for(y=0; y<im->height;y++)
			{
				idy = y * im->bytesPerLine;
				for(x=0; x<im->width;x++)
				{
					idata [ idy + x * bpp ] 	= *h++;
					idata [ idy + x * bpp + 1] 	= *h++;
				}
			}
		}
	}
	
readImageDataPlanar_exit:
	if( channel != NULL )
		myfree( (void**)channel );
	return result;
}





// Write image as separate first layer

static int writeLayerAndMask( Image *im, file_spec fnum )
{
	unsigned long 	var;
	PTRect 			theRect;
	int 			channelLength;
	long 			count;
	short 			svar;
	char 			data[12], *d;
	unsigned char 	ch;
	int 			i, chnum, lenLayerInfo, numLayers,BitsPerChannel,channels;
	int				oddSized = 0;
	

	GetBitsPerChannel( im, BitsPerChannel );
	GetChannels( im, channels );

	getImageRectangle( im, &theRect );

	numLayers = 1;
	
	channelLength = (theRect.right-theRect.left) * (theRect.bottom-theRect.top) * BitsPerChannel/8  + 2;	
	
	
	lenLayerInfo = 2 + numLayers * (4*4 + 2 + channels * 6 + 4 + 4 + 4 * 1 + 4 + 12 + channels * channelLength);
		
	
	WRITELONG( lenLayerInfo + 4 + 4 );
	// Layer info. See table 2�13.

	if( lenLayerInfo/2 != (lenLayerInfo+1)/2 ) // odd
	{
		lenLayerInfo += 1; oddSized = 1;
	}
	
	// Length of the layers info section, rounded up to a multiple of 2.
	WRITELONG( lenLayerInfo );
	
	// 2 bytes Count Number of layers. If <0, then number of layers is absolute value,
	// and the first alpha channel contains the transparency data for
	// the merged result.
	WRITESHORT( numLayers );
	
	// ********** Layer Structure ********************************* //	


	WRITELONG( theRect.top );					// Layer top 
	WRITELONG( theRect.left );					// Layer left
	WRITELONG( theRect.bottom ) ;				// Layer bottom 
	WRITELONG( theRect.right  ) ;				// Layer right 

	WRITESHORT( channels ); // The number of channels in the layer.

	// ********** Channel information ***************************** //
		
	WRITESHORT( 0 );			// red
	WRITELONG( channelLength ) 	// Length of following channel data.
	WRITESHORT( 1 );			// green
	WRITELONG( channelLength ) 	// Length of following channel data.
	WRITESHORT( 2 );			// blue
	WRITELONG( channelLength ) 	// Length of following channel data.
	if( channels == 4 ) // alpha channel
	{
		WRITESHORT( -1 ); //-2 );		// transparency mask
		WRITELONG( channelLength ) 	// Length of following channel data.
	}

	// ********** End Channel information ***************************** //
	
	WRITELONG( '8BIM'); // Blend mode signature Always 8BIM.
	WRITELONG( 'norm'); // Blend mode key

	WRITEUCHAR(255); // 1 byte Opacity 0 = transparent ... 255 = opaque
	WRITEUCHAR( 0 ); // 1 byte Clipping 0 = base, 1 = non�base
	WRITEUCHAR( 1 ); // 1 byte Flags bit 0 = transparency protected bit 1 = visible
	WRITEUCHAR( 0 ); // 1 byte (filler) (zero)

	WRITELONG( 12);		// Extra data size Length of the extra data field. 
	WRITELONG( 0 );		// Layer Mask data
	WRITELONG( 0 );		// Layer blending ranges

	WRITEUCHAR( 3 ); WRITEUCHAR( 'L' );WRITEUCHAR( '0' );WRITEUCHAR( '1' );// Layer name
	
	
	// ************* End Layer Structure ******************************* //

	// ************* Write Channel Image data ************************** //

	for(i = 0; i < channels; i++)
	{
		chnum = i + channels - 3;
		if(chnum == 4) chnum = 0; // Order: r g b (alpha)
		if( writeChannelData( im, fnum, chnum, &theRect ) ) 
			return -1;
	}
	if( oddSized )			// pad byte
	{
		WRITEUCHAR( 0 );
	}

	// ************* End Write Channel Image data ************************** //
	
	// ************* Global layer mask info ******************************** //
	
	WRITELONG(  0 );		// Length of global layer mask info section.
	
	// WRITESHORT( 0 );		// 2 bytes Overlay color space
	// WRITESHORT( 0 );		// 4 * 2 byte color components
	// WRITESHORT( 0 );		
	// WRITESHORT( 0 );		
	// WRITESHORT( 0 );
	// WRITESHORT( 0 );		// 2 bytes Opacity 0 = transparent, 100 = opaque.
	// WRITEUCHAR( 128 );		// 1 byte Kind 0=Color selected�i.e. inverted; 
							// 1=Color protected;128=use value stored per layer.
							//  This value is preferred. The others are for back-ward
							// compatibility with beta versions.
	// WRITEUCHAR( 0 );

	return 0;

}




static int writeChannelData( Image *im, file_spec fnum, int channel, PTRect *theRect )
{
	register int 			x,y,idy, bpp,BitsPerChannel,channels;
	unsigned char 			**ch;
	register unsigned char 	*c, *idata;
	long					count;
	short					svar;
	char					data[12], *d;

	GetBitsPerChannel( im, BitsPerChannel );
	GetChannels( im, channels );
	
	// Write Compression info

	WRITESHORT( 0 );	 // Raw data

	bpp = im->bitsPerPixel/8;

	count = (theRect->right - theRect->left) * (theRect->bottom - theRect->top) * BitsPerChannel/8;
	
	ch = (unsigned char**)mymalloc( count );
	
	if( ch == NULL )
	{
		PrintError("Not Enough Memory");
		return -1;
	}

	c = *ch; idata = &((*(im->data))[channel*BitsPerChannel/8]);
	
	if(BitsPerChannel == 8)
	{
		for(y=theRect->top; y<theRect->bottom;y++)
		{
			idy = y * im->bytesPerLine;
			for(x=theRect->left; x<theRect->right;x++)
			{
				*c++ = idata [ idy + x * bpp ];
			}
		}
	}
	else // 16
	{
		for(y=theRect->top; y<theRect->bottom;y++)
		{
			idy = y * im->bytesPerLine;
			for(x=theRect->left; x<theRect->right;x++)
			{
				*c++ = idata [ idy + x * bpp ];
				*c++ = idata [ idy + x * bpp + 1 ];
			}
		}
	}	
	mywrite( fnum, count, *ch );
	
		
	myfree( (void**)ch );
	return 0;
}


// Return the smallest rectangle enclosing the image; 
// Use alpha channel and rgb data 
static void getImageRectangle( Image *im, PTRect *theRect )
{
	register unsigned char *alpha, *data;
	register int cx,x,y,cy,bpp,channels;
	int BitsPerChannel;

	GetChannels( im, channels );
	GetBitsPerChannel( im, BitsPerChannel );
	bpp = im->bitsPerPixel/8;


	theRect->top 			= 0;
	theRect->left 			= 0;
	theRect->bottom 		= im->height;
	theRect->right 			= im->width;

	if( channels == 4 ){ //  alpha channel present

		if( BitsPerChannel == 8 ){
			for(y=0; y<im->height; y++){
				for(x=0, alpha = *(im->data) + y * im->bytesPerLine; 
				    x<im->width; 
				    x++, alpha += 4){
					if( * (int*) alpha ){
						theRect->top = y;
						goto _get_bottom;
					}
				}
			}
		}else{ // 16
			for(y=0; y<im->height; y++){
				for(x=0, alpha = *(im->data) + y * im->bytesPerLine; 
				    x<im->width; 
				    x++, alpha += 8){
					if( * (int*) alpha || * (int*) (alpha+4) ){
						theRect->top = y;
						goto _get_bottom;
					}
				}
			}
		}
		

	_get_bottom:
		if( BitsPerChannel == 8 ){
			for(y=im->height-1; y>=0; y--){
				for(x=0, alpha = *(im->data) + y * im->bytesPerLine; 
				    x<im->width; 
				    x++, alpha += 4){
					if( * (int*) alpha ){
						theRect->bottom = y+1;
						goto _get_left;
					}
				}
			}
		}else{ // 16
			for(y=im->height-1; y>=0; y--){
				for(x=0, alpha = *(im->data) + y * im->bytesPerLine; 
				    x<im->width; 
				    x++, alpha += 8){
					if( * (int*) alpha || * (int*) (alpha+4) ){
						theRect->bottom = y+1;
						goto _get_left;
					}
				}
			}
		}
		
	_get_left:
		if( BitsPerChannel == 8 ){
			for(x=0; x<im->width; x++){
				for(y=0, alpha = *(im->data) + x*bpp; 
				    y<im->height; 
				    y++, alpha += im->bytesPerLine){
					if( * (int*) alpha ){
						theRect->left = x;
						goto _get_right;
					}
				}
			}
		}else{ // 16
			for(x=0; x<im->width; x++){
				for(y=0, alpha = *(im->data) + x*bpp; 
				    y<im->height; 
				    y++, alpha += im->bytesPerLine){
					if( * (int*) alpha || * (int*) (alpha+4) ){
						theRect->left = x;
						goto _get_right;
					}
				}
			}
		}	
	_get_right:
		if( BitsPerChannel == 8 ){
			for(x=im->width-1; x>=0; x--){
				for(y=0, alpha = *(im->data) + x*bpp; 
				    y<im->height; 
				    y++, alpha += im->bytesPerLine){
					if( * (int*) alpha ){
						theRect->right = x + 1;
						goto _get_exit;
					}
				}
			}
		}else{ // 16
			for(x=im->width-1; x>=0; x--){
				for(y=0, alpha = *(im->data) + x*bpp; 
				    y<im->height; 
				    y++, alpha += im->bytesPerLine){
					if( * (int*) alpha || * (int*) (alpha+4) ){
						theRect->right = x + 1;
						goto _get_exit;
					}
				}
			}
		}
	}
	else //  no alpha channel: Use non black rectangle
	{
		alpha 		= *(im->data);

		if( BitsPerChannel == 8 )
		{
			for(y=0; y<im->height; y++)
			{
				cy = y * im->bytesPerLine;
				for(x=0; x<im->width; x++)
				{
					data = alpha + cy + bpp*x;
					if( *data || *(data+1) || *(data+2) )
					{
						theRect->top = y;
						goto _get_bottom_noalpha;
					}
				}
			}
		}
		else // 16
		{
			for(y=0; y<im->height; y++)
			{
				cy = y * im->bytesPerLine;
				for(x=0; x<im->width; x++)
				{
					data = alpha + cy + bpp*x;
					if( *((USHORT*)data) || *(((USHORT*)data)+1) || *(((USHORT*)data)+2) )
					{
						theRect->top = y;
						goto _get_bottom_noalpha;
					}
				}
			}
		}

	_get_bottom_noalpha:
		if( BitsPerChannel == 8 )
		{
			for(y=im->height-1; y>=0; y--)
			{
				cy = y * im->bytesPerLine;
				for(x=0; x<im->width; x++)
				{
					data = alpha + cy + bpp*x;
					if( *data || *(data+1) || *(data+2) )
					{
						theRect->bottom = y+1;
						goto _get_left_noalpha;
					}
				}
			}
		}
		else // 16
		{
			for(y=im->height-1; y>=0; y--)
			{
				cy = y * im->bytesPerLine;
				for(x=0; x<im->width; x++)
				{
					data = alpha + cy + bpp*x;
					if( *((USHORT*)data) || *(((USHORT*)data)+1) || *(((USHORT*)data)+2) )
					{
						theRect->bottom = y+1;
						goto _get_left_noalpha;
					}
				}
			}
		}
		
	_get_left_noalpha:
		if( BitsPerChannel == 8 )
		{
			for(x=0; x<im->width; x++)
			{
				cx = bpp * x;
				for(y=0; y<im->height; y++)
				{
					data = alpha + y * im->bytesPerLine + cx;
					if( *data || *(data+1) || *(data+2) )
					{
						theRect->left = x;
						goto _get_right_noalpha;
					}
				}
			}
		}
		else // 16
		{
			for(x=0; x<im->width; x++)
			{
				cx = bpp * x;
				for(y=0; y<im->height; y++)
				{
					data = alpha + y * im->bytesPerLine + cx;
					if( *((USHORT*)data) || *(((USHORT*)data)+1) || *(((USHORT*)data)+2) )
					{
						theRect->left = x;
						goto _get_right_noalpha;
					}
				}
			}
		}
	
	_get_right_noalpha:
		if( BitsPerChannel == 8 )
		{
			for(x=im->width-1; x>=0; x--)
			{
				cx = bpp * x;
				for(y=0; y<im->height; y++)
				{
					data = alpha + y * im->bytesPerLine + cx;
					if( *data || *(data+1) || *(data+2) )
					{
						theRect->right = x + 1;
						goto _get_exit;
					}
				}
			}
		}
		else // 16
		{
			for(x=im->width-1; x>=0; x--)
			{
				cx = bpp * x;
				for(y=0; y<im->height; y++)
				{
					data = alpha + y * im->bytesPerLine + cx;
					if( *((USHORT*)data) || *(((USHORT*)data)+1) || *(((USHORT*)data)+2) )
					{
						theRect->right = x + 1;
						goto _get_exit;
					}
				}
			}
		}
	}
	_get_exit: ;
}		



									
// Image is added to layer structure of file.
// There must be one valid layer structure, and the
// filepointer is at the beginning of it in both src and dest

static int addLayer( Image *im, file_spec src, file_spec fnum, stBuf *sB )
{
	unsigned long 	var;
	PTRect			theRect, *nRect = NULL;
	int 			channelLength,  bpp, oddSized = 0;
	int				result = 0;
	long 			count;
	short 			svar;
	char 			data[12], *d;
	unsigned char 	ch;
	int 			i, k, lenLayerInfo, numLayers,channels,BitsPerChannel;
	int				*chlength = NULL, *nchannel = NULL, 
					*cnames = NULL, *nodd = NULL;
	unsigned char	**alpha = NULL, **buf = NULL;

	GetChannels( im, channels );
	GetBitsPerChannel( im, BitsPerChannel );
	bpp = im->bitsPerPixel/8;
	
	if( channels == 4 ) // Alpha channel present
	{
		if( sB->seam == _middle )	// we have to stitch
			alpha = (unsigned char**) mymalloc( im->width * im->height * BitsPerChannel/8);
	}

	if( alpha != NULL )
	{
		memset( *alpha, 0 , im->width * im->height * BitsPerChannel/8);
	}

	getImageRectangle( im, &theRect );
	channelLength = (theRect.right-theRect.left) * (theRect.bottom-theRect.top)  + 2;
	
	
	// Read layerinfo up to channeldata
	READLONG( var );			// Length of the miscellaneous information section (ignored)
	READLONG( var );			// Length of the layers info section, rounded up to a multiple of 2(ignored)
	READSHORT( numLayers );		// Number of layers

	chlength = (int*) malloc( numLayers * sizeof( int ));
	nchannel = (int*) malloc( numLayers * sizeof( int ));
	cnames 	 = (int*) malloc( numLayers * sizeof( int ));
	nodd 	 = (int*) malloc( numLayers * sizeof( int ));
	nRect	 = (PTRect*) malloc( numLayers * sizeof( PTRect ));
	if( chlength == NULL || nchannel== NULL || nRect == NULL ||
		cnames == NULL || nodd == NULL)
	{
		PrintError("Not enough memory 1");
		result = -1;
		goto _addLayer_exit;
	}
	lenLayerInfo = 2;
	for(i=0; i<numLayers; i++) 
	{
		READLONG( nRect[i].top   )	; 			// Layer top 
		READLONG( nRect[i].left  )	; 			// Layer left
		READLONG( nRect[i].bottom) 	; 			// Layer bottom 
		READLONG( nRect[i].right ) 	; 			// Layer right 

		READSHORT( nchannel[i] ); 			// The number of channels in the layer.

		// ********** Channel information ***************************** //
		
		READSHORT( svar ); 					// red
		READLONG(chlength[i]); 				// Length of following channel data.
		READSHORT( svar ); 					// green
		READLONG(chlength[i]); 				// Length of following channel data.
		READSHORT( svar ); 					// blue
		READLONG(chlength[i]); 				// Length of following channel data.
		if( nchannel[i] > 3 ) // 1st alpha channel
		{
			READSHORT( svar ); 				// transparency mask
			READLONG( chlength[i]); 		// Length of following channel data.
		}
		if( nchannel[i] > 4 ) // 2nd alpha channel
		{
			READSHORT( svar ); 				// transparency mask
			READLONG( chlength[i]); 		// Length of following channel data.
		}
		
		// ********** End Channel information ***************************** //
	
		READLONG( var ); 					 // Blend mode signature Always 8BIM.
		READLONG( var ); 					 // Blend mode key

		READLONG( var ); 					// Four flag bytes

		READLONG( var ); 					// Extra data size Length of the extra data field. This is the
		READLONG( var ); 					// Layer Mask data
		READLONG( var ); 					// Layer blending ranges

		READLONG( cnames[i] );				// Layer name
		
		var = 4*4 + 2 + nchannel[i] * 6 + 4 + 4 + 4 * 1 + 4 + 12 + nchannel[i] * chlength[i]; // length
		if( var/2 != (var+1)/2 ) // odd
		{
			nodd[i] = 1; var++;
			//READUCHAR( ch );
		}
		else
		{
			nodd[i] = 0;
		}
		lenLayerInfo += var;
	}
	
	// length of new channel
	
	if(0)//alpha != NULL)
		var = 4*4 + 2 + (channels+1) * 6 + 4 + 4 + 4 * 1 + 4 + 12 + (channels+1) * channelLength;
	else
		var = 4*4 + 2 + channels * 6 + 4 + 4 + 4 * 1 + 4 + 12 + channels * channelLength;

	if( var/2 != (var+1)/2 ) // odd
	{
		oddSized = 1; var++;
		    // PrintError("odd");
	}
	lenLayerInfo += var;
		
	//printf("Writing Length of Layerinfo: %d\n", lenLayerInfo);
	if( lenLayerInfo/2 != (lenLayerInfo+1)/2 ) // odd, should never happen
	{
		//PrintError("odd");
		lenLayerInfo += 1;
	}
	
	WRITELONG( lenLayerInfo + 4 + 4 );
	// Length of the layers info section, rounded up to a multiple of 2.
	WRITELONG( lenLayerInfo ); ;
	//printf("Corrected to: %d\n", lenLayerInfo);
	
	// 2 bytes Count Number of layers. If <0, then number of layers is absolute value,
	// and the first alpha channel contains the transparency data for
	// the merged result.
	WRITESHORT( numLayers + 1); 
	// Write previous layers, read channel data	
	for(i=0; i<numLayers; i++) 
	{
		WRITELONG( nRect[i].top    );			// Layer top 
		WRITELONG( nRect[i].left   );			// Layer left
		WRITELONG( nRect[i].bottom );			// Layer bottom 
		WRITELONG( nRect[i].right  );			// Layer right 

		WRITESHORT( nchannel[i] ); 		// The number of channels in the layer.

		// ********** Channel information ***************************** //
		
		WRITESHORT( 0 );					// red
		WRITELONG( chlength[i] );			// Length of following channel data.
		WRITESHORT( 1 );					// green
		WRITELONG( chlength[i] );			// Length of following channel data.
		WRITESHORT( 2 );					// blue
		WRITELONG( chlength[i] );			// Length of following channel data.
		if( nchannel[i] > 3 ) // alpha channel
		{
			WRITESHORT( -2); 				// transparency mask
			WRITELONG( chlength[i] ); 		// Length of following channel data.
		}
		// ********** End Channel information ***************************** //
	
		WRITELONG( '8BIM'); // Blend mode signature Always 8BIM.
		WRITELONG( 'norm'); // Blend mode key

		WRITEUCHAR(255); // 1 byte Opacity 0 = transparent ... 255 = opaque
		WRITEUCHAR( 0 ); // 1 byte Clipping 0 = base, 1 = non�base
		WRITEUCHAR( 1 ); // 1 byte Flags bit 0 = transparency protected bit 1 = visible
		WRITEUCHAR( 0 ); // 1 byte (filler) (zero)

		WRITELONG( 12);		// Extra data size Length of the extra data field. This is the
		WRITELONG( 0 );		// Layer Mask data
		WRITELONG( 0 );		// Layer blending ranges

		WRITELONG( cnames[i] );		// Layer name
		
		//printf("Writing layer %d: top %d, left %d, bottom %d, right %d, size %d\n", i, 
		//nRect[i].top, nRect[i].left, nRect[i].bottom, nRect[i].right, chlength[i]);
	}
	// ************** The New Layer ************************************ //
	// ********** Layer Structure ********************************* //	

		//printf("Adding layer : top %d, left %d, bottom %d, right %d, size %d\n",  
		//theRect.top, theRect.left, theRect.bottom, theRect.right, channelLength);


	WRITELONG( theRect.top );					// Layer top 
	WRITELONG( theRect.left );					// Layer left
	WRITELONG( theRect.bottom ) ;				// Layer bottom 
	WRITELONG( theRect.right  ) ;				// Layer right 

	if( alpha!= NULL )
	{
		WRITESHORT( channels);//+1 ); // The number of channels in the layer.
	}
	else
	{
		WRITESHORT( channels ); // The number of channels in the layer.
	}	

	// ********** Channel information ***************************** //
		
	WRITESHORT( 0 );			// red
	WRITELONG( channelLength ) 	// Length of following channel data.
	WRITESHORT( 1 );			// green
	WRITELONG( channelLength ) 	// Length of following channel data.
	WRITESHORT( 2 );			// blue
	WRITELONG( channelLength ) 	// Length of following channel data.
	if( bpp == 4 ) // alpha channel
	{
		WRITESHORT( -2 ); //2 );		// transparency mask
		WRITELONG( channelLength ) 	// Length of following channel data.
	}

	// ********** End Channel information ***************************** //
	
	WRITELONG( '8BIM'); // Blend mode signature Always 8BIM.
	WRITELONG( 'norm'); // Blend mode key

	WRITEUCHAR(255); // 1 byte Opacity 0 = transparent ... 255 = opaque
	WRITEUCHAR( 0 ); // 1 byte Clipping 0 = base, 1 = non�base
	WRITEUCHAR( 1 ); // 1 byte Flags bit 0 = transparency protected bit 1 = visible
	WRITEUCHAR( 0 ); // 1 byte (filler) (zero)

	WRITELONG( 12);		// Extra data size Length of the extra data field. This is the
	WRITELONG( 0 );		// Layer Mask data
	WRITELONG( 0 );		// Layer blending ranges
	
	sprintf(&(data[1]), "L%02d", numLayers+1 ); data[0] = 3;
	count = 4; 
	mywrite( fnum, count, data ); // Layer Name
	
	
	// ************* End Layer Structure ******************************* //

	// ************* Write Channel Image data ************************** //

	for( i=0; i< numLayers; i++)
	{
		buf = (unsigned char**) mymalloc( chlength[i] );
		if( buf == NULL )
		{
			PrintError("Not enough memory, %d bytes needed", (int)chlength[i]);
			result = -1;
			goto _addLayer_exit;
		}
		for( k=0; k< nchannel[i]; k++ )
		{
			fileCopy( src, fnum, chlength[i], *buf );
			//printf("Compression Layer %d Channel %d: %d\n", i,k,(int)*((short*)*buf));
			
			if( k == 3)	// found an alpha channel
			{
				if( alpha!= NULL )
				{
					orAlpha( *alpha, &((*buf)[2]), im, &(nRect[i]) );
				}
			}
		}
		myfree( (void**)buf );
		if( nodd[i] )	// pad byte
		{
			READUCHAR( ch );
			WRITEUCHAR( 0 );
		}
	}
	
	// Write color channels
	for( i=0; i<3; i++)
	{
		if( writeChannelData( im, fnum, i + channels - 3, &theRect ) ) 
		{
			result = -1;
			goto _addLayer_exit;
		}
	}
	
	if( channels > 3 ) 	// Alpha channel present
	{		
#if 0
		if( writeChannelData( im, fnum, 0, &theRect ) ) 
		{
			result = -1;
			goto _addLayer_exit;
		}
#endif
		if( sB->seam == _middle && alpha != NULL ) // Create stitching mask
		{
			mergeAlpha( im, *alpha, sB->feather, &theRect );
#if 0
			{
				static nim = 0;
				fullPath fp;
				makePathToHost( &fp );
				sprintf( (char*)fp.name, "Test.%d", nim++);
				c2pstr((char*)fp.name);
				mycreate( &fp, '8BIM', '8BPS' );
				 writePSD(im, &fp );
			}
#endif
		}
		if( writeChannelData( im, fnum, 0, &theRect ) ) 
		{
			result = -1;
			goto _addLayer_exit;
		}
	}
	if( oddSized )	// pad byte
	{
		WRITEUCHAR( 0 );
	}
		
	// ************* End Write Channel Image data ************************** //
	
	// ************* Global layer mask info ******************************** //
	
	READLONG( var ); 	WRITELONG(  0 );		// Length of global layer mask info section.
	//READSHORT( svar );	WRITESHORT( 0 );		// 2 bytes Overlay color space
	//READSHORT( svar );	WRITESHORT( 0 );		// 4 * 2 byte color components
	//READSHORT( svar );	WRITESHORT( 0 );		
	//READSHORT( svar );	WRITESHORT( 0 );		
	//READSHORT( svar );	WRITESHORT( 0 );
	//READSHORT( svar );	WRITESHORT( 0 );		// 2 bytes Opacity 0 = transparent, 100 = opaque.
	//READSHORT( svar );	WRITEUCHAR( 128 );		// 1 byte Kind 0=Color selected�i.e. inverted; 
												// 1=Color protected;128=use value stored per layer.
												//  This value is preferred. The others are for back-ward
												// compatibility with beta versions.
						//WRITEUCHAR( 0 );
	
	
_addLayer_exit:	
	if( alpha 		!= NULL ) 	myfree( (void**)alpha );
	if( chlength 	!= NULL ) 	free(chlength); 
	if( nchannel	!= NULL )	free(nchannel);
	if( nRect		!= NULL )	free(nRect);
	if( cnames		!= NULL )	free(cnames);
	if( nodd		!= NULL )	free(nodd);

	return result;
}


static int fileCopy( file_spec src, file_spec dest, int numBytes, unsigned char *buf )
{
	long count = numBytes;
	
	myread(  src, count, buf );
	mywrite( dest, count, buf );
	return 0;
}


// Or two alpha channels: one in alpha (same size as image im)
// one in buf comprising the rectangle top,bottom,left,right
// store result in alpha

static void orAlpha( unsigned char* alpha, unsigned char *buf, Image *im, PTRect *theRect )
{
	register int x,y,ay,by,w;
	int BitsPerChannel;
	
	GetBitsPerChannel( im, BitsPerChannel );

	if( im->bitsPerPixel != 32 && im->bitsPerPixel != 64) return;
	
	w = (theRect->right - theRect->left);
	
	if( BitsPerChannel == 8 )
	{
		for(y=theRect->top; y<theRect->bottom; y++)
		{
			ay = y * im->width;
			by = (y - theRect->top) * w;
			{
				for(x=theRect->left; x<theRect->right; x++)
				{
					if( buf[ by + x - theRect->left ] )
						alpha[ ay + x ] = 255U;
				}
			}
		}
	}
	else // 16
	{
		for(y=theRect->top; y<theRect->bottom; y++)
		{
			ay = y * im->width * 2;
			by = (y - theRect->top) * w * 2;
			{
				for(x=theRect->left; x<theRect->right; x++)
				{
					if( *((USHORT*)(buf+ by + 2*(x - theRect->left))) )
						*((USHORT*)(alpha + ay + 2*x) ) = 65535U;
				}
			}
		}
	}
}



void FindScript( aPrefs *thePrefs )
{
	FindFile( &(thePrefs->scriptFile) );
}

int LoadOptions( cPrefs * thePrefs )
{	
	fullPath path;
	file_spec fnum;
	struct correct_Prefs loadPrefs;
	long	count;
	int	result;
	
	if( FindFile( &path ) )
	{
		return -1;
	}
	
	if( myopen( &path, read_bin, fnum ))
	{
		PrintError("Could not open file");
		return -1;
	}
	
	count = sizeof( struct correct_Prefs);
	myread(  fnum, count, &loadPrefs );

	if( count != sizeof( struct correct_Prefs) || loadPrefs.magic != 20 )
	{
		PrintError( "Wrong format!");
		result = -1;
	}
	else
	{
		memcpy((char*) thePrefs, (char*)&loadPrefs, sizeof(struct correct_Prefs));
		result = 0;
	}
	myclose(fnum);

	return result;
}


char* LoadScript( fullPath* scriptFile )
{
	fullPath	sfile;
	int 		result = FALSE, i;
	file_spec 	fnum;
	long		count;
	char		*script = NULL, ch;
	
	memset( &sfile, 0, sizeof( fullPath ) );
	if( memcmp( scriptFile, &sfile, sizeof( fullPath ) ) == 0 )
	{
		PrintError("No Scriptfile selected");
		goto _loadError;
	}

	if( myopen( scriptFile, read_text, fnum ))
	{
		PrintError("Error Opening Scriptfile");
		goto _loadError;
	}
	
	count = 1; i=0;	// Get file length
	
	while( count == 1 )
	{
		myread(  fnum, count, &ch ); 
		if(count==1) i++;
	}
	myclose(fnum);

	count = i;
	script = (char*)malloc( count+1 );
	if( script == NULL )
	{
		PrintError("Not enough memory to load scriptfile");
		goto _loadError;
	}
	if( myopen( scriptFile, read_text, fnum ))
	{
		PrintError("Error Opening Scriptfile");
		goto _loadError;
	}
	
	
	myread(fnum,count,script);
	script[count] = 0;
	myclose(fnum);
	return script;
	
_loadError:
	return (char*)NULL;
}


int WriteScript( char* res, fullPath* scriptFile, int launch )
{
	fullPath	sfile;
	int 		result = FALSE;
	file_spec 	fnum;
	long		count;

	memset( &sfile, 0, sizeof( fullPath ) );
	if( memcmp( scriptFile, &sfile, sizeof( fullPath ) ) == 0 )
	{
		PrintError("No Scriptfile selected");
		goto _writeError;
	}

	memcpy(  &sfile, scriptFile, sizeof (fullPath) );
	mydelete( &sfile );
	mycreate(&sfile,'ttxt','TEXT');

	if( myopen( &sfile, write_text, fnum ) )
	{
		PrintError("Error Opening Scriptfile");
		goto _writeError;
	}
	
	count = strlen( res );
	mywrite( fnum, count, res );	
	myclose (fnum );

	if( launch == 1 )
	{
		showScript( &sfile);
	}
	return 0;
	

_writeError:
	return -1;
}


void SaveOptions( cPrefs * thePrefs )
{
	fullPath	path;
	file_spec	fspec;
	long		count;
	
	memset( &path, 0, sizeof( fullPath ));
	if( SaveFileAs( &path, "Save Settings as..", "Params" ) )
		return;
		
	mycreate	(&path,'GKON','TEXT');
	
	if( myopen( &path, write_bin, fspec ) )
		return;
	
	count = sizeof( cPrefs );
	mywrite( fspec, count, thePrefs );
	myclose( fspec );
}



// Temporarily save a 32bit image (including Alpha-channel) using name fname

int	SaveBufImage( Image *image, char *fname )
{
	
	fullPath		fspec;

	MakeTempName( &fspec, fname );


	mydelete( &fspec );
	mycreate( &fspec, '8BIM', '8BPS');
	
	return writePSD(image,  &fspec);
}

// Load a saved 32bit image (including Alpha-channel) using name fname
// image must be allocated (not data)
// mode = 0: only load image struct
// mode = 1: also allocate and load data
	
	
int	LoadBufImage( Image *image, char *fname, int mode)
{
	fullPath	fspec;

	MakeTempName( &fspec, fname );
	return readPSD(image, &fspec,  mode);
}


// Read Photoshop Multilayer-image
int readPSDMultiLayerImage( MultiLayerImage *mim, fullPath* sfile){
	file_spec		src;

	char    		header[128], *h;
	long			count,chlength;
	unsigned long 		var;
	char			data[12], *d;
	short 			svar;
	int 			i, k, result = 0,nchannel, *nodd = NULL;
	unsigned char		**buf = NULL, ch;
	Image			im;
	int			BitsPerSample = 8;
	
	SetImageDefaults( &im );

	if( myopen( sfile,read_bin, src ) ){	
		PrintError("Error Opening Image File");
		return -1;
	}
		
	// Read psd header

	h 	= header;
	count 	= PSDHLENGTH;
	myread( src,count,h); 
	if( count != PSDHLENGTH ){
		PrintError("Error Reading Image Header");
		myclose( src );
		return -1;
	}
	
	if( ParsePSDHeader( header, &im ) != 0 ){
		PrintError("Wrong File Format");
		myclose( src );
		return -1;
	}
	
	// Read (and ignore) Color mode data
	READLONG( var ); 
	count = 1;
	for( i=0; i<var; i++ )
	{
		myread(src,count,h);
	}

	// Read (and ingnore) Image resources
	READLONG( var ); 
	count = 1;
	for( i=0; i<var; i++ )
	{
		myread(src,count,h);
	}

	// Read the layers


	// Read layerinfo up to channeldata
	READLONG( var );			// Length of info section(ignored)
	READLONG( var );			// Length of layer info (ignored)
	READSHORT( mim->numLayers );		// Number of layers
	
	mim->Layer 	= (Image*) 	malloc( mim->numLayers * sizeof( Image ) );
	nodd 	 	= (int*) malloc( mim->numLayers * sizeof( int ));
	
	if( mim->Layer == NULL || nodd == NULL) 
	{
		PrintError("Not enough memory");
		result = -1;
		goto readPSDMultiLayerImage_exit;
	}
	for(i=0; i<mim->numLayers; i++) 
	{
		SetImageDefaults( &mim->Layer[i] );
		mim->Layer[i].width  = im.width;
		mim->Layer[i].height = im.height;
		READLONG( mim->Layer[i].selection.top )	;  // Layer top 
		READLONG( mim->Layer[i].selection.left  ); // Layer left
		READLONG( mim->Layer[i].selection.bottom); // Layer bottom 
		READLONG( mim->Layer[i].selection.right ); // Layer right 
		
		READSHORT( nchannel ); 								// The number of channels in the layer.
		mim->Layer[i].bitsPerPixel = nchannel * BitsPerSample;

		mim->Layer[i].bytesPerLine = (mim->Layer[i].selection.right - mim->Layer[i].selection.left) *
						mim->Layer[i].bitsPerPixel/8;
		mim->Layer[i].dataSize = mim->Layer[i].bytesPerLine * 
					 (mim->Layer[i].selection.bottom - mim->Layer[i].selection.top);
		mim->Layer[i].data = (unsigned char**) mymalloc(mim->Layer[i].dataSize);
		if( mim->Layer[i].data == NULL )
		{
			PrintError("Not enough memory");
			result = -1;
			goto readPSDMultiLayerImage_exit;
		}
		
		
		// ********** Channel information ***************************** //
		
		READSHORT( svar ); // red
		READLONG(chlength);// Length of following channel data.
		READSHORT( svar ); // green
		READLONG(var); 	   // Length of following channel data.
		READSHORT( svar ); // blue
		READLONG(var); 	   // Length of following channel data.
		if( mim->Layer[i].bitsPerPixel == 32 || mim->Layer[i].bitsPerPixel == 64) // alpha channel
		{
			READSHORT( svar ); 		// transparency mask
			READLONG( var); 		// Length of following channel data.
		}
		// ********** End Channel information ***************************** //
	
		READLONG( var ); 					 // Blend mode signature Always 8BIM.
		READLONG( var ); 					 // Blend mode key

		READLONG( var ); 					// Four flag bytes

		READLONG( var ); 					// Extra data size Length of the extra data field. This is the
		READLONG( var ); 					// Layer Mask data
		READLONG( var ); 					// Layer blending ranges

		READLONG( var );				// Layer name
		
		var = 4*4 + 2 + nchannel * 6 + 4 + 4 + 4 * 1 + 4 + 12 + nchannel * chlength; // length
		if( var/2 != (var+1)/2 ) // odd
		{
			nodd[i] = 1; 
			//READUCHAR( ch );
		}
		else
		{
			nodd[i] = 0;
		}
	}
	
	
	// ************* End Layer Structure ******************************* //

	// ************* Read Channel Image data ************************** //

	for( i=0; i< mim->numLayers; i++)
	{
		nchannel 	= mim->Layer[i].bitsPerPixel/BitsPerSample;
		chlength 	= mim->Layer[i].dataSize/ nchannel;
		buf = (unsigned char**) mymalloc( chlength );
		if( buf == NULL )
		{
			PrintError("Not enough memory");
			result = -1;
			goto readPSDMultiLayerImage_exit;
		}
		for( k=0; k< nchannel; k++ )
		{
			READSHORT( svar );
			if( svar != 0 )
			{
				PrintError("File format error");
				result = -1;
				goto readPSDMultiLayerImage_exit; 
			}
			count = chlength;
			myread(  src, count, *buf );
			{
				register int x,y,cy,by;
				register unsigned char* theData = *(mim->Layer[i].data);
				int offset,bpp;
				
				offset = (mim->Layer[i].bitsPerPixel == 32?1:0) + k;
				if( k==3 ) offset = 0;
				
				bpp = mim->Layer[i].bitsPerPixel/8;
			
				for(y=0; y<mim->Layer[i].height; y++)
				{
					cy = y*mim->Layer[i].bytesPerLine + offset;
					by = y*mim->Layer[i].width;
					for(x=0; x<mim->Layer[i].width; x++)
					{
						theData[cy + bpp*x] = (*buf)[by + x];
					}
				}
			}
		}
		myfree( (void**)buf );
		if( nodd[i] )	// pad byte
		{
			READUCHAR( ch );
		}
	}
	

readPSDMultiLayerImage_exit:

	if( nodd != NULL )				free( nodd );

	myclose( src );
	return result;
}



	




