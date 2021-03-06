/*
	Copyright (c) 2013 Kyle Davis, All Rights Reserved.

	Permission is hereby granted, free of charge, to any person
	obtaining a copy of this software and associated documentation
	files (the "Software"), to deal in the Software without
	restriction, including without limitation the rights to use,
	copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the
	Software is furnished to do so, subject to the following
	conditions:

	The above copyright notice and this permission notice shall be
	included in all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
	OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
	NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
	HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
	WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
	OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "global.h"
#include "stb_image.h"

#include "geom.h"
#include "rndr.h"
#include "anim.h"
#include "mapobj.h"
#include "player.h"
#include "level.h"

#define LIGHTGRAD 0.012
#define RNDRDIST 8192
uint32 *pixels;
float pplut [SWIDTH] = { 0 }; // LUT for angles pointing to points on the projection plane
float ppdist; // distance to projection plane center, from the player

float zbuf [SWIDTH] = { 0 }; // store wall distances, for rendering sprites
mapsprite *spritelist = NULL;

static float bobframes = 0.0, bobamt = 0.0;

static inline void rndr_setpixel (uint32 x, uint32 y, uint8 r, uint8 g, uint8 b)
{
	*(pixels + (y * SWIDTH) + x) = 0xff000000 | (r << 16) | (g << 8) | b;
}

static inline void rndr_darken (uint32 x, uint32 y, float r, float g, float b, float mul)
{
	rndr_setpixel (x, y, (uint8)(r * mul), (uint8)(g * mul), (uint8)(b * mul));
}

static inline void rndr_pixel (uint32 x, uint32 y, uint32 px, float mul)
{
	// stb_image has the red and blue swapped, compared to pixels
	rndr_darken (x, y, (px & 0x000000ff), (px & 0x0000ff00) >> 8, (px & 0x00ff0000) >> 16, mul);
}

static inline void rndr_clear (void)
{
	memset (pixels, 0, SWIDTH * SHEIGHT * 4);
}

void rndr_loadtex (texture *tx, animframe *frames, uint32 w, uint32 h)
{
	tx->pixels = (uint32*)stbi_load (tx->path, &(tx->w), &(tx->h), NULL, 4);

	// save real width, for grabbing a pixel
	tx->rw = tx->w;

	if (w || h)
	{
		tx->w = w;
		tx->h = h;
	}

	if (frames)
	{
		tx->frames = frames;
		tx->curframe = 0;
		tx->dur = tx->frames [0].dur;
	}
	else
		tx->frames = NULL; // no animation
}

uint8 rndr_texadvframe (texture *tx)
{
	if (!tx->frames || tx->dur == -1)
		return 0;

	if (--tx->dur == 0) // go to next frame
	{
		if (tx->frames [tx->curframe].func)
			tx->frames [tx->curframe].func ();

		tx->curframe = tx->frames [tx->curframe].next;
		if (tx->curframe == -1)
			return 1;

		tx->dur = tx->frames [tx->curframe].dur;
	}

	return 0;
}

void rndr_texsetframe (texture *tx, int8 frame)
{
	if (!tx->frames)
		return;

	tx->curframe = frame;
	tx->dur = tx->frames [tx->curframe].dur;;
}

void rndr_drawtex (texture *tx, uint32 x, uint32 y)
{
	int i, j, aoffx = 0, aoffy = 0;
	uint32 *pixel;

	// set offset if we have to, to draw the proper frame
	if (tx->frames)
	{
		aoffx = tx->w * tx->frames [tx->curframe].x;
		aoffy = tx->h * tx->frames [tx->curframe].y;
	}

	for (i = 0; i < tx->w; i++)
	for (j = 0; j < tx->h; j++)
	{
		pixel = tx->pixels + (j + aoffy) * tx->rw + (i + aoffx);
		if (i + x >= 0 && i + x < SWIDTH && j + y >= 0 && j + y < SHEIGHT && (*pixel & 0xff000000) >> 24 == 255)
			rndr_pixel (x + i, y + j, *pixel, 1.0);
	}

	rndr_texadvframe (tx);
}

void rndr_addsprite (texture *tx, point *p, uint8 wall)
{
	mapsprite *ms = malloc (sizeof (mapsprite));

	ms->p = p;
	memcpy (&(ms->tx), tx, sizeof (texture));
	ms->wall = wall;
	ms->clear = 0;
	ms->next = NULL;

	if (!spritelist)
		spritelist = ms;
	else
	{
		ms->next = spritelist;
		spritelist = ms;
	}
}

mapsprite *rndr_spritesort (mapsprite *list)
{
	mapsprite *sorted = NULL;

	while (list)
	{   
		mapsprite *head = list, **trail = &sorted;

		list = list->next;

		while (1) 
		{
			// move the head?
			if (!(*trail) || head->dist > (*trail)->dist)
			{
				head->next = *trail;
				*trail = head;
				break;
			}
			else
				trail = &(*trail)->next;
		}
	}

	return sorted;
}

// render texture, taking distance into account
void rndr_drawsprite (mapsprite *ms)
{
	if (ms->dist > RNDRDIST || ms->dist == 0)
		return;

	// translate point by -player [x,y]
	point tpoint = { ms->p->x - player.p.x, ms->p->y - player.p.y };

	// apply rotation matrix to tpoint
	point rpoint = { tpoint.x * cos (-player.angle) - tpoint.y * sin (-player.angle),
	                 tpoint.x * sin (-player.angle) + tpoint.y * cos (-player.angle) };

	if (rpoint.x < 0) // sprite is behind camera
		return;

	// calculate angle between player and p
	float ang = asinf (rpoint.y / ms->dist);

	// straight distance
	float sdist = ms->dist * cosf (-ang);

	if (sdist < 0)
		return;

	// using that angle, find screen coordinates
	float x = -(tanf (ang) * ppdist - (float)SWIDTH / 2.0);
	float y = 0.0;
	if (ms->wall)
	{
		if (x >= 0.0 && x < SWIDTH)
		{
			float colh = (128.0 / zbuf [(int)x]) * ppdist;
			y = (float)SHEIGHT / 2.0 - colh * (0.5 + bobamt) + colh * 0.67;
		}
	}
	else
		y = ((128.0 * (0.5 - bobamt)) / (sdist / ppdist)) + (float)SHEIGHT / 2.0;

	// now find the scale to render the sprite at
	float h = ((float)ms->tx.h / sdist) * ppdist;
	float ratio = (float)ms->tx.h / h;
	float w = (float)ms->tx.w / ratio;

	int xstart = x - w / 2, aoffx = 0;
	int ystart = y - h, aoffy = 0;
	uint32 *pixel;
	float dark = 1 / (sdist * LIGHTGRAD);
	dark = dark > 1 ? 1 : dark;

	if (ms->tx.frames) // we need to offset the section we render
	{
		aoffx = ms->tx.w * ms->tx.frames [ms->tx.curframe].x;
		aoffy = ms->tx.h * ms->tx.frames [ms->tx.curframe].y;
	}

	int i, j;
	for (i = xstart; i < x + w / 2; i++)
	{
		// don't render off screen
		if (i < 0 || i > SWIDTH - 1)
			continue;

		// don't render if there's a wall in front
		if (sdist > zbuf [i])
			continue;

		// calculate texture x from scaled x
		uint32 offsx = (float)(i - xstart) * ratio;

		for (j = ystart; j < y; j++)
		{
			if (j < 0 || j > SHEIGHT - 1)
				continue;

			uint32 offsy = (float)(j - ystart) * ratio;

			// find pixel, and render it
			pixel = ms->tx.pixels + (offsy + aoffy) * ms->tx.rw + (offsx + aoffx);
			if ((*pixel & 0xff000000) >> 24 == 255)
				rndr_pixel (i, j, *pixel, dark);
		}
	}

	if (rndr_texadvframe (&(ms->tx)))
	{
		ms->clear = 1;
	}
}

texture walltex = { "res/textures/lower_normal.png" };
texture floortex = { "res/textures/lower_floor.png" };
texture guntex = { "res/hud/gun.png" };
texture plastex = { "res/objects/effects/plasma.png" };
texture zombietex = { "res/objects/enemy/zombie1.png" };

void rndr_prepare (void)
{
	int i;

	pixels = malloc (4 * SWIDTH * SHEIGHT);

	ppdist = ((float)SWIDTH / 2.0) / tanf (FOV / 2.0);

	for (i = 0; i < SWIDTH; i++)
		pplut [i] = atanf ((((float)SWIDTH / 2.0 - i)) / ppdist);

	rndr_loadtex (&walltex, NULL, 0, 0);
	rndr_loadtex (&floortex, NULL, 0, 0);
	rndr_loadtex (&guntex, gun_frames, 64, 64);
	rndr_loadtex (&plastex, plasma_frames, 32, 32);
	rndr_loadtex (&zombietex, NULL, 64, 64);
}

// render wall section
// returns bottom pixel
uint32 rndr_column (float dist, uint32 x, line *ray, line *wall, point in)
{
	int i;
	int32 offsx, offsy;
	float colh = (128.0 / dist) * ppdist;
	float hratio = 128.0 / colh;
	int colstart = SHEIGHT / 2 - colh * (0.5 + bobamt);
	float dark = 1 / (dist * LIGHTGRAD);
	dark = dark > 1 ? 1 : dark;

	offsx = distcalc (wall->a, in);
	offsx %= walltex.w;

	for (i = colstart; i < SHEIGHT / 2 + colh * (0.5 - bobamt); i++)
	{
		if (i < 0 || i >= SHEIGHT)
			continue;

		offsy = (float)(i - colstart) * hratio;
		rndr_pixel (x, i, *(walltex.pixels + (offsy * walltex.rw) + offsx), dark);
	}

	return i;
}

// render floor
void rndr_floor (uint32 x, uint32 start)
{
	// TODO: maybe save straight distances in a LUT, speeding up the calculation
	int i;
	float sdist, adist;
	float dark;

	// precalc these
	float pplutcos = cosf (-pplut [x]);
	float plcos = cosf (player.angle + pplut [x]);
	float plsin = sinf (player.angle + pplut [x]);

	// coords to the pixel
	int32 pxx, pxy;

	for (i = start; i < SHEIGHT; i++)
	{
		sdist = ppdist * ((128.0 * (0.5 - bobamt)) / (i - SHEIGHT / 2));
		adist = sdist / pplutcos;

		// world coordinates:
		pxx = player.p.x + plcos * adist;
		pxy = player.p.y + plsin * adist;

		dark = 1 / (sdist * LIGHTGRAD);
		dark = dark > 1 ? 1 : dark;
		rndr_pixel (x, i, *(floortex.pixels + ((pxy % walltex.h) * walltex.rw) + (pxx % walltex.rw)), dark);
	}
}

static inline float rndr_col_distcalc (uint32 x, line *ray, point in)
{
	return distcalc (ray->a, in) * cosf (-pplut [x]);
}

extern uint16 frametimes [48];
extern uint8 renderdebug;
extern uint32 ticktime;

void rndr_dorndr (void)
{
	int i;
	uint32 lx, ly, colend;
	line ray;
	point in;
	tile *t;
	linelist *it;

	line *best;
	point bestpt;
	float bestdist, compdist;

	rndr_clear ();

	// calculate bob amount
	if (player.momx == 0.0 && player.momy == 0.0)
	{
		bobframes = 0.0;
		if (fabs (bobamt) > 0.0001)
			bobamt -= bobamt / 10;
	}
	else
	{
		bobframes += 0.1;
		bobamt = sinf (bobframes) * 0.05;
	}
	ray.a.x = player.p.x;
	ray.a.y = player.p.y;

	for (i = 0; i < SWIDTH; i++)
	{
		bestdist = RNDRDIST;
		best = NULL;

		// cast a ray for every screen pixel
		// end point will fall off in the distance
		ray.b.x = ray.a.x + cosf (player.angle + pplut [i]) * RNDRDIST;
		ray.b.y = ray.a.y + sinf (player.angle + pplut [i]) * RNDRDIST;

		// render each line visible from where the player is
		lx = (uint32)(player.p.x / 256.0);
		ly = (uint32)(player.p.y / 256.0);
		it = level [lx] [ly].visible;
		while (it)
		{
			if (intersect (&ray, it->l, &in))
				compdist = rndr_col_distcalc (i, &ray, in);
			else
			{
				it = it->next;
				continue;
			}

			if (compdist < bestdist)
			{
				bestdist = compdist;
				best = it->l;
				bestpt = in;
			}

			it = it->next;
		}

		// render best choice
		zbuf [i] = bestdist;
		if (best)
		{
			colend = rndr_column (bestdist, i, &ray, best, bestpt);
			rndr_floor (i, colend);
		}
		else
			rndr_floor (i, SHEIGHT / 2);
	}

	// render sprites
	// update distances:
	mapsprite *sit = spritelist, *prev = NULL;
	while (sit)
	{
		sit->dist = distcalc (player.p, *(sit->p));
		sit = sit->next;
	}

	// sort by distance
	sit = spritelist = rndr_spritesort (spritelist);

	// cycle and render
	while (sit)
	{
		if (sit->clear) // this sprite was marked to get removed from the list, so don't render it, just free it.
		{
			if (prev)
				prev->next = sit->next;
			else // sit was head
				spritelist = sit->next;
			free (sit);
			sit = prev ? prev->next : NULL;
			continue;
		}

		rndr_drawsprite (sit);
		prev = sit;
		sit = sit->next;
	}

	// draw hud stuff
	rndr_drawtex (&guntex, SWIDTH / 2 - 32 + (sinf (bobframes / 2) * 8.0), SHEIGHT - 60 + (bobamt * 80.0));

	// draw debug stuff (render times)
	for (i = 0; renderdebug && i < 48; i++)
		if (frametimes [i] < SHEIGHT)
			rndr_setpixel (i, SHEIGHT - 1 - frametimes [i], 200, frametimes [i] < ticktime ? 200 : 0, 0);
}
