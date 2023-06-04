/*
 * Copyright (C) 2018 The Geeqie Team
 *
 * Author: Wolfgang Lieff
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 * Derived from dds_reader written by:
 * Copyright (c) 2015 Kenji Sasaki
 * Released under the MIT license.
 * https://github.com/npedotnet/DDSReader/blob/master/LICENSE

 */

#include "main.h"

#include "image-load.h"
#include "image-load-dds.h"

typedef struct _ImageLoaderDDS ImageLoaderDDS;
struct _ImageLoaderDDS {
	ImageLoaderBackendCbAreaUpdated area_updated_cb;
	ImageLoaderBackendCbSize size_cb;
	ImageLoaderBackendCbAreaPrepared area_prepared_cb;
	gpointer data;
	GdkPixbuf *pixbuf;
	guint requested_width;
	guint requested_height;
	gboolean abort;
};

static void free_buffer(guchar *pixels, gpointer UNUSED(data))
{
	g_free(pixels);
}

uint ddsGetHeight(unsigned const char * buffer) {
	return (buffer[12] & 0xFF) | (buffer[13] & 0xFF) << 8 | (buffer[14] & 0xFF) << 16 | (buffer[15] & 0xFF) << 24;
}

uint ddsGetWidth(unsigned const char * buffer) {
	return (buffer[16] & 0xFF) | (buffer[17] & 0xFF) << 8 | (buffer[18] & 0xFF) << 16 | (buffer[19] & 0xFF) << 24;
}

//uint ddsGetMipmap(unsigned const char * buffer) {
	//return (buffer[28] & 0xFF) | (buffer[29] & 0xFF) << 8 | (buffer[30] & 0xFF) << 16 | (buffer[31] & 0xFF) << 24;
//}

uint ddsGetPixelFormatFlags(unsigned const char * buffer) {
	return (buffer[80] & 0xFF) | (buffer[81] & 0xFF) << 8 | (buffer[82] & 0xFF) << 16 | (buffer[83] & 0xFF) << 24;
}

uint ddsGetFourCC(unsigned const char * buffer) {
	return (buffer[84] & 0xFF) << 24 | (buffer[85] & 0xFF) << 16 | (buffer[86] & 0xFF) << 8 | (buffer[87] & 0xFF);
}

uint ddsGetBitCount(unsigned const char * buffer) {
	return (buffer[88] & 0xFF) | (buffer[89] & 0xFF) << 8 | (buffer[90] & 0xFF) << 16 | (buffer[91] & 0xFF) << 24;
}

uint ddsGetRedMask(unsigned const char * buffer) {
	return (buffer[92] & 0xFF) | (buffer[93] & 0xFF) << 8 | (buffer[94] & 0xFF) << 16 | (buffer[95] & 0xFF) << 24;
}

uint ddsGetGreenMask(unsigned const char * buffer) {
	return (buffer[96] & 0xFF) | (buffer[97] & 0xFF) << 8 | (buffer[98] & 0xFF) << 16 | (buffer[99] & 0xFF) << 24;
}

uint ddsGetBlueMask(unsigned const char * buffer) {
	return (buffer[100] & 0xFF) | (buffer[101] & 0xFF) << 8 | (buffer[102] & 0xFF) << 16 | (buffer[103] & 0xFF) << 24;
}

uint ddsGetAlphaMask(unsigned const char * buffer) {
	return (buffer[104] & 0xFF) | (buffer[105] & 0xFF) << 8 | (buffer[106] & 0xFF) << 16 | (buffer[107] & 0xFF) << 24;
}

// Image Type
#define DXT1 (0x44585431)
#define DXT2 (0x44585432)
#define DXT3 (0x44585433)
#define DXT4 (0x44585434)
#define DXT5 (0x44585435)
#define A1R5G5B5 ((1 << 16) | 2)
#define X1R5G5B5 ((2 << 16) | 2)
#define A4R4G4B4 ((3 << 16) | 2)
#define X4R4G4B4 ((4 << 16) | 2)
#define R5G6B5 ((5 << 16) | 2)
#define R8G8B8 ((1 << 16) | 3)
#define A8B8G8R8 ((1 << 16) | 4)
#define X8B8G8R8 ((2 << 16) | 4)
#define A8R8G8B8 ((3 << 16) | 4)
#define X8R8G8B8 ((4 << 16) | 4)

// RGBA Masks
static const uint A1R5G5B5_MASKS[] = { 0x7C00, 0x03E0, 0x001F, 0x8000 };
static const uint X1R5G5B5_MASKS[] = { 0x7C00, 0x03E0, 0x001F, 0x0000 };
static const uint A4R4G4B4_MASKS[] = { 0x0F00, 0x00F0, 0x000F, 0xF000 };
static const uint X4R4G4B4_MASKS[] = { 0x0F00, 0x00F0, 0x000F, 0x0000 };
static const uint R5G6B5_MASKS[] = { 0xF800, 0x07E0, 0x001F, 0x0000 };
static const uint R8G8B8_MASKS[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0x000000 };
static const uint A8B8G8R8_MASKS[] = { 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000 };
static const uint X8B8G8R8_MASKS[] = { 0x000000FF, 0x0000FF00, 0x00FF0000, 0x00000000 };
static const uint A8R8G8B8_MASKS[] = { 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 };
static const uint X8R8G8B8_MASKS[] = { 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000 };

// BIT4 = 17 * index;
static const uint BIT5[] = { 0, 8, 16, 25, 33, 41, 49, 58, 66, 74, 82, 90, 99, 107, 115, 123, 132, 140, 148, 156, 165, 173, 181, 189, 197, 206, 214, 222, 230, 239, 247, 255 };
static const uint BIT6[] = { 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 45, 49, 53, 57, 61, 65, 69, 73, 77, 81, 85, 89, 93, 97, 101, 105, 109, 113, 117, 121, 125, 130, 134, 138, 142, 146, 150, 154, 158, 162, 166, 170, 174, 178, 182, 186, 190, 194, 198, 202, 206, 210, 215, 219, 223, 227, 231, 235, 239, 243, 247, 251, 255 };

static uint ddsGetType(const unsigned char *buffer) {
	uint type = 0;
	uint flags = ddsGetPixelFormatFlags(buffer);
	if ((flags & 0x04) != 0) {
		// DXT
		type = ddsGetFourCC(buffer);
	}
	else if ((flags & 0x40) != 0) {
		// RGB
		uint bitCount = ddsGetBitCount(buffer);
		uint redMask = ddsGetRedMask(buffer);
		uint greenMask = ddsGetGreenMask(buffer);
		uint blueMask = ddsGetBlueMask(buffer);
		uint alphaMask = ((flags & 0x01) != 0) ? ddsGetAlphaMask(buffer) : 0; // 0x01 alpha
		if (bitCount == 16) {
			if (redMask == A1R5G5B5_MASKS[0] && greenMask == A1R5G5B5_MASKS[1] && blueMask == A1R5G5B5_MASKS[2] && alphaMask == A1R5G5B5_MASKS[3]) {
				// A1R5G5B5
				type = A1R5G5B5;
			}
			else if (redMask == X1R5G5B5_MASKS[0] && greenMask == X1R5G5B5_MASKS[1] && blueMask == X1R5G5B5_MASKS[2] && alphaMask == X1R5G5B5_MASKS[3]) {
				// X1R5G5B5
				type = X1R5G5B5;
			}
			else if (redMask == A4R4G4B4_MASKS[0] && greenMask == A4R4G4B4_MASKS[1] && blueMask == A4R4G4B4_MASKS[2] && alphaMask == A4R4G4B4_MASKS[3]) {
				// A4R4G4B4
				type = A4R4G4B4;
			}
			else if (redMask == X4R4G4B4_MASKS[0] && greenMask == X4R4G4B4_MASKS[1] && blueMask == X4R4G4B4_MASKS[2] && alphaMask == X4R4G4B4_MASKS[3]) {
				// X4R4G4B4
				type = X4R4G4B4;
			}
			else if (redMask == R5G6B5_MASKS[0] && greenMask == R5G6B5_MASKS[1] && blueMask == R5G6B5_MASKS[2] && alphaMask == R5G6B5_MASKS[3]) {
				// R5G6B5
				type = R5G6B5;
			}
			else {
				// Unsupported 16bit RGB image
			}
		}
		else if (bitCount == 24) {
			if (redMask == R8G8B8_MASKS[0] && greenMask == R8G8B8_MASKS[1] && blueMask == R8G8B8_MASKS[2] && alphaMask == R8G8B8_MASKS[3]) {
				// R8G8B8
				type = R8G8B8;
			}
			else {
				// Unsupported 24bit RGB image
			}
		}
		else if (bitCount == 32) {
			if (redMask == A8B8G8R8_MASKS[0] && greenMask == A8B8G8R8_MASKS[1] && blueMask == A8B8G8R8_MASKS[2] && alphaMask == A8B8G8R8_MASKS[3]) {
				// A8B8G8R8
				type = A8B8G8R8;
			}
			else if (redMask == X8B8G8R8_MASKS[0] && greenMask == X8B8G8R8_MASKS[1] && blueMask == X8B8G8R8_MASKS[2] && alphaMask == X8B8G8R8_MASKS[3]) {
				// X8B8G8R8
				type = X8B8G8R8;
			}
			else if (redMask == A8R8G8B8_MASKS[0] && greenMask == A8R8G8B8_MASKS[1] && blueMask == A8R8G8B8_MASKS[2] && alphaMask == A8R8G8B8_MASKS[3]) {
				// A8R8G8B8
				type = A8R8G8B8;
			}
			else if (redMask == X8R8G8B8_MASKS[0] && greenMask == X8R8G8B8_MASKS[1] && blueMask == X8R8G8B8_MASKS[2] && alphaMask == X8R8G8B8_MASKS[3]) {
				// X8R8G8B8
				type = X8R8G8B8;
			}
			else {
				// Unsupported 32bit RGB image
			}
		}
	}
	else {
		// YUV or LUMINANCE image
	}
	return type;
}

uint ddsGetDXTColor2_1(uint c0, uint c1, uint a) {
	// 2*c0/3 + c1/3
	uint r = (2 * BIT5[(c0 & 0xFC00) >> 11] + BIT5[(c1 & 0xFC00) >> 11]) / 3;
	uint g = (2 * BIT6[(c0 & 0x07E0) >> 5] + BIT6[(c1 & 0x07E0) >> 5]) / 3;
	uint b = (2 * BIT5[c0 & 0x001F] + BIT5[c1 & 0x001F]) / 3;
	return (a << 24) | (r << 0) | (g << 8) | (b << 16);
}

uint ddsGetDXTColor1_1(uint c0, uint c1, uint a) {
	// (c0+c1) / 2
	uint r = (BIT5[(c0 & 0xFC00) >> 11] + BIT5[(c1 & 0xFC00) >> 11]) / 2;
	uint g = (BIT6[(c0 & 0x07E0) >> 5] + BIT6[(c1 & 0x07E0) >> 5]) / 2;
	uint b = (BIT5[c0 & 0x001F] + BIT5[c1 & 0x001F]) / 2;
	return (a << 24) | (r << 0) | (g << 8) | (b << 16);
}

uint ddsGetDXTColor1(uint c, uint a) {
	uint r = BIT5[(c & 0xFC00) >> 11];
	uint g = BIT6[(c & 0x07E0) >> 5];
	uint b = BIT5[(c & 0x001F)];
	return (a << 24) | (r << 0) | (g << 8) | (b << 16);
}

uint ddsGetDXTColor(uint c0, uint c1, uint a, uint t) {
	switch (t) {
	case 0: return ddsGetDXTColor1(c0, a);
	case 1: return ddsGetDXTColor1(c1, a);
	case 2: return (c0 > c1) ? ddsGetDXTColor2_1(c0, c1, a) : ddsGetDXTColor1_1(c0, c1, a);
	case 3: return (c0 > c1) ? ddsGetDXTColor2_1(c1, c0, a) : 0;
	}
	return 0;
}

guchar *ddsDecodeDXT1(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	uint w = (width + 3) / 4;
	uint h = (height + 3) / 4;
	for (uint i = 0; i<h; i++) {
		for (uint j = 0; j<w; j++) {
			uint c0 = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8; index += 2;
			uint c1 = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8; index += 2;
			for (uint k = 0; k<4; k++) {
				if (4 * i + k >= height) break;
				uint t0 = (buffer[index] & 0x03);
				uint t1 = (buffer[index] & 0x0C) >> 2;
				uint t2 = (buffer[index] & 0x30) >> 4;
				uint t3 = (buffer[index++] & 0xC0) >> 6;
				pixels[4 * width*i + 4 * j + width*k + 0] = ddsGetDXTColor(c0, c1, 0xFF, t0);
				if (4 * j + 1 >= width) continue;
				pixels[4 * width*i + 4 * j + width*k + 1] = ddsGetDXTColor(c0, c1, 0xFF, t1);
				if (4 * j + 2 >= width) continue;
				pixels[4 * width*i + 4 * j + width*k + 2] = ddsGetDXTColor(c0, c1, 0xFF, t2);
				if (4 * j + 3 >= width) continue;
				pixels[4 * width*i + 4 * j + width*k + 3] = ddsGetDXTColor(c0, c1, 0xFF, t3);
			}
		}
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsDecodeDXT3(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	uint w = (width + 3) / 4;
	uint h = (height + 3) / 4;
	uint alphaTable[16];
	for (uint i = 0; i<h; i++) {
		for (uint j = 0; j<w; j++) {
			// create alpha table(4bit to 8bit)
			for (uint k = 0; k<4; k++) {
				uint a0 = (buffer[index++] & 0xFF);
				uint a1 = (buffer[index++] & 0xFF);
				// 4bit alpha to 8bit alpha
				alphaTable[4 * k + 0] = 17 * ((a0 & 0xF0) >> 4);
				alphaTable[4 * k + 1] = 17 * (a0 & 0x0F);
				alphaTable[4 * k + 2] = 17 * ((a1 & 0xF0) >> 4);
				alphaTable[4 * k + 3] = 17 * (a1 & 0x0F);
			}
			uint c0 = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8; index += 2;
			uint c1 = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8; index += 2;
			for (uint k = 0; k<4; k++) {
				if (4 * i + k >= height) break;
				uint t0 = (buffer[index] & 0x03);
				uint t1 = (buffer[index] & 0x0C) >> 2;
				uint t2 = (buffer[index] & 0x30) >> 4;
				uint t3 = (buffer[index++] & 0xC0) >> 6;
				pixels[4 * width*i + 4 * j + width*k + 0] = ddsGetDXTColor(c0, c1, alphaTable[4 * k + 0], t0);
				if (4 * j + 1 >= width) continue;
				pixels[4 * width*i + 4 * j + width*k + 1] = ddsGetDXTColor(c0, c1, alphaTable[4 * k + 1], t1);
				if (4 * j + 2 >= width) continue;
				pixels[4 * width*i + 4 * j + width*k + 2] = ddsGetDXTColor(c0, c1, alphaTable[4 * k + 2], t2);
				if (4 * j + 3 >= width) continue;
				pixels[4 * width*i + 4 * j + width*k + 3] = ddsGetDXTColor(c0, c1, alphaTable[4 * k + 3], t3);
			}
		}
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsDecodeDXT2(uint width, uint height, const unsigned char *buffer) {
	return ddsDecodeDXT3(width, height, buffer);
}

int ddsGetDXT5Alpha(uint a0, uint a1, uint t) {
	if (a0 > a1) switch (t) {
	case 0: return a0;
	case 1: return a1;
	case 2: return (6 * a0 + a1) / 7;
	case 3: return (5 * a0 + 2 * a1) / 7;
	case 4: return (4 * a0 + 3 * a1) / 7;
	case 5: return (3 * a0 + 4 * a1) / 7;
	case 6: return (2 * a0 + 5 * a1) / 7;
	case 7: return (a0 + 6 * a1) / 7;
	}
	else switch (t) {
	case 0: return a0;
	case 1: return a1;
	case 2: return (4 * a0 + a1) / 5;
	case 3: return (3 * a0 + 2 * a1) / 5;
	case 4: return (2 * a0 + 3 * a1) / 5;
	case 5: return (a0 + 4 * a1) / 5;
	case 6: return 0;
	case 7: return 255;
	}
	return 0;
}

guchar *ddsDecodeDXT5(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	uint w = (width + 3) / 4;
	uint h = (height + 3) / 4;
	uint alphaTable[16];
	for (uint i = 0; i<h; i++) {
		for (uint j = 0; j<w; j++) {
			// create alpha table
			uint a0 = (buffer[index++] & 0xFF);
			uint a1 = (buffer[index++] & 0xFF);
			uint b0 = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8 | (buffer[index + 2] & 0xFF) << 16; index += 3;
			uint b1 = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8 | (buffer[index + 2] & 0xFF) << 16; index += 3;
			alphaTable[0] = b0 & 0x07;
			alphaTable[1] = (b0 >> 3) & 0x07;
			alphaTable[2] = (b0 >> 6) & 0x07;
			alphaTable[3] = (b0 >> 9) & 0x07;
			alphaTable[4] = (b0 >> 12) & 0x07;
			alphaTable[5] = (b0 >> 15) & 0x07;
			alphaTable[6] = (b0 >> 18) & 0x07;
			alphaTable[7] = (b0 >> 21) & 0x07;
			alphaTable[8] = b1 & 0x07;
			alphaTable[9] = (b1 >> 3) & 0x07;
			alphaTable[10] = (b1 >> 6) & 0x07;
			alphaTable[11] = (b1 >> 9) & 0x07;
			alphaTable[12] = (b1 >> 12) & 0x07;
			alphaTable[13] = (b1 >> 15) & 0x07;
			alphaTable[14] = (b1 >> 18) & 0x07;
			alphaTable[15] = (b1 >> 21) & 0x07;
			uint c0 = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8; index += 2;
			uint c1 = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8; index += 2;
			for (uint k = 0; k<4; k++) {
				if (4 * i + k >= height) break;
				uint t0 = (buffer[index] & 0x03);
				uint t1 = (buffer[index] & 0x0C) >> 2;
				uint t2 = (buffer[index] & 0x30) >> 4;
				uint t3 = (buffer[index++] & 0xC0) >> 6;
				pixels[4 * width*i + 4 * j + width*k + 0] = ddsGetDXTColor(c0, c1, ddsGetDXT5Alpha(a0, a1, alphaTable[4 * k + 0]), t0);
				if (4 * j + 1 >= width) continue;
				pixels[4 * width*i + 4 * j + width*k + 1] = ddsGetDXTColor(c0, c1, ddsGetDXT5Alpha(a0, a1, alphaTable[4 * k + 1]), t1);
				if (4 * j + 2 >= width) continue;
				pixels[4 * width*i + 4 * j + width*k + 2] = ddsGetDXTColor(c0, c1, ddsGetDXT5Alpha(a0, a1, alphaTable[4 * k + 2]), t2);
				if (4 * j + 3 >= width) continue;
				pixels[4 * width*i + 4 * j + width*k + 3] = ddsGetDXTColor(c0, c1, ddsGetDXT5Alpha(a0, a1, alphaTable[4 * k + 3]), t3);
			}
		}
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsDecodeDXT4(uint width, uint height, const unsigned char *buffer) {
	return ddsDecodeDXT5(width, height, buffer);
}

guchar *ddsReadA1R5G5B5(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	for (uint i = 0; i<height*width; i++) {
		uint rgba = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8; index += 2;
		uint r = BIT5[(rgba & A1R5G5B5_MASKS[0]) >> 10];
		uint g = BIT5[(rgba & A1R5G5B5_MASKS[1]) >> 5];
		uint b = BIT5[(rgba & A1R5G5B5_MASKS[2])];
		uint a = 255 * ((rgba & A1R5G5B5_MASKS[3]) >> 15);
		pixels[i] = (a << 24) | (r << 0) | (g << 8) | (b << 16);
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsReadX1R5G5B5(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	for (uint i = 0; i<height*width; i++) {
		uint rgba = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8; index += 2;
		uint r = BIT5[(rgba & X1R5G5B5_MASKS[0]) >> 10];
		uint g = BIT5[(rgba & X1R5G5B5_MASKS[1]) >> 5];
		uint b = BIT5[(rgba & X1R5G5B5_MASKS[2])];
		uint a = 255;
		pixels[i] = (a << 24) | (r << 0) | (g << 8) | (b << 16);
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsReadA4R4G4B4(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	for (uint i = 0; i<height*width; i++) {
		uint rgba = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8; index += 2;
		uint r = 17 * ((rgba & A4R4G4B4_MASKS[0]) >> 8);
		uint g = 17 * ((rgba & A4R4G4B4_MASKS[1]) >> 4);
		uint b = 17 * ((rgba & A4R4G4B4_MASKS[2]));
		uint a = 17 * ((rgba & A4R4G4B4_MASKS[3]) >> 12);
		pixels[i] = (a << 24) | (r << 0) | (g << 8) | (b << 16);
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsReadX4R4G4B4(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	for (uint i = 0; i<height*width; i++) {
		uint rgba = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8; index += 2;
		uint r = 17 * ((rgba & A4R4G4B4_MASKS[0]) >> 8);
		uint g = 17 * ((rgba & A4R4G4B4_MASKS[1]) >> 4);
		uint b = 17 * ((rgba & A4R4G4B4_MASKS[2]));
		uint a = 255;
		pixels[i] = (a << 24) | (r << 0) | (g << 8) | (b << 16);
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsReadR5G6B5(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	for (uint i = 0; i<height*width; i++) {
		uint rgba = (buffer[index] & 0xFF) | (buffer[index + 1] & 0xFF) << 8; index += 2;
		uint r = BIT5[((rgba & R5G6B5_MASKS[0]) >> 11)];
		uint g = BIT6[((rgba & R5G6B5_MASKS[1]) >> 5)];
		uint b = BIT5[((rgba & R5G6B5_MASKS[2]))];
		uint a = 255;
		pixels[i] = (a << 24) | (r << 0) | (g << 8) | (b << 16);
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsReadR8G8B8(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	for (uint i = 0; i<height*width; i++) {
		uint b = buffer[index++] & 0xFF;
		uint g = buffer[index++] & 0xFF;
		uint r = buffer[index++] & 0xFF;
		uint a = 255;
		pixels[i] = (a << 24) | (r << 0) | (g << 8) | (b << 16);
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsReadA8B8G8R8(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	for (uint i = 0; i<height*width; i++) {
		uint r = buffer[index++] & 0xFF;
		uint g = buffer[index++] & 0xFF;
		uint b = buffer[index++] & 0xFF;
		uint a = buffer[index++] & 0xFF;
		pixels[i] = (a << 24) | (r << 0) | (g << 8) | (b << 16);
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsReadX8B8G8R8(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	for (uint i = 0; i<height*width; i++) {
		uint r = buffer[index++] & 0xFF;
		uint g = buffer[index++] & 0xFF;
		uint b = buffer[index++] & 0xFF;
		uint a = 255; index++;
		pixels[i] = (a << 24) | (r << 0) | (g << 8) | (b << 16);
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsReadA8R8G8B8(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	for (uint i = 0; i<height*width; i++) {
		uint b = buffer[index++] & 0xFF;
		uint g = buffer[index++] & 0xFF;
		uint r = buffer[index++] & 0xFF;
		uint a = buffer[index++] & 0xFF;
		pixels[i] = (a << 24) | (r << 0) | (g << 8) | (b << 16);
	}
	return reinterpret_cast<guchar *>(pixels);
}

guchar *ddsReadX8R8G8B8(uint width, uint height, const unsigned char *buffer) {
	auto pixels = static_cast<uint *>(g_try_malloc(4 * width*height));
	if (pixels == NULL) return NULL;
	uint index = 128;
	for (uint i = 0; i<height*width; i++) {
		uint b = buffer[index++] & 0xFF;
		uint g = buffer[index++] & 0xFF;
		uint r = buffer[index++] & 0xFF;
		uint a = 255; index++;
		pixels[i] = (a << 24) | (r << 0) | (g << 8) | (b << 16);
	}
	return reinterpret_cast<guchar *>(pixels);
}

static gboolean image_loader_dds_load (gpointer loader, const guchar *buf, gsize UNUSED(count), GError **UNUSED(error))
{
	auto ld = static_cast<ImageLoaderDDS *>(loader);
	uint width = ddsGetWidth(buf);
	uint height = ddsGetHeight(buf);
	uint type = ddsGetType(buf);
	if (type == 0) return FALSE;
	{
		guchar *pixels = NULL;
		guint rowstride = width * 4;
		switch (type) {
		case DXT1: pixels = ddsDecodeDXT1(width, height, buf); break;
		case DXT2: pixels = ddsDecodeDXT2(width, height, buf); break;
		case DXT3: pixels = ddsDecodeDXT3(width, height, buf); break;
		case DXT4: pixels = ddsDecodeDXT4(width, height, buf); break;
		case DXT5: pixels = ddsDecodeDXT5(width, height, buf); break;
		case A1R5G5B5: pixels = ddsReadA1R5G5B5(width, height, buf); break;
		case X1R5G5B5: pixels = ddsReadX1R5G5B5(width, height, buf); break;
		case A4R4G4B4: pixels = ddsReadA4R4G4B4(width, height, buf); break;
		case X4R4G4B4: pixels = ddsReadX4R4G4B4(width, height, buf); break;
		case R5G6B5: pixels = ddsReadR5G6B5(width, height, buf); break;
		case R8G8B8: pixels = ddsReadR8G8B8(width, height, buf); break;
		case A8B8G8R8: pixels = ddsReadA8B8G8R8(width, height, buf); break;
		case X8B8G8R8: pixels = ddsReadX8B8G8R8(width, height, buf); break;
		case A8R8G8B8: pixels = ddsReadA8R8G8B8(width, height, buf); break;
		case X8R8G8B8: pixels = ddsReadX8R8G8B8(width, height, buf); break;
		}
		ld->pixbuf = gdk_pixbuf_new_from_data (pixels, GDK_COLORSPACE_RGB, TRUE, 8, width, height, rowstride, free_buffer, NULL);
		ld->area_updated_cb(loader, 0, 0, width, height, ld->data);
		return TRUE;
	}
}

static gpointer image_loader_dds_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	auto loader = g_new0(ImageLoaderDDS, 1);
	loader->area_updated_cb = area_updated_cb;
	loader->size_cb = size_cb;
	loader->area_prepared_cb = area_prepared_cb;
	loader->data = data;
	return (gpointer) loader;
}

static void image_loader_dds_set_size(gpointer loader, int width, int height)
{
	auto ld = static_cast<ImageLoaderDDS *>(loader);
	ld->requested_width = width;
	ld->requested_height = height;
}

static GdkPixbuf* image_loader_dds_get_pixbuf(gpointer loader)
{
	auto ld = static_cast<ImageLoaderDDS *>(loader);
	return ld->pixbuf;
}

static gchar* image_loader_dds_get_format_name(gpointer UNUSED(loader))
{
	return g_strdup("dds");
}
static gchar** image_loader_dds_get_format_mime_types(gpointer UNUSED(loader))
{
	static const gchar *mime[] = {"image/vnd-ms.dds", NULL};
	return g_strdupv(const_cast<gchar **>(mime));
}

static gboolean image_loader_dds_close(gpointer UNUSED(loader), GError **UNUSED(error))
{
	return TRUE;
}

static void image_loader_dds_abort(gpointer loader)
{
	auto ld = static_cast<ImageLoaderDDS *>(loader);
	ld->abort = TRUE;
}

static void image_loader_dds_free(gpointer loader)
{
	auto ld = static_cast<ImageLoaderDDS *>(loader);
	if (ld->pixbuf) g_object_unref(ld->pixbuf);
	g_free(ld);
}

void image_loader_backend_set_dds(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_dds_new;
	funcs->set_size = image_loader_dds_set_size;
	funcs->load = image_loader_dds_load;
	funcs->write = NULL;
	funcs->get_pixbuf = image_loader_dds_get_pixbuf;
	funcs->close = image_loader_dds_close;
	funcs->abort = image_loader_dds_abort;
	funcs->free = image_loader_dds_free;
	funcs->get_format_name = image_loader_dds_get_format_name;
	funcs->get_format_mime_types = image_loader_dds_get_format_mime_types;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
