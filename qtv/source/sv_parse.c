/*
	#FILENAME#

	#DESCRIPTION#

	Copyright (C) 2004 #AUTHOR#

	Author: #AUTHOR#
	Date: #DATE#

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

*/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static __attribute__ ((unused)) const char rcsid[] =
	"$Id$";

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "QF/cmd.h"
#include "QF/console.h"
#include "QF/dstring.h"
#include "QF/hash.h"
#include "QF/idparse.h"
#include "QF/info.h"
#include "QF/msg.h"
#include "QF/qendian.h"
#include "QF/sys.h"
#include "QF/va.h"

#include "qw/msg_ucmd.h"
#include "qw/protocol.h"

#include "client.h"
#include "connection.h"
#include "qtv.h"
#include "server.h"

static void
sv_broadcast (server_t *sv, int reliable, byte *msg, int len)
{
	client_t   *cl;
	byte        svc;

	if (len < 1)
		return;
	svc = *msg++;
	len--;
	for (cl = sv->clients; cl; cl = cl->next) {
		if (reliable) {
			MSG_ReliableWrite_Begin (&cl->backbuf, svc, len + 1);
			MSG_ReliableWrite_SZ (&cl->backbuf, msg, len);
		} else {
			MSG_WriteByte (&cl->datagram, svc);
			SZ_Write (&cl->datagram, msg, len);
		}
	}
}

static void
sv_serverdata (server_t *sv, qmsg_t *msg)
{
	const char *str;

	sv->ver = MSG_ReadLong (msg);
	sv->spawncount = MSG_ReadLong (msg);
	sv->gamedir = strdup (MSG_ReadString (msg));

	sv->message = strdup (MSG_ReadString (msg));
	sv->movevars.gravity = MSG_ReadFloat (msg);
	sv->movevars.stopspeed = MSG_ReadFloat (msg);
	sv->movevars.maxspeed = MSG_ReadFloat (msg);
	sv->movevars.spectatormaxspeed = MSG_ReadFloat (msg);
	sv->movevars.accelerate = MSG_ReadFloat (msg);
	sv->movevars.airaccelerate = MSG_ReadFloat (msg);
	sv->movevars.wateraccelerate = MSG_ReadFloat (msg);
	sv->movevars.friction = MSG_ReadFloat (msg);
	sv->movevars.waterfriction = MSG_ReadFloat (msg);
	sv->movevars.entgravity = MSG_ReadFloat (msg);

	sv->cdtrack = MSG_ReadByte (msg);
	sv->sounds = MSG_ReadByte (msg);

	COM_TokenizeString (MSG_ReadString (msg), qtv_args);
	cmd_args = qtv_args;
	Info_Destroy (sv->info);
	sv->info = Info_ParseString (Cmd_Argv (1), MAX_SERVERINFO_STRING, 0);

	str = Info_ValueForKey (sv->info, "hostname");
	if (strcmp (str, "unnamed"))
		qtv_printf ("%s: %s\n", sv->name, str);
	str = Info_ValueForKey (sv->info, "*version");
	qtv_printf ("%s: QW %s\n", sv->name, str);
	str = Info_ValueForKey (sv->info, "*qf_version");
	if (str[0])
		qtv_printf ("%s: QuakeForge %s\n", sv->name, str);
	qtv_printf ("%s: gamedir: %s\n", sv->name, sv->gamedir);
	str = Info_ValueForKey (sv->info, "map");
	qtv_printf ("%s: (%s) %s\n", sv->name, str, sv->message);

	MSG_WriteByte (&sv->netchan.message, qtv_stringcmd);
	MSG_WriteString (&sv->netchan.message,
					 va ("soundlist %i %i", sv->spawncount, 0));
	sv->next_run = realtime;
}

static void
sv_soundlist (server_t *sv, qmsg_t *msg)
{
	int         numsounds = MSG_ReadByte (msg);
	int         n;
	const char *str;

	for (;;) {
		str = MSG_ReadString (msg);
		if (!str[0])
			break;
		//qtv_printf ("%s\n", str);
		numsounds++;
		if (numsounds == MAX_SOUNDS) {
			while (str[0])
				str = MSG_ReadString (msg);
			MSG_ReadByte (msg);
			return;
		}
		// save sound name
	}
	n = MSG_ReadByte (msg);
	if (n) {
		MSG_WriteByte (&sv->netchan.message, qtv_stringcmd);
		MSG_WriteString (&sv->netchan.message,
						 va ("soundlist %d %d", sv->spawncount, n));
	} else {
		MSG_WriteByte (&sv->netchan.message, qtv_stringcmd);
		MSG_WriteString (&sv->netchan.message,
						 va ("modellist %d %d", sv->spawncount, 0));
	}
	sv->next_run = realtime;
}

static void
sv_modellist (server_t *sv, qmsg_t *msg)
{
	int         nummodels = MSG_ReadByte (msg);
	int         n;
	const char *str;

	for (;;) {
		str = MSG_ReadString (msg);
		if (!str[0])
			break;
		//qtv_printf ("%s\n", str);
		nummodels++;
		if (nummodels == MAX_SOUNDS) {
			while (str[0])
				str = MSG_ReadString (msg);
			MSG_ReadByte (msg);
			return;
		}
		// save sound name
	}
	n = MSG_ReadByte (msg);
	if (n) {
		MSG_WriteByte (&sv->netchan.message, qtv_stringcmd);
		MSG_WriteString (&sv->netchan.message,
						 va ("modellist %d %d", sv->spawncount, n));
	} else {
		MSG_WriteByte (&sv->netchan.message, qtv_stringcmd);
		MSG_WriteString (&sv->netchan.message,
						 va ("prespawn %d 0 0", sv->spawncount));
	}
	sv->next_run = realtime;
}

static void
sv_cmd_f (server_t *sv)
{
	if (Cmd_Argc () > 1) {
		MSG_WriteByte (&sv->netchan.message, qtv_stringcmd);
		SZ_Print (&sv->netchan.message, Cmd_Args (1));
	}
	sv->next_run = realtime;
}

static void
sv_skins_f (server_t *sv)
{
	int         i;
	// we don't actually bother checking skins here, but this is a good way
	// to get everything ready at the last miniute before we start getting
	// actual in-game update messages
	MSG_WriteByte (&sv->netchan.message, qtv_stringcmd);
	MSG_WriteString (&sv->netchan.message, va ("begin %d", sv->spawncount));
	sv->next_run = realtime;
	sv->connected = 2;
	sv->delta = -1;

	for (i = 0; i < UPDATE_BACKUP; i++) {
		sv->frames[i].entities.entities = sv->entity_states[i];
		sv->frames[i].players.players = sv->player_states[i];
	}
}

typedef struct {
	const char *name;
	void      (*func) (server_t *sv);
} svcmd_t;

svcmd_t svcmds[] = {
	{"cmd",			sv_cmd_f},
	{"skins",		sv_skins_f},

	{0,				0},
};

void
sv_stringcmd (server_t *sv, qmsg_t *msg)
{
	svcmd_t    *c;
	const char *name;

	COM_TokenizeString (MSG_ReadString (msg), qtv_args);
	cmd_args = qtv_args;
	name = Cmd_Argv (0);

	for (c = svcmds; c->name; c++)
		if (strcmp (c->name, name) == 0)
			break;
	if (!c->name) {
		qtv_printf ("Bad QTV command: %s\n", name);
		return;
	}
	c->func (sv);
}

static void
sv_parse_delta (qmsg_t *msg, int bits, entity_state_t *ent)
{
	ent->number = bits & 511;
	bits &= ~511;

	if (bits & U_MOREBITS)
		bits |= MSG_ReadByte (msg);
	if (bits & U_EXTEND1) {
		bits |= MSG_ReadByte (msg) << 16;
		if (bits & U_EXTEND2)
			bits |= MSG_ReadByte (msg) << 24;
	}
	if (bits & U_MODEL)
		ent->modelindex = MSG_ReadByte (msg);
	if (bits & U_FRAME)
		ent->frame = (ent->frame & 0xff00) | MSG_ReadByte (msg);
	if (bits & U_COLORMAP)
		ent->colormap = MSG_ReadByte (msg);
	if (bits & U_SKIN)
		ent->skinnum = MSG_ReadByte (msg);
	if (bits & U_EFFECTS)
		ent->effects = (ent->effects & 0xff00) | MSG_ReadByte (msg);
	if (bits & U_ORIGIN1)
		ent->origin[0] = MSG_ReadCoord (msg);
	if (bits & U_ANGLE1)
		ent->angles[0] = MSG_ReadAngle (msg);
	if (bits & U_ORIGIN2)
		ent->origin[1] = MSG_ReadCoord (msg);
	if (bits & U_ANGLE2)
		ent->angles[1] = MSG_ReadAngle (msg);
	if (bits & U_ORIGIN3)
		ent->origin[2] = MSG_ReadCoord (msg);
	if (bits & U_ANGLE3)
		ent->angles[2] = MSG_ReadAngle (msg);
	if (bits & U_SOLID) {
		// FIXME
	}
	if (!(bits & U_EXTEND1))
		return;
	if (bits & U_ALPHA)
		ent->alpha = MSG_ReadByte (msg);
	if (bits & U_SCALE)
		ent->scale = MSG_ReadByte (msg);
	if (bits & U_EFFECTS2)
		ent->effects = (ent->effects & 0x00ff) | (MSG_ReadByte (msg) << 8);
	if (bits & U_GLOWSIZE)
		ent->glow_size = MSG_ReadByte (msg);
	if (bits & U_GLOWCOLOR)
		ent->glow_color = MSG_ReadByte (msg);
	if (bits & U_COLORMOD)
		ent->colormod = MSG_ReadByte (msg);
	if (!(bits & U_EXTEND1))
		return;
	if (bits & U_FRAME2)
		ent->frame = (ent->frame & 0x00ff) | (MSG_ReadByte (msg) << 8);
}

static void
sv_packetentities (server_t *sv, qmsg_t *msg, int delta)
{
	unsigned short word;
	int         newnum, oldnum, from;
	int         newindex, oldindex;
	int         newpacket, oldpacket;
	int         full;
	packet_entities_t *oldp, *newp, dummy;

	newpacket = sv->netchan.incoming_sequence & UPDATE_MASK;
	newp = &sv->frames[newpacket].entities;
	sv->frames[newpacket].invalid = false;

	if (delta) {
		from = MSG_ReadByte (msg);
		oldpacket = sv->frames[newpacket].delta_sequence;
		if ((from & UPDATE_MASK) != (oldpacket & UPDATE_MASK))
			qtv_printf ("WARNING: from mismatch\n");
	} else {
		oldpacket = -1;
	}
	full = 0;
	if (oldpacket != -1) {
		if (sv->netchan.outgoing_sequence - oldpacket > UPDATE_BACKUP) {
			//XXX flush_entity_packet ();
			return;
		}
		oldp = &sv->frames[oldpacket & UPDATE_MASK].entities;
	} else {
		oldp = &dummy;
		dummy.num_entities = 0;
		full = 1;
	}
	//qtv_printf ("newp = %-5d oldp = %d\n", newpacket, oldpacket & UPDATE_MASK);
	sv->delta = sv->netchan.incoming_sequence;
	newindex = oldindex = 0;
	newp->num_entities = 0;
	while (1) {
		word = MSG_ReadShort (msg);
		if (msg->badread) {     // something didn't parse right...
			qtv_printf ("msg_badread in packetentities\n");
			return;
		}
		//qtv_printf ("word = %04x new = %-3d old = %-3d\n", word, newindex, oldindex);
		if (!word) {
			// copy rest of ents from old packet
			while (oldindex < oldp->num_entities) {
				if (newindex >= MAX_DEMO_PACKET_ENTITIES) {
					qtv_printf ("A too many packet entities\n");
					Sys_Quit ();
					//XXX flush_entitiy_packet
					return;
				}
				newp->entities[newindex] = oldp->entities[oldindex];
				newnum = newp->entities[newindex].number;
				sv->entities[newnum] = newp->entities[newindex];
				newindex++;
				oldindex++;
			}
			break;
		}
		newnum = word & 511;
		oldnum = 9999;
		if (oldindex < oldp->num_entities)
			oldnum = oldp->entities[oldindex].number;
		//qtv_printf ("    %-3d %-4d %3d\n", newnum, oldnum, oldp->num_entities);

		while (newnum > oldnum) {
			if (full) {
				qtv_printf ("WARNING: oldcopy on full update\n");
				//XXX flush_entitiy_packet
				return;
			}
			if (newindex >= MAX_DEMO_PACKET_ENTITIES) {
				qtv_printf ("B too many packet entities\n");
				Sys_Quit ();
				//XXX flush_entitiy_packet
				return;
			}
			newp->entities[newindex] = oldp->entities[oldindex];
			sv->entities[newnum] = newp->entities[newindex];
			newindex++;
			oldindex++;
			oldnum = 9999;
			if (oldindex < oldp->num_entities)
				oldnum = oldp->entities[oldindex].number;
		//qtv_printf ("    %-3d %-4d %3d\n", newnum, oldnum, oldp->num_entities);
		}

		if (newnum < oldnum) {
			if (word & U_REMOVE) {
				if (full) {
					sv->delta = 0;	//XXX -1?
					qtv_printf ("WARNING: U_REMOVE on full update\n");
					//XXX flush_entitiy_packet
					return;
				}
				continue;
			}
			if (newindex >= MAX_DEMO_PACKET_ENTITIES) {
				qtv_printf ("C too many packet entities\n");
				Sys_Quit ();
				//XXX flush_entitiy_packet
				return;
			}
			newp->entities[newindex] = sv->baselines[newnum];
			sv_parse_delta (msg, word, &newp->entities[newindex]);
			sv->entities[newnum] = newp->entities[newindex];
			newindex++;
			continue;
		}
		if (newnum == oldnum) {
			if (full) {
				sv->delta = 0;	//XXX -1?
				qtv_printf ("WARNING: delta on full update\n");
			}
			if (word & U_REMOVE) {
				memset (&sv->entities[newnum], 0, sizeof (entity_state_t));
				oldindex++;
				continue;
			}
			newp->entities[newindex] = oldp->entities[oldindex];
			sv_parse_delta (msg, word, &sv->entities[newindex]);
			sv->entities[newnum] = newp->entities[newindex];
			newindex++;
			oldindex++;
		}
	}
	newp->num_entities = newindex;
}

static void
sv_playerinfo (server_t *sv, qmsg_t *msg)
{
	player_t    *pl;
	plent_state_t dummy;				// for bad player indices
	plent_state_t *ent;
	int          num, flags;
	int          i;

	num = MSG_ReadByte (msg);
	if (num > MAX_SV_PLAYERS) {
		qtv_printf ("bogus player: %d\n", num);
		ent = &dummy;
	} else {
		pl = &sv->players[num];
		ent = &pl->ent;
	}
	flags = ent->flags = MSG_ReadShort (msg);
	//qtv_printf ("%2d %x\n", num, flags);
	MSG_ReadCoordV (msg, ent->origin);
	ent->frame = (ent->frame & 0xff00) | MSG_ReadByte (msg);
	if (flags & PF_MSEC)
		ent->msec = MSG_ReadByte (msg);
	if (flags & PF_COMMAND)
		MSG_ReadDeltaUsercmd (msg, &nullcmd, &ent->cmd);
	for (i = 0; i < 3; i++) {
		if (flags & (PF_VELOCITY1 << i))
			ent->velocity[i] = MSG_ReadShort (msg);
	}
	if (flags & PF_MODEL)
		ent->modelindex = MSG_ReadByte (msg);
	if (flags & PF_SKINNUM)
		ent->skinnum = MSG_ReadByte (msg);
	if (flags & PF_EFFECTS)
		ent->effects = (ent->effects & 0xff00) | MSG_ReadByte (msg);;
	if (flags & PF_WEAPONFRAME)
		ent->weaponframe = MSG_ReadByte (msg);
	if (flags & PF_QF) {
		int         bits;

		bits = MSG_ReadByte (msg);
		if (bits & PF_ALPHA)
			ent->alpha = MSG_ReadByte (msg);
		if (bits & PF_SCALE)
			ent->scale = MSG_ReadByte (msg);
		if (bits & PF_EFFECTS2)
			ent->effects = (ent->effects & 0x00ff)
						 | (MSG_ReadByte (msg) << 8);
		if (bits & PF_GLOWSIZE)
			ent->glow_size = MSG_ReadByte (msg);
		if (bits & PF_GLOWCOLOR)
			ent->glow_color = MSG_ReadByte (msg);
		if (bits & PF_COLORMOD)
			ent->colormod = MSG_ReadByte (msg);
		if (bits & PF_FRAME2)
			ent->frame = (ent->frame & 0xff)
					   | (MSG_ReadByte (msg) << 8);
	}
}

static void
sv_serverinfo (server_t *sv, qmsg_t *msg)
{
	dstring_t  *key = dstring_newstr ();
	dstring_t  *value = dstring_newstr ();

	dstring_copystr (key, MSG_ReadString (msg));
	dstring_copystr (value, MSG_ReadString (msg));

	Info_SetValueForKey (sv->info, key->str, value->str, 0);

	dstring_delete (key);
	dstring_delete (value);
}

static void
sv_setinfo (server_t *sv, qmsg_t *msg)
{
	int         slot;
	dstring_t  *key = dstring_newstr ();
	dstring_t  *value = dstring_newstr ();
	player_t   *pl;

	slot = MSG_ReadByte (msg);
	dstring_copystr (key, MSG_ReadString (msg));
	dstring_copystr (value, MSG_ReadString (msg));
	if (slot >= MAX_SV_PLAYERS) {
		qtv_printf ("bogus player: %d\n", slot);
	} else {
		pl = sv->players + slot;
		if (!pl->info)
			pl->info = Info_ParseString ("", MAX_INFO_STRING,
										 0);
		Info_SetValueForKey (pl->info, key->str, value->str,
							 0);
	}
	dstring_delete (key);
	dstring_delete (value);
}

static void
sv_updateuserinfo (server_t *sv, qmsg_t *msg)
{
	int         slot, uid;
	const char *info;
	player_t   *pl;

	slot = MSG_ReadByte (msg);
	uid = MSG_ReadLong (msg);
	info = MSG_ReadString (msg);
	if (slot >= MAX_SV_PLAYERS) {
		qtv_printf ("bogus player: %d\n", slot);
		return;
	}
	pl = sv->players + slot;
	if (pl->info)
		Info_Destroy (pl->info);
	if (info) {
		pl->info = Info_ParseString (info, MAX_INFO_STRING, 0);
		pl->uid = uid;
	}
}

static void
sv_updatestat (server_t *sv, qmsg_t *msg, int islong)
{
	int         stat, val;
	player_t   *pl;

	stat = MSG_ReadByte (msg);
	if (!islong)
		val = MSG_ReadByte (msg);
	else
		val = MSG_ReadLong (msg);
	for (pl = sv->players; pl; pl = pl->next)
		pl->stats[stat] = val;
}

static void
sv_update_net (server_t *sv, qmsg_t *msg, int ping)
{
	int         slot, val;
	player_t   *pl;

	slot = MSG_ReadByte (msg);
	if (ping)
		val = MSG_ReadShort (msg);
	else
		val = MSG_ReadByte (msg);
	if (slot >= MAX_SV_PLAYERS) {
		qtv_printf ("bogus player: %d\n", slot);
		return;
	}
	pl = sv->players + slot;
	if (ping)
		pl->ping = val;
	else
		pl->pl = val;
}

static void
sv_sound (server_t *sv, qmsg_t *msg, int stop)
{
	// XXX
	int         c;
	vec3_t      v;

	if (stop) {
		MSG_ReadShort (msg);
	} else {
		c = MSG_ReadShort (msg);
		if (c & SND_VOLUME)
			MSG_ReadByte (msg);
		if (c & SND_ATTENUATION)
			MSG_ReadByte (msg);
		MSG_ReadByte (msg);
		MSG_ReadCoordV (msg, v);
	}
}

static void
sv_setangle (server_t *sv, qmsg_t *msg)
{
	int         slot;
	player_t   *pl;
	vec3_t      ang;

	slot = MSG_ReadByte (msg);
	MSG_ReadAngleV (msg, ang);
	if (slot >= MAX_SV_PLAYERS) {
		qtv_printf ("bogus player: %d\n", slot);
		return;
	}
	pl = sv->players + slot;
	VectorCopy (ang, pl->ent.cmd.angles);
}

static void
sv_updatefrags (server_t *sv, qmsg_t *msg)
{
	int         slot, frags;
	player_t   *pl;

	slot = MSG_ReadByte (msg);
	frags = MSG_ReadShort (msg);
	if (slot >= MAX_SV_PLAYERS) {
		qtv_printf ("bogus player: %d\n", slot);
		return;
	}
	pl = sv->players + slot;
	pl->frags = frags;
}

static void
parse_baseline (qmsg_t *msg, entity_state_t *ent)
{
	ent->modelindex = MSG_ReadByte (msg);
	ent->frame = MSG_ReadByte (msg);
	ent->colormap = MSG_ReadByte (msg);
	ent->skinnum = MSG_ReadByte (msg);
	MSG_ReadCoordAngleV (msg, ent->origin, ent->angles);
	ent->colormod = 255;
	ent->alpha = 255;
	ent->scale = 16;
	ent->glow_size = 254;
	ent->glow_color = 254;
}

static void
sv_spawnbaseline (server_t *sv, qmsg_t *msg)
{
	int         i;

	i = MSG_ReadShort (msg) % MAX_SV_ENTITIES;
	sv->baselines[i].number = i;
	parse_baseline (msg, &sv->baselines[i]);
}

static void
sv_spawnstatic (server_t *sv, qmsg_t *msg)
{
	entity_state_t ent;
	parse_baseline (msg, &ent);
}

static void
parse_beam (qmsg_t *msg)
{
	vec3_t      start, end;

	MSG_ReadShort (msg);
	MSG_ReadCoordV (msg, start);
	MSG_ReadCoordV (msg, end);
}

static void
sv_temp_entity (server_t *sv, qmsg_t *msg)
{
	vec3_t      pos;
	int         type;

	type = MSG_ReadByte (msg);
	switch (type) {
		case TE_WIZSPIKE:
			MSG_ReadCoordV (msg, pos);
			break;
		case TE_KNIGHTSPIKE:
			MSG_ReadCoordV (msg, pos);
			break;
		case TE_SPIKE:
			MSG_ReadCoordV (msg, pos);
			break;
		case TE_SUPERSPIKE:
			MSG_ReadCoordV (msg, pos);
			break;
		case TE_EXPLOSION:
			MSG_ReadCoordV (msg, pos);
			break;
		case TE_TAREXPLOSION:
			MSG_ReadCoordV (msg, pos);
			break;
		case TE_LIGHTNING1:
		case TE_LIGHTNING2:
		case TE_LIGHTNING3:
		case TE_BEAM:
			parse_beam (msg);
			break;
		case TE_LAVASPLASH:
			MSG_ReadCoordV (msg, pos);
			break;
		case TE_TELEPORT:
			MSG_ReadCoordV (msg, pos);
			break;
		case TE_EXPLOSION2:
			MSG_ReadCoordV (msg, pos);
			MSG_ReadByte (msg);
			MSG_ReadByte (msg);
			break;
		case TE_GUNSHOT:
			MSG_ReadByte (msg);
			MSG_ReadCoordV (msg, pos);
			break;
		case TE_BLOOD:
			MSG_ReadByte (msg);
			MSG_ReadCoordV (msg, pos);
			break;
		case TE_LIGHTNINGBLOOD:
			MSG_ReadCoordV (msg, pos);
			break;
	}
}

static void
sv_nails (server_t *sv, qmsg_t *msg, int nails2)
{
	int         c, i;
	byte        bits[6];

	c = MSG_ReadByte (msg);
	for (i = 0; i < c; i++) {
		if (nails2)
			MSG_ReadByte (msg);
		MSG_ReadBytes (msg, bits, 6);
	}
}

static void
sv_print (server_t *sv, qmsg_t *msg)
{
	byte       *data;
	int         len;

	len = msg->readcount - 1;
	data = msg->message->data + len;
	MSG_ReadByte (msg);
	qtv_printf ("%s", MSG_ReadString (msg));
	len = msg->readcount - len;
	sv_broadcast (sv, 1, data, len);
}

void
sv_parse (server_t *sv, qmsg_t *msg, int reliable)
{
	int         svc;
	vec3_t      v;
	player_t   *pl;

	while (1) {
		svc = MSG_ReadByte (msg);
		if (svc == -1)
			break;
		//qtv_printf ("sv_parse: svc: %d\n", svc);
		switch (svc) {
			default:
				qtv_printf ("sv_parse: unknown svc: %d\n", svc);
				Sys_Quit ();
				return;
			case svc_nop:
				break;
			//case svc_setview:
			//	break;
			case svc_sound:
				sv_sound (sv, msg, 0);
				break;
			case svc_print:
				sv_print (sv, msg);
				break;
			case svc_setangle:
				sv_setangle (sv, msg);
				break;
			case svc_updatefrags:
				sv_updatefrags (sv, msg);
				break;
			case svc_stopsound:
				sv_sound (sv, msg, 1);
				break;
			case svc_damage:
				//XXX
				MSG_ReadByte (msg);
				MSG_ReadByte (msg);
				MSG_ReadCoordV (msg, v);
				break;
			case svc_temp_entity:
				sv_temp_entity (sv, msg);
				//XXX
				break;
			case svc_setpause:
				//XXX
				MSG_ReadByte (msg);
				break;
			case svc_centerprint:
				//XXX
				MSG_ReadString (msg);
				break;
			case svc_killedmonster:
				for (pl = sv->players; pl; pl = pl->next)
					pl->stats[14]++;	//FIXME STAT_MONSTERS
				break;
			case svc_foundsecret:
				for (pl = sv->players; pl; pl = pl->next)
					pl->stats[13]++;	//FIXME STAT_SECRETS
				break;
			case svc_intermission:
				//XXX
				MSG_ReadCoordV (msg, v);
				MSG_ReadAngleV (msg, v);
				break;
			case svc_finale:
				//XXX
				MSG_ReadString (msg);
				break;
			case svc_cdtrack:
				//XXX
				MSG_ReadByte (msg);
				break;
			case svc_sellscreen:
				//ignore
				break;
			case svc_smallkick:
				//XXX
				break;
			case svc_bigkick:
				//XXX
				break;
			case svc_updateentertime:
				//XXX
				MSG_ReadByte (msg);
				MSG_ReadFloat (msg);
				break;
			case svc_updatestat:
			case svc_updatestatlong:
				sv_updatestat (sv, msg, svc == svc_updatestatlong);
				break;
			case svc_muzzleflash:
				//XXX
				MSG_ReadShort (msg);
				break;
			case svc_updateuserinfo:
				sv_updateuserinfo (sv, msg);
				break;
			case svc_playerinfo:
				sv_playerinfo (sv, msg);
				break;
			case svc_nails:
			case svc_nails2:
				sv_nails (sv, msg, svc == svc_nails2);
				break;
			case svc_packetentities:
				sv_packetentities (sv, msg, 0);
				break;
			case svc_deltapacketentities:
				sv_packetentities (sv, msg, 1);
				break;
			case svc_maxspeed:
				//XXX
				MSG_ReadFloat (msg);
				break;
			case svc_entgravity:
				//XXX
				MSG_ReadFloat (msg);
				break;
			case svc_setinfo:
				sv_setinfo (sv, msg);
				break;
			case svc_serverinfo:
				sv_serverinfo (sv, msg);
				break;
			case svc_updatepl:
				sv_update_net (sv, msg, 0);
				break;
			case svc_updateping:
				sv_update_net (sv, msg, 1);
				break;
			case svc_chokecount:
				//XXX
				MSG_ReadByte (msg);
				break;
			case svc_serverdata:
				sv_serverdata (sv, msg);
				break;
			case svc_stufftext:
				sv_stringcmd (sv, msg);
				break;

			case svc_soundlist:
				sv_soundlist (sv, msg);
				break;
			case svc_modellist:
				sv_modellist (sv, msg);
				break;

			case svc_spawnstaticsound:
				//XXX
				MSG_ReadCoordV (msg, v);
				MSG_ReadByte (msg);
				MSG_ReadByte (msg);
				MSG_ReadByte (msg);
				break;

			case svc_spawnbaseline:
				sv_spawnbaseline (sv, msg);
				break;
			case svc_spawnstatic:
				sv_spawnstatic (sv, msg);
				break;
			case svc_lightstyle:
				//XXX
				MSG_ReadByte (msg);
				MSG_ReadString (msg);
				break;
		}
	}
}
