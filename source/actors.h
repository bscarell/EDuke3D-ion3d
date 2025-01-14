//-------------------------------------------------------------------------
/*
Copyright (C) 2010 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#ifndef actors_h_
#define actors_h_

#include "player.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAXSLEEPDIST        16384
#define SLEEPTIME           1536
#define ZOFFSET             (1<<8)

#define ACTOR_MAXFALLINGZVEL 6144
#define ACTOR_ONWATER_ADDZ (24<<8)

// KEEPINSYNC lunatic/con_lang.lua
#define STAT_DEFAULT        0
#define STAT_ACTOR          1
#define STAT_ZOMBIEACTOR    2
#define STAT_EFFECTOR       3
#define STAT_PROJECTILE     4
#define STAT_MISC           5
#define STAT_STANDABLE      6
#define STAT_LOCATOR        7
#define STAT_ACTIVATOR      8
#define STAT_TRANSPORT      9
#define STAT_PLAYER         10
#define STAT_FX             11
#define STAT_FALLER         12
#define STAT_DUMMYPLAYER    13
#define STAT_LIGHT          14
#define STAT_NETALLOC       MAXSTATUS-1


// Defines the motion characteristics of an actor
enum amoveflags_t {
    face_player         = 1,
    geth                = 2,
    getv                = 4,
    random_angle        = 8,
    face_player_slow    = 16,
    spin                = 32,
    face_player_smart   = 64,
    fleeenemy           = 128,
    jumptoplayer_only   = 256,
    jumptoplayer_bits   = 257,  // NOTE: two bits set!
    seekplayer          = 512,
    furthestdir         = 1024,
    dodgebullet         = 4096
};

// Defines for 'useractor' keyword
enum uactortypes_t {
    notenemy,
    enemy,
    enemystayput
};

// These macros are there to give names to the t_data[]/T*/vm.g_t[] indices
// when used with actors. Greppability of source code is certainly a virtue.
#define AC_COUNT(t) ((t)[0])  /* the actor's count */
/* The ID of the actor's current move. In C-CON, the bytecode offset to the
 * move composite: */
#define AC_MOVE_ID(t) ((t)[1])
#define AC_ACTION_COUNT(t) ((t)[2])  /* the actor's action count */
#define AC_CURFRAME(t) ((t)[3])  /* the actor's current frame offset */
/* The ID of the actor's current action. In C-CON, the bytecode offset to the
 * action composite: */
#define AC_ACTION_ID(t) ((t)[4])
#define AC_AI_ID(t) ((t)[5])  /* the ID of the actor's current ai */

#ifdef LUNATIC
struct action {
    // These members MUST be in this exact order because FFI cdata of this type
    // can be initialized by passing a table with numeric indices (con.action).
    int16_t startframe, numframes;
    int16_t viewtype, incval, delay;
};

struct move {
    // These members MUST be in this exact order.
    int16_t hvel, vvel;
};

#pragma pack(push,1)
typedef struct { int32_t id; struct move mv; } con_move_t;
typedef struct { int32_t id; struct action ac; } con_action_t;
#pragma pack(pop)
#endif

// Select an actor's actiontics and movflags locations depending on
// whether we compile the Lunatic build.
// <spr>: sprite pointer
// <a>: actor_t pointer
#ifdef LUNATIC
# define AC_ACTIONTICS(spr, a) ((a)->actiontics)
# define AC_MOVFLAGS(spr, a) ((a)->movflags)
#else
# define AC_ACTIONTICS(spr, a) ((spr)->lotag)
# define AC_MOVFLAGS(spr, a) ((spr)->hitag)
#endif

// (+ 40 16 16 4 8 6 8 6 4 20)
typedef struct {
    int32_t t_data[10];  // 40b sometimes used to hold offsets to con code

#ifdef LUNATIC
    // total: 16b
    struct move mv;
    struct action ac;
    // Gets incremented by TICSPERFRAME on each A_Execute() call:
    uint16_t actiontics;
#endif

    int32_t flags; //4b
    vec3_t bpos; //12b
    int32_t floorz,ceilingz,lastvx,lastvy; //16b
    int32_t lasttransport; //4b

    int16_t picnum,ang,extra,owner; //8b
    int16_t movflag,tempang,timetosleep; //6b

    // NOTE: 'dispicnum' is updated locally, not in sync with the game!
    int16_t actorstayput, dispicnum;
#if !defined LUNATIC
    // NOTE: shootzvel is not used any more.
    int16_t shootzvel_;
#else
    // Movement flags, sprite[i].hitag in C-CON:
    uint16_t movflags;
#endif
    int16_t cgg;

    int16_t lightId, lightcount, lightmaxrange; //6b
#ifdef POLYMER
    _prlight *lightptr; //4b/8b
#else
    void *lightptr;
#endif

// pad struct to 128 bytes
#if !defined UINTPTR_MAX
# error Need UINTPTR_MAX define to select between 32- and 64-bit structs
#endif
#if UINTPTR_MAX == 0xffffffff
    /* 32-bit */
# if !defined LUNATIC
    int8_t filler[20];
# else
    int8_t filler[4];
# endif
#else
    /* 64-bit */
# if !defined LUNATIC
    int8_t filler[16];
# else
    /* no padding */
#endif
#endif
} actor_t;

#pragma pack(push,1)
// this struct needs to match the beginning of actor_t above
typedef struct {
    int32_t t_data[10];  // 40b sometimes used to hold offsets to con code

#ifdef LUNATIC
    struct move mv;
    struct action ac;
    uint16_t actiontics;
#endif

    int32_t flags; //4b
    vec3_t bpos; //12b
    int32_t floorz,ceilingz,lastvx,lastvy; //16b
    int32_t lasttransport; //4b

    int16_t picnum,ang,extra,owner; //8b
    int16_t movflag,tempang,timetosleep; // 6b

    int16_t actorstayput, dispicnum;
#if !defined LUNATIC
    int16_t shootzvel_;
#else
    uint16_t movflags;
#endif
    int16_t cgg;

    spritetype sprite;
    int16_t netIndex;

} netactor_t;
#pragma pack(pop)

typedef struct {
#if !defined LUNATIC
    intptr_t *execPtr; // pointer to CON script for this tile, formerly actorscrptr
    intptr_t *loadPtr; // pointer to load time CON script, formerly actorLoadEventScrPtr or something
#endif
    uint32_t flags;    // formerly SpriteFlags, ActorType

    int32_t cacherange; // formerly SpriteCache

    // todo: make this a pointer and allocate at runtime
    projectile_t *proj;
    projectile_t *defproj;
} tiledata_t;


// KEEPINSYNC lunatic/con_lang.lua
enum sflags_t {
    SFLAG_SHADOW           = 0x00000001,
    SFLAG_NVG              = 0x00000002,
    SFLAG_NOSHADE          = 0x00000004,
    SFLAG_PROJECTILE       = 0x00000008,
    SFLAG_DECAL            = 0x00000010,
    SFLAG_BADGUY           = 0x00000020,
    SFLAG_NOPAL            = 0x00000040,
    SFLAG_NOEVENTCODE      = 0x00000080,
    SFLAG_NOLIGHT          = 0x00000100,
    SFLAG_USEACTIVATOR     = 0x00000200,
    SFLAG_NULL             = 0x00000400, // null sprite in multiplayer
    SFLAG_NOCLIP           = 0x00000800, // clipmove it with cliptype 0
    SFLAG_NOFLOORSHADOW    = 0x00001000, // for temp. internal use, per-tile flag not checked
    SFLAG_SMOOTHMOVE       = 0x00002000,
    SFLAG_NOTELEPORT       = 0x00004000,
    SFLAG_BADGUYSTAYPUT    = 0x00008000,
    SFLAG_CACHE            = 0x00010000,
    // rotation-fixed wrt a pivot point to prevent position diverging due to
    // roundoff error accumulation:
    SFLAG_ROTFIXED         = 0x00020000,
    SFLAG_HARDCODED_BADGUY = 0x00040000,
    SFLAG_DIDNOSE7WATER    = 0x00080000, // used temporarily
    SFLAG_NODAMAGEPUSH     = 0x00100000,
    SFLAG_NOWATERDIP       = 0x00200000,
    SFLAG_HURTSPAWNBLOOD   = 0x00400000,
};

// Custom projectiles "workslike" flags.
// XXX: Currently not predefined from CON.
enum pflags_t {
    PROJECTILE_HITSCAN             = 0x00000001,
    PROJECTILE_RPG                 = 0x00000002,
    PROJECTILE_BOUNCESOFFWALLS     = 0x00000004,
    PROJECTILE_BOUNCESOFFMIRRORS   = 0x00000008,
    PROJECTILE_KNEE                = 0x00000010,
    PROJECTILE_WATERBUBBLES        = 0x00000020,
    PROJECTILE_TIMED               = 0x00000040,
    PROJECTILE_BOUNCESOFFSPRITES   = 0x00000080,
    PROJECTILE_SPIT                = 0x00000100,
    PROJECTILE_COOLEXPLOSION1      = 0x00000200,
    PROJECTILE_BLOOD               = 0x00000400,
    PROJECTILE_LOSESVELOCITY       = 0x00000800,
    PROJECTILE_NOAIM               = 0x00001000,
    PROJECTILE_RANDDECALSIZE       = 0x00002000,
    PROJECTILE_EXPLODEONTIMER      = 0x00004000,
    PROJECTILE_RPG_IMPACT          = 0x00008000,
    PROJECTILE_RADIUS_PICNUM       = 0x00010000,
    PROJECTILE_ACCURATE_AUTOAIM    = 0x00020000,
    PROJECTILE_FORCEIMPACT         = 0x00040000,
    PROJECTILE_REALCLIPDIST        = 0x00080000,
    PROJECTILE_ACCURATE            = 0x00100000,
    PROJECTILE_NOSETOWNERSHADE     = 0x00200000,
    PROJECTILE_TYPE_MASK           = PROJECTILE_HITSCAN|PROJECTILE_RPG|PROJECTILE_KNEE|PROJECTILE_BLOOD,
};

extern tiledata_t       g_tile[MAXTILES];
extern actor_t          actor[MAXSPRITES];
extern int32_t          block_deletesprite;
extern int32_t          g_noEnemies;
extern int32_t          otherp;
extern int32_t          ticrandomseed;
extern intptr_t         *g_parsingActorPtr;
extern projectile_t     SpriteProjectile[MAXSPRITES];



void                A_AddToDeleteQueue(int32_t i);
int32_t             A_CheckSwitchTile(int32_t i);
void                A_DeleteSprite(int32_t s);
void                A_DoGuts(int32_t sp,int32_t gtype,int32_t n);
void                A_DoGutsDir(int32_t sp,int32_t gtype,int32_t n);
int32_t             A_IncurDamage(int32_t sn);
void                A_MoveCyclers(void);
void                A_MoveDummyPlayers(void);
void                A_PlayAlertSound(int32_t i);
void                A_RadiusDamage(int32_t i,int32_t r,int32_t hp1,int32_t hp2,int32_t hp3,int32_t hp4);
void                A_SpawnMultiple(int32_t sp,int32_t pic,int32_t n);

void                G_AddGameLight(int32_t radius,int32_t srcsprite,int32_t zoffset,int32_t range,int32_t color,int32_t priority);
void                G_ClearCameraView(DukePlayer_t *ps);
void                G_DoInterpolations(int32_t smoothratio);
void                G_MoveWorld(void);
void                G_RefreshLights(void);
int32_t             G_SetInterpolation(int32_t * const posptr);
void                G_StopInterpolation(int32_t * const posptr);

// PK 20110701: changed input argument: int32_t i (== sprite, whose sectnum...) --> sectnum directly
void                Sect_ToggleInterpolation(int sectnum, int doset);
FORCE_INLINE void   Sect_ClearInterpolation(int sectnum) { Sect_ToggleInterpolation(sectnum, 0); }
FORCE_INLINE void   Sect_SetInterpolation(int sectnum) { Sect_ToggleInterpolation(sectnum, 1); }

#ifdef LUNATIC
int32_t G_ToggleWallInterpolation(int32_t w, int32_t doset);
#endif

#ifdef __cplusplus
}
#endif

#include "actors_inline.h"

#endif
