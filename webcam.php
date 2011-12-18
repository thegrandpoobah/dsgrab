<?php
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

// NOTE: This PHP script uses DSGrab, the GD image library, and WinSCP to 
// capture an image from an attached Windows Imaging device (read: WebCam)
// and upload to a remote FTP server. This should merely serve as an example
// of how to use DSGrab and other tools to create a "live cam" style tool
// and most likely will not work in your environment without significant
// amounts of tweaking.

# Some constants
$IMAGEWIDTH		= 640;
$IMAGEHEIGHT	= 480;
$TEXTSIZE		= 10;
$JPEGQUALITY	= 75;
$DATEFORMAT		= "l, F j, Y \a\\t g:i A";
$FONTFILE		= "ARIAL.TTF";
$CAPTUREDEVICE	= "Logitech QuickCam for Notebooks Pro"
$PATH_TO_DSGRAB = "DSGrab.exe";
$PATH_TO_OUTPUT = "webcam";
$PATH_TO_REMOTE = "images/webcam.jpg";
$REMOTE_NAME    = "<remote server>";
$PATH_TO_WINSCP = "C:\Program Files\winscp3\winscp3.com";
# Do the Capture
$command = <<< EOT
start /b {$PATH_TODSGRAB} -s -d "{$CAPTUREDEVICE}" -r {$IMAGEWIDTH}x{$IMAGEHEIGHT} -w 3000 {$PATH_TO_OUTPUT}.png
EOT;
$output = array();
$returnValue = 0;
exec( $command, $output, $returnValue );

# Do the timestamping using GD2
$imHandle = imagecreatefrompng( "{$PATH_TO_OUTPUT}.png" );
if ( $imHandle ) {
	$imageString = date( $DATEFORMAT );
	$fontPath = "C:\\Windows\\Fonts\\${FONTFILE}";
	
	$box = imagettfbbox( $TEXTSIZE, 0, $fontPath, $imageString );
	$textwidth = abs( $box[4] - $box[0] );
	$textheight = abs( $box[5] - $box[1] );
	$x = ( $IMAGEWIDTH - $textwidth ) / 2; // centre on the x-axis
	$y = $IMAGEHEIGHT - $textheight - 4; // magic number is the bottom padding
	
	$color = imagecolorallocate( $imHandle, 255, 0, 0 );
	
	$boundingBox = imagettftext( $imHandle, $TEXTSIZE, 0, $x, $y, $color, $fontPath, $imageString );
	imageinterlace( $imHandle, 1 );
	imagejpeg( $imHandle, "{$PATH_TO_OUTPUT}.jpg", $JPEGQUALITY );
	imagedestroy( $imHandle );
	unlink( "{$PATH_TO_OUTPUT}.png" );
}

# Do the Actual Transfer
$command = <<< EOT
option batch on
option confirm off
open "{$REMOTE_NAME}"
option transfer binary
put "{$PATH_TO_OUTPUT}" {$PATH_TO_REMOTE}
close
exit
EOT;

$tmpfile = tempnam( "%TMP", "wc" );
$handle = fopen( $tmpfile, 'w' );
fwrite( $handle, $command );
fclose( $handle );

exec( "start /b {$PATH_TO_WINSCP} /script={$tmpfile} > nul" );
unlink( $tmpfile );
?>