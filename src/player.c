#include <math.h>

#include "sdl.h"

#include "global.h"
#include "xor.h"

#include "input.h"
#include "geom.h"
#include "mapobj.h"
#include "level.h"
#include "player.h"
#include "rndr.h"

mapobj player = { { 64, 64 }, 0.0, 32.0, player_logic };
mapobj testcoll = { { 64, 256 }, 0.0, 32.0 };

extern texture guntex;
void player_logic (mapobj *self)
{
	self->momx = self->momy = 0.0;
	if (input_keydown ("w"))
	{
		self->momx += cosf (self->angle) * 3;
		self->momy += sinf (self->angle) * 3;
	}

	if (input_keydown ("s"))
	{
		self->momx -= cosf (self->angle) * 3;
		self->momy -= sinf (self->angle) * 3;
	}

	if (input_keydown ("a"))
	{
		self->momx += cosf (self->angle + M_PI / 2) * 3;
		self->momy += sinf (self->angle + M_PI / 2) * 3;
	}

	if (input_keydown ("d"))
	{
		self->momx += cosf (self->angle - M_PI / 2) * 3;
		self->momy += sinf (self->angle - M_PI / 2) * 3;
	}

	// start firing animation if we need to
	if (input_mousedown (1) && guntex.curframe == 0)
		rndr_texsetframe (&guntex, 1);

	self->p.x += self->momx;
	self->p.y += self->momy;

	if (input_keydown ("left"))
	{
		self->angle += 0.03;
	}

	if (input_keydown ("right"))
	{
		self->angle -= 0.03;
	}

	// collision detection
	int32 lx, ly, i;
	tile *t;
	lx = (int32)(self->p.x / 256.0);
	ly = (int32)(self->p.y / 256.0);
	t = &(level [lx] [ly]);
	linelist *it = t->visible;

	while (it)
	{
		if (it->l)
			mapobj_line_coll (self, it->l);

		it = it->next;
	}

	//mapobj_obj_coll (self, &testcoll);

	mapobj_correct_coll (self);
}

#define MSENS 0.004
void player_turn (mapobj *self, int32 amt)
{
	self->angle -= amt * MSENS;
}

#define SHOTGUN 50
void player_shoot (void)
{
	int i;
	mapobj_shoot (&player, player.angle + fxrand () * 0.1 - 0.05);
}
