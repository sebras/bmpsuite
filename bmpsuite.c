//
// BMP Suite (2012 rewrite)
// Copyright (C) 2012 Jason Summers
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//
// Image files generated by this program are in the public domain.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>

#define BMP_MAX_SIZE 100000

#define bmpovl_width 78
#define bmpovl_height 26
#define bmpovl_xpos 25
#define bmpovl_ypos 19

static const char *bmpovl[] = {
 "11111111111111111111.......11....................11...11111111111111111111....",
 "1111111111111111111111.....111..................111...1111111111111111111111..",
 "11222222222222222222111....1111................1111...11222222222222222222111.",
 "112222222222222222222111...11211..............11211...112222222222222222222111",
 "112211111111111111122211...112211............112211...112211111111111111122211",
 "112211111111111111112211...1122211..........1122211...112211111111111111112211",
 "112211...........1112211...11222211........11222211...112211...........1112211",
 "112211............112211...112212211......112212211...112211............112211",
 "112211............112211...1122112211....1122112211...112211............112211",
 "112211...........1112211...11221112211..11221112211...112211...........1112211",
 "11221111111111111112211....112211112211112211112211...112211111111111111112211",
 "1122111111111111112211.....112211.1122112211.112211...112211111111111111122211",
 "112222222222222222211......112211..11222211..112211...112222222222222222222111",
 "112222222222222222211......112211...112211...112211...11222222222222222222111.",
 "1122111111111111112211.....112211....1111....112211...1122111111111111111111..",
 "11221111111111111112211....112211.....11.....112211...11221111111111111111....",
 "112211...........1112211...112211............112211...112211..................",
 "112211............112211...112211............112211...112211..................",
 "112211............112211...112211............112211...112211..................",
 "112211...........1112211...112211............112211...112211..................",
 "112211111111111111112211...112211............112211...112211..................",
 "112211111111111111122211...112211............112211...112211..................",
 "112222222222222222222111...112211............112211...112211..................",
 "11222222222222222222111....112211............112211...112211..................",
 "1111111111111111111111.....111111............111111...111111..................",
 "11111111111111111111.......111111............111111...111111.................."
};

struct context {
	const char *filename;
	unsigned char *mem;
	size_t mem_used;
	int bpp;
	int pal_entries;
	int clr_used;
	int bmpversion;
	int headersize;
	int bitfieldssize;
	int palettesize;
	int bitsoffset; // Offset from beginning of file
	int bitssize;
	int w, h;
	int rowsize;
	int xpelspermeter, ypelspermeter;
	int compression;

	int pal_wb; // 2-color, palette[0] = white
	int pal_bg; // 2-color, blue & green
	int pal_p1; // 1-color
	int rgba;
	unsigned int bf_r, bf_g, bf_b, bf_a; // used if compression==3
	unsigned int nbits_r, nbits_g, nbits_b, nbits_a;
	int dither;
	int topdown;
};

static void set_int16(struct context *c, size_t offset, int v)
{
	c->mem[offset] = v&0xff;
	c->mem[offset+1] = (v>>8)&0xff;
}

static void set_int32(struct context *c, size_t offset, int v)
{
	c->mem[offset] = v&0xff;
	c->mem[offset+1] = (v>>8)&0xff;
	c->mem[offset+2] = (v>>16)&0xff;
	c->mem[offset+3] = (v>>24)&0xff;
}

static void set_uint32(struct context *c, size_t offset, unsigned int v)
{
	c->mem[offset] = v&0xff;
	c->mem[offset+1] = (v>>8)&0xff;
	c->mem[offset+2] = (v>>16)&0xff;
	c->mem[offset+3] = (v>>24)&0xff;
}

// Returns an int between 0 and m, inclusive.
static int scale_to_int(double x, int m)
{
	int s;
	s = (int)(0.5+x*m);
	if(s<0) s=0;
	if(s>m) s=m;
	return s;
}

static double srgb_to_linear(double v_srgb)
{
	if(v_srgb<=0.04045) {
		return v_srgb/12.92;
	}
	else {
		return pow( (v_srgb+0.055)/(1.055) , 2.4);
	}
}

static void get_pixel_color(struct context *c, int x, int y,
	double *pr, double *pg, double *pb, double *pa)
{
	unsigned char t;

	if(x>=bmpovl_xpos && x<(bmpovl_xpos+bmpovl_width) &&
	   y>=bmpovl_ypos && y<(bmpovl_ypos+bmpovl_height))
	{
		t = bmpovl[y-bmpovl_ypos][x-bmpovl_xpos];
		if(t=='1') {
			*pr = 0.0; *pg = 0.0; *pb = 0.0; *pa = 1.0;
			return;
		}
		else if(t=='2') {
			if(c->rgba) {
				// Make the inside of the overlay transparent, if possible.
				if( (y-bmpovl_ypos)<(bmpovl_height/2) ) {
					// Make the top half complete transparent ("transparent green").
					*pr = 0.0; *pg = 1.0; *pb = 0.0; *pa = 0.0;
				}
				else {
					// Make the bootom half a red gradient from transparent to opaque.
					*pr = 1.0; *pg = 0.0; *pb = 0.0;
					*pa = 2*((double)(y-bmpovl_ypos)) /(bmpovl_height) -1.0;
				}
			}
			else {
				*pr = 1.0; *pg = 1.0; *pb = 1.0; *pa = 1.0;
			}
			return;
		}
	}

	*pa = 1.0;

	// Standard truecolor image
	if(x<32) {
		*pr = ((double)(63-y))/63.0;
		*pg = ((double)(x%32))/31.0;
		*pb = ((double)(x%32))/31.0;
	}
	else if(x<64) {
		*pr = ((double)(x%32))/31.0;
		*pg = ((double)(63-y))/63.0;
		*pb = ((double)(x%32))/31.0;
	}
	else if(x<96) {
		*pr = ((double)(x%32))/31.0;
		*pg = ((double)(x%32))/31.0;
		*pb = ((double)(63-y))/63.0;
	}
	else {
		*pr = ((double)(159-y))/255.0;
		*pg = ((double)(159-y))/255.0;
		*pb = ((double)(159-y + x%32))/255.0;
	}
}

static int ordered_dither_lowlevel(double fraction, int x, int y)
{
	double threshold;
	static const float pattern[64] = {
		 0.5/64,48.5/64,12.5/64,60.5/64, 3.5/64,51.5/64,15.5/64,63.5/64,
		32.5/64,16.5/64,44.5/64,28.5/64,35.5/64,19.5/64,47.5/64,31.5/64,
		 8.5/64,56.5/64, 4.5/64,52.5/64,11.5/64,59.5/64, 7.5/64,55.5/64,
		40.5/64,24.5/64,36.5/64,20.5/64,43.5/64,27.5/64,39.5/64,23.5/64,
		 2.5/64,50.5/64,14.5/64,62.5/64, 1.5/64,49.5/64,13.5/64,61.5/64,
		34.5/64,18.5/64,46.5/64,30.5/64,33.5/64,17.5/64,45.5/64,29.5/64,
		10.5/64,58.5/64, 6.5/64,54.5/64, 9.5/64,57.5/64, 5.5/64,53.5/64,
		42.5/64,26.5/64,38.5/64,22.5/64,41.5/64,25.5/64,37.5/64,21.5/64
	 };

	threshold = pattern[(x%8) + 8*(y%8)];
	return (fraction >= threshold) ? 1 : 0;
}

// 'v' is on a scale from 0.0 to 1.0.
// maxcc is the max color code; e.g. 255.
// This returns the output color code on a scale of 0 to maxcc.
static int ordered_dither(double v_to_1, int maxcc, int x, int y)
{
	double v_to_1_linear;
	double v_to_maxcc;
	double floor_to_maxcc, ceil_to_maxcc;
	double floor_to_1, ceil_to_1;
	double floor_to_1_linear, ceil_to_1_linear;
	double fraction;

	v_to_maxcc = v_to_1*maxcc;
	floor_to_maxcc = floor(v_to_maxcc);
	if(floor_to_maxcc>=(double)maxcc) return maxcc;
	ceil_to_maxcc = floor_to_maxcc+1.0;

	// The two possible values to return are floor_to_maxcc and ceil_to_maxcc.
	// v_to_maxcc's brightness is some fraction of the way between
	// floor_to_maxcc's brightness and ceil_to_maxcc's brightness, and we need
	// to calculate that fraction. To do that, convert everything to a linear
	// colorspace.

	floor_to_1 = floor_to_maxcc/maxcc;
	ceil_to_1 = ceil_to_maxcc/maxcc;

	floor_to_1_linear = srgb_to_linear(floor_to_1);
	v_to_1_linear = srgb_to_linear(v_to_1);
	ceil_to_1_linear = srgb_to_linear(ceil_to_1);

	fraction = (v_to_1_linear-floor_to_1_linear)/(ceil_to_1_linear-floor_to_1_linear);

	if(ordered_dither_lowlevel(fraction,x,y))
		return (int)ceil_to_maxcc;
	else
		return (int)floor_to_maxcc;
}

static void set_pixel(struct context *c, int x, int y,
  double r, double g, double b, double a)
{
	unsigned int r2, g2, b2, a2;
	int tmp1, tmp2, tmp3;
	int p;
	size_t row_offs;
	size_t offs;
	double tmpd;
	unsigned int u;

	if(c->topdown)
		row_offs = y*c->rowsize;
	else
		row_offs = (c->h-y-1)*c->rowsize;

	if(c->bpp==32) {
		offs = row_offs + 4*x;
		if(c->compression==3 && !c->rgba) {
			r2 = scale_to_int(r,(1<<c->nbits_r)-1);
			g2 = scale_to_int(g,(1<<c->nbits_g)-1);
			b2 = scale_to_int(b,(1<<c->nbits_b)-1);
			u = (r2<<(c->nbits_g+c->nbits_b)) | (g2<<c->nbits_b) | b2;
			c->mem[c->bitsoffset+offs+0] = (unsigned char)(u&0xff);
			c->mem[c->bitsoffset+offs+1] = (unsigned char)((u>>8)&0xff);
			c->mem[c->bitsoffset+offs+2] = (unsigned char)((u>>16)&0xff);
			c->mem[c->bitsoffset+offs+3] = (unsigned char)((u>>24)&0xff);
		}
		else {
			r2 = scale_to_int(r,255);
			g2 = scale_to_int(g,255);
			b2 = scale_to_int(b,255);
			if(c->rgba) a2 = scale_to_int(a,255);
			else a2 = 0;
			c->mem[c->bitsoffset+offs+0] = b2;
			c->mem[c->bitsoffset+offs+1] = g2;
			c->mem[c->bitsoffset+offs+2] = r2;
			c->mem[c->bitsoffset+offs+3] = a2;
		}
	}
	else if(c->bpp==24) {
		offs = row_offs + 3*x;
		r2 = (unsigned char)scale_to_int(r,255);
		g2 = (unsigned char)scale_to_int(g,255);
		b2 = (unsigned char)scale_to_int(b,255);
		c->mem[c->bitsoffset+offs+0] = b2;
		c->mem[c->bitsoffset+offs+1] = g2;
		c->mem[c->bitsoffset+offs+2] = r2;
	}
	else if(c->bpp==16) {
		offs = row_offs + 2*x;
		if(c->dither) {
			r2 = ordered_dither(r,(1<<c->nbits_r)-1,x,y);
			g2 = ordered_dither(g,(1<<c->nbits_g)-1,x,y);
			b2 = ordered_dither(b,(1<<c->nbits_b)-1,x,y);
		}
		else {
			r2 = scale_to_int(r,(1<<c->nbits_r)-1);
			g2 = scale_to_int(g,(1<<c->nbits_g)-1);
			b2 = scale_to_int(b,(1<<c->nbits_b)-1);
		}
		if(c->rgba) a2 = scale_to_int(a,(1<<c->nbits_a)-1);

		u = (r2<<(c->nbits_g+c->nbits_b)) | (g2<<c->nbits_b) | b2;
		if(c->rgba) u |= a2<<(c->nbits_r+c->nbits_g+c->nbits_b);
		c->mem[c->bitsoffset+offs+0] = (unsigned char)(u&0xff);
		c->mem[c->bitsoffset+offs+1] = (unsigned char)((u>>8)&0xff);
	}
	else if(c->bpp==8) {
		offs = row_offs + x;
		tmp1 = ordered_dither(r,5,x,y);
		tmp2 = ordered_dither(g,6,x,y);
		tmp3 = ordered_dither(b,5,x,y);
		p = tmp1 + tmp2*6 + tmp3*42;
		c->mem[c->bitsoffset+offs] = p;
	}
	else if(c->bpp==4) {
		offs = row_offs + x/2;
		tmp1 = ordered_dither(r,1,x,y);
		tmp2 = ordered_dither(g,2,x,y);
		tmp3 = ordered_dither(b,1,x,y);
		p = tmp1 + tmp2*2 + tmp3*6;
		if(x%2)
			c->mem[c->bitsoffset+offs] |= p;
		else
			c->mem[c->bitsoffset+offs] |= p<<4;
	}
	else if(c->bpp==1) {
		offs = row_offs + x/8;
		tmpd = srgb_to_linear(r)*0.212655
			 + srgb_to_linear(g)*0.715158
			 + srgb_to_linear(b)*0.072187;
		tmp1 = ordered_dither_lowlevel(tmpd,x,y);
		if(c->pal_wb) tmp1 = 1-tmp1; // Palette starts with white, so invert the colors.
		if(c->pal_p1) tmp1 = 0;
		if(tmp1) {
			c->mem[c->bitsoffset+offs] |= 1<<(7-x%8);
		}
	}
}

static void write_bits(struct context *c)
{
	int i,j;
	double r, g, b, a;

	c->rowsize = (((c->w * c->bpp)+31)/32)*4;
	c->bitssize = c->rowsize*c->h;

	c->mem_used = c->bitsoffset + c->bitssize;

	for(j=0;j<c->h;j++) {
		for(i=0;i<c->w;i++) {
			get_pixel_color(c,i,j,&r,&g,&b,&a);
			set_pixel(c,i,j,r,g,b,a);
		}
	}
}

static void write_bitfields(struct context *c)
{
	size_t offs;
	if(c->bitfieldssize!=12) return;
	offs = 14+c->headersize;
	set_uint32(c,offs  ,c->bf_r);
	set_uint32(c,offs+4,c->bf_g);
	set_uint32(c,offs+8,c->bf_b);
}

static void write_palette(struct context *c)
{
	size_t offs;
	int i;
	int r,g,b;

	offs = 14+c->headersize+c->bitfieldssize;

	if(c->bpp==8) {
		// R6G7B6 palette
		// Entry for a given (R,G,B) is R + G*6 + B*42
		for(i=0;i<c->pal_entries;i++) {
			if(i>=256) continue;
			r = i%6;
			g = (i%42)/6;
			b = i/42;
			c->mem[offs+4*i+2] = scale_to_int( ((double)r)/5.0, 255);
			c->mem[offs+4*i+1] = scale_to_int( ((double)g)/6.0, 255);
			c->mem[offs+4*i+0] = scale_to_int( ((double)b)/5.0, 255);
		}
	}
	else if(c->bpp==4) {
		for(i=0;i<c->pal_entries;i++) {
			r = i%2;
			g = (i%6)/2;
			b = i/6;
			c->mem[offs+4*i+2] = scale_to_int( ((double)r)/1.0, 255);
			c->mem[offs+4*i+1] = scale_to_int( ((double)g)/2.0, 255);
			c->mem[offs+4*i+0] = scale_to_int( ((double)b)/1.0, 255);
		}
	}
	else if(c->bpp==1) {
		if(c->pal_entries==2) {
			if(c->pal_wb) {
				c->mem[offs+4*0+2] = 255;
				c->mem[offs+4*0+1] = 255;
				c->mem[offs+4*0+0] = 255;
			}
			else if(c->pal_bg) {
				c->mem[offs+4*0+2] = 64;
				c->mem[offs+4*0+1] = 64;
				c->mem[offs+4*0+0] = 255;
				c->mem[offs+4*1+2] = 64;
				c->mem[offs+4*1+1] = 255;
				c->mem[offs+4*1+0] = 64;
			}
			else {
				c->mem[offs+4*1+2] = 255;
				c->mem[offs+4*1+1] = 255;
				c->mem[offs+4*1+0] = 255;
			}
		}
		else { // assuming c->pal_p1
			c->mem[offs+4*0+2] = 64;
			c->mem[offs+4*0+1] = 64;
			c->mem[offs+4*0+0] = 255;
		}
	}
	else if(c->bpp>8) {
		// Write a 'suggested' palette.
		for(i=0;i<c->pal_entries;i++) {
			c->mem[offs+4*i+2] = (unsigned char)(i*15);
			c->mem[offs+4*i+1] = (unsigned char)(i*15);
			c->mem[offs+4*i+0] = (unsigned char)(i*15);
		}

	}
}

static void write_fileheader(struct context *c)
{
	c->mem[0]='B';
	c->mem[1]='M';
	set_int32(c,2,(int)c->mem_used);
	set_int32(c,10,c->bitsoffset);
}

static void write_bitmapinfoheader(struct context *c)
{
	set_int32(c,14+0,c->headersize);
	set_int32(c,14+4,c->w);
	set_int32(c,14+8,(c->topdown) ? -c->h : c->h);
	set_int16(c,14+12,1); // planes
	set_int16(c,14+14,c->bpp);
	set_int32(c,14+16,c->compression);
	set_int32(c,14+20,c->bitssize); // biSizeImage
	set_int32(c,14+24,c->xpelspermeter);
	set_int32(c,14+28,c->ypelspermeter);
	set_int32(c,14+32,c->clr_used); // biClrUsed
	set_int32(c,14+36,0); // biClrImportant

	if(c->bmpversion>=4) {
		if(c->compression==3 && c->rgba) {
			set_uint32(c,14+40,c->bf_r);
			set_uint32(c,14+44,c->bf_g);
			set_uint32(c,14+48,c->bf_b);
			set_uint32(c,14+52,c->bf_a);
		}
		set_uint32(c,14+56,0x73524742); // CSType = sRGB
	}
	if(c->bmpversion>=5) {
		set_uint32(c,14+108,4); // Rendering intent = Perceptual
	}
}

static void make_bmp(struct context *c)
{
	write_bitfields(c);
	write_palette(c);
	write_bits(c);
	write_bitmapinfoheader(c);
	write_fileheader(c);
}

static int write_image_file(struct context *c)
{
	FILE *f;

	fprintf(stderr,"Writing %s\n",c->filename);
	f = fopen(c->filename,"wb");
	if(!f) {
		fprintf(stderr,"Can't write %s\n",c->filename);
		return 0;
	}
	fwrite(c->mem,1,c->mem_used,f);
	fclose(f);
	return 1;
}

static int make_bmp_file(struct context *c)
{
	make_bmp(c);
	if(!write_image_file(c)) return 0;
	return 1;
}

static void set_calculated_fields(struct context *c)
{
	if(c->bmpversion==5) {
		c->headersize = 124;
	}
	else {
		c->headersize = 40;
	}

	if(c->bpp==8 && c->pal_entries!=256) {
		c->clr_used = c->pal_entries;
	}
	else if(c->bpp==4 && c->pal_entries!=16) {
		c->clr_used = c->pal_entries;
	}
	else if(c->bpp==1 && c->pal_entries!=2) {
		c->clr_used = c->pal_entries;
	}
	else {
		c->clr_used = c->pal_entries;
	}

	c->palettesize = c->pal_entries*4;

	c->bitsoffset = 14 + c->headersize + c->bitfieldssize + c->palettesize;
}

static void defaultbmp(struct context *c)
{
	memset(c->mem,0,BMP_MAX_SIZE);
	c->mem_used = 0;
	c->w = 127;
	c->h = 64;
	c->bpp = 8;
	c->pal_entries = 252;
	c->clr_used = 0;
	c->bmpversion = 3;
	c->headersize = 40;
	c->bitssize = 0;
	c->bitfieldssize = 0;
	c->rowsize = 0;
	c->filename = "noname.bmp";
	c->compression = 0; // BI_RGB
	c->xpelspermeter = 2835; // = about 72dpi
	c->ypelspermeter = 2835;
	c->pal_wb = 0;
	c->pal_bg = 0;
	c->pal_p1 = 0;
	c->rgba = 0;
	c->dither = 0;
	c->topdown = 0;
	c->bf_r = c->bf_g = c->bf_b = c->bf_a = 0;
	c->nbits_r = c->nbits_g = c->nbits_b = c->nbits_a = 0;
	set_calculated_fields(c);
}

static int run(struct context *c)
{
	int retval = 0;

	mkdir("g",0755);
	mkdir("q",0755);
	mkdir("b",0755);

	defaultbmp(c);
	c->filename = "g/pal8.bmp";
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "q/pal8oversizepal.bmp";
	c->pal_entries = 300;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "b/pal8badindex.bmp";
	c->pal_entries = 100;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "q/pal8topdown.bmp";
	c->topdown = 1;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "g/pal4.bmp";
	c->bpp = 4;
	c->pal_entries = 12;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "g/pal1.bmp";
	c->bpp = 1;
	c->pal_entries = 2;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "g/pal1wb.bmp";
	c->bpp = 1;
	c->pal_entries = 2;
	c->pal_wb = 1;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "g/pal1bg.bmp";
	c->bpp = 1;
	c->pal_entries = 2;
	c->pal_bg = 1;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "q/pal1p1.bmp";
	c->bpp = 1;
	c->pal_entries = 1;
	c->pal_p1 = 1;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "g/rgb16.bmp";
	c->bpp = 16;
	c->nbits_r = c->nbits_g = c->nbits_b = 5;
	c->pal_entries = 0;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "g/rgb16-565.bmp";
	c->bpp = 16;
	c->pal_entries = 0;
	c->compression = 3;
	c->bf_r = 0x0000f800; c->nbits_r = 5;
	c->bf_g = 0x000007e0; c->nbits_g = 6;
	c->bf_b = 0x0000001f; c->nbits_b = 5;
	c->bitfieldssize = 12;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "q/rgba16-4444.bmp";
	c->bmpversion = 5;
	c->rgba = 1;
	c->bpp = 16;
	c->pal_entries = 0;
	c->compression = 3;
	c->bf_r = 0x00000f00; c->nbits_r = 4;
	c->bf_g = 0x000000f0; c->nbits_g = 4;
	c->bf_b = 0x0000000f; c->nbits_b = 4;
	c->bf_a = 0x0000f000; c->nbits_a = 4;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "q/rgb16-231.bmp";
	c->bpp = 16;
	c->pal_entries = 0;
	c->compression = 3;
	c->bf_r = 0x00000030; c->nbits_r = 2;
	c->bf_g = 0x0000000e; c->nbits_g = 3;
	c->bf_b = 0x00000001; c->nbits_b = 1;
	c->bitfieldssize = 12;
	c->dither = 1;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "g/rgb24.bmp";
	c->bpp = 24;
	c->pal_entries = 0;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "g/rgb24pal.bmp";
	c->bpp = 24;
	c->pal_entries = 17;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "g/rgb32.bmp";
	c->bpp = 32;
	c->pal_entries = 0;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "q/rgb32-111110.bmp";
	c->bpp = 32;
	c->compression = 3;
	c->pal_entries = 0;
	c->bf_r = 0xffe00000; c->nbits_r = 11;
	c->bf_g = 0x001ffc00; c->nbits_g = 11;
	c->bf_b = 0x000003ff; c->nbits_b = 10;
	c->bitfieldssize = 12;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	defaultbmp(c);
	c->filename = "q/rgba32.bmp";
	c->bmpversion = 5;
	c->bpp = 32;
	c->compression = 3; // BI_BITFIELDS
	c->bf_r = 0x00ff0000; c->bf_g = 0x0000ff00;
	c->bf_b = 0x000000ff; c->bf_a = 0xff000000;
	c->rgba = 1;
	c->pal_entries = 0;
	set_calculated_fields(c);
	if(!make_bmp_file(c)) goto done;

	retval = 1;
done:
	return retval;
}

int main(int argc, char **argv)
{
	struct context c;
	int ret;

	memset(&c,0,sizeof(struct context));
	c.mem = malloc(BMP_MAX_SIZE);
	ret = run(&c);

	return ret ? 0 : 1;
}
