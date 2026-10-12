/*
 * SLOPE CE - Stage 14 (v0.2): structure tiers, ball-sized tube walls, painter's
 * blocks, spawn drop, capped tunnel speed, fully hidden cheats, worlds axes.
 * TI-84 Plus CE / CEdev / GraphX. 320x240, timer-based simulation.
 * Original-style perspective mesh, rolling sprites, red cubes, ramps, and routes.
 */
#include <tice.h>
#include <graphx.h>
#include <keypadc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/timers.h>
#include "gfx/sprite_1.h"
#include "gfx/sprite_2.h"
#include "gfx/sprite_3.h"
#include "gfx/sprite_4.h"
#include "gfx/sprite_5.h"
#include "gfx/sprite_6.h"

#define SW 320
#define SH 240
#define CAMERA_Y_SHIFT 58
#define MAX_TRACK_HAZARDS 64
#define SLALOM_ROAD_LENGTH 2400u
#define GROUND_GRID_STEP 60u
#define PROJECTION_DEPTH 2400u
#define MAX_STRUCTURE_DEPTH 2360u   /* was 2200: see farther, still inside the 2400-entry tables */
#define BLOCK_DETAIL_DEPTH 900u    /* was 1350: distant blocks take the cheap flat path sooner */
#define TUNNEL_VISUAL_LENGTH 1000u
#define SCORE_DISTANCE_STEP 600u
#define SAVE_NAME "SLPESAVE" /* AppVar names are limited to 8 characters. */
#define SAVE_MAGIC 0x534Cu
#define SAVE_VERSION 2u
#define COIN_CAP 99999999UL
#define GEM_COST_ROCKET 3u
#define WALL_RIDE_TOTAL 420u
#define WALL_RIDE_CLIMB 120u
#define WALL_RIDE_TOP 270u
#define WALL_RIDE_COOLDOWN 1000u
#define HEAVEN_DURATION_TICKS (30UL * 32768UL)
#define HELL_MAX_TICKS (5UL * 32768UL)
#define HEAVEN_WORLD_INDEX 3u
#define CHEAT_CODE_LENGTH 20u
#define CHEAT_COUNT 4u
#define CHEAT_SLIDE_TICKS 10u
#define CHEAT_SLIDE_GAP 4u
#define WALL_RAMP_LANE_PERCENT 35u
#define NEAR_MISS_PX 10
#define NEAR_MISS_SCORE 5u
#define HELL_NEAR_MISS_COINS 25u
#define HELL_FIRST_ROW_OFFSET 175 /* first Hell hazard row reaches the player after about 1.5 s */
#define GEM_CHANCE 9u             /* 1 in 9 patterns carries a gem: rare on purpose */
#define GEM_CAP 65000u
#define HEAVEN_SLIDE_STEP 20
#define WINGS_CRACK_TICKS (32768UL + 8192UL)
#define WINGS_FALL_TICKS (2UL * 32768UL)
#define LORE_COUNT 8u
#ifndef PLAYER_TILT
#define PLAYER_TILT 0
#endif

/* Palette indices. Colours are selected per planet at runtime. */
enum {
    C_BLACK = 0, C_BG_DARK, C_GRID_DIM, C_GRID, C_NEON, C_NEON_HI,
    C_RED_DARK, C_RED, C_RED_HI, C_WHITE, C_GRAY, C_GOLD, C_CYAN,
    C_PANEL, C_STAR, C_GREEN_DARK, C_PAD, C_PURPLE, C_CREAM, C_LIME,
    C_SKY_TOP, C_SKY_MID, C_SKY_LOW, C_MARBLE, C_MARBLE_DK, C_GOLD_DK
};

enum ScreenState {
    SCREEN_TITLE, SCREEN_GAME, SCREEN_DEATH, SCREEN_GARAGE,
    SCREEN_WORLDS, SCREEN_SHOP, SCREEN_SETTINGS, SCREEN_EVENT,
    SCREEN_BESTS, SCREEN_CODEX
};
enum RealmMode { REALM_NORMAL, REALM_HEAVEN, REALM_HELL };
enum EventType { EVENT_NONE, EVENT_ASCENSION, EVENT_HELL_DESCENT, EVENT_RETURN, EVENT_WINGS };
enum Direction { DIR_NONE, DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
enum Shape { SHAPE_SPHERE, SHAPE_CUBE, SHAPE_PYRAMID };
enum CheatToken {
    CODE_4 = 5, CODE_5, CODE_6, CODE_SIN, CODE_COS, CODE_TAN, CODE_9
};

#define SET_TRAIL       0x01u
#define SET_INVULNERABLE 0x02u
#define SET_SPEED_CHEAT 0x04u
#define SET_DOUBLE_COIN 0x08u
#define SET_SLOW_CHEAT  0x10u

/* Version-1 layout, kept only so existing saves migrate instead of resetting. */
typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t activePlanet;
    uint32_t coins;
    uint16_t gems;
    uint32_t bestScore;
    uint8_t ownedShapes;
    uint8_t activeShape;
    uint8_t colorTheme;
    uint8_t tracksBuilt[3];
    uint8_t unlockedPlanets;
    uint8_t cheatsUnlocked;
    uint8_t settings;
    uint8_t upgrades;
    uint16_t checksum;
} SaveDataV1;

typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t activePlanet;
    uint32_t coins;
    uint16_t gems;
    uint32_t bestScore;
    uint8_t ownedShapes;       /* bit 0 sphere, bit 1 cube, bit 2 pyramid */
    uint8_t activeShape;
    uint8_t colorTheme;
    uint8_t tracksBuilt[3];
    uint8_t unlockedPlanets;   /* bit mask; bit 3 means HEAVEN has been visited */
    uint8_t cheatsUnlocked;
    uint8_t settings;
    uint8_t upgrades;          /* bit 0 = permanent double-coin upgrade */
    uint32_t worldBest[4];     /* NEON, CYAN, VOID, HEAVEN */
    uint32_t dailyBest;
    uint16_t dailyYear;
    uint8_t dailyMonth;
    uint8_t dailyDay;
    uint16_t loreMask;         /* one bit per codex entry */
    uint16_t checksum;
} SaveData;

typedef struct {
    uint8_t direction; /* most recently pressed arrow */
    uint8_t select;
    uint8_t back;
    uint8_t codeToken; /* edge-triggered arrow, digit, or trig key for Settings */
    uint8_t menuNumber; /* edge-triggered 1-6 quick selection on the title screen */
    uint8_t fkey;       /* edge-triggered F1-F5, reported as 1-5 */
    uint8_t key8;
    uint8_t leftHeld;
    uint8_t rightHeld;
} InputFrame;

enum HazardType { HAZARD_BLOCK, HAZARD_RAMP, HAZARD_GAP, HAZARD_GEM };
typedef struct {
    int16_t lane;
    uint16_t depth;
    uint8_t heightLane;
    uint8_t type;
    uint8_t active;
} TrackHazard;

typedef struct {
    int16_t z;
    uint16_t zlen;
} GroundPlane;

enum OriginalObjectType { ORIGINAL_BLOCK, ORIGINAL_RAMP };

static enum ScreenState screen = SCREEN_TITLE;
static SaveData save;
static int8_t menuIndex = 0;
static int8_t garageTab = 0;    /* 0 shape, 1 colour */
static uint8_t garageShapeIndex = SHAPE_SPHERE; /* separate from equipped shape */
static int8_t worldAction = 0;  /* 0 build track, 1 launch rocket */
static int8_t worldPlanet = 0;
static int8_t shopIndex = 0;
static int8_t settingsIndex = 0;
static uint8_t cheatProgress = 0;
static uint16_t cheatNotice = 0;
static uint8_t cheatRevealStage = CHEAT_COUNT;
static uint8_t cheatRevealFrame = 0;
static uint8_t cheatShakeTicks = 0;
static uint32_t uiFrame = 0;

static uint32_t travelled = 0;
static uint32_t score = 0;
static uint16_t scoreDistance = 0;
static uint32_t runScore = 0;
static uint32_t frameNumber = 0;
static uint16_t boostTimer = 0;
static uint8_t crashed = 0;
static uint8_t falling = 0;
static uint8_t dropping = 0;
static uint8_t dropFrames = 0;
static uint16_t slalomRoadTimer = 0;
static uint16_t slalomCycle = 0;
static uint32_t fallElapsedTicks = 0;
static int16_t fallCameraPan = 0;
static uint8_t deathPhase = 0; /* 0 count score, 1 count coins, 2 payout complete */
static uint32_t deathCounter = 0;
static uint32_t coinCounter = 0;
static uint32_t coinPayout = 0;
static uint8_t payoutApplied = 0;
static uint8_t realmMode = REALM_NORMAL;
static uint8_t eventType = EVENT_NONE;
static uint8_t eventStep = 0;
static uint8_t eventFrame = 0;
static uint32_t eventElapsedTicks = 0;
static uint32_t realmElapsedTicks = 0;
static uint32_t hellElapsedTicks = 0;
static uint32_t lastPhasePayout = 0;
static uint8_t wallRideSide = 0;
static uint16_t wallRideProgress = 0;
static uint16_t wallRideCooldown = 0;
static uint8_t wallClimbCount = 0;
static TrackHazard trackHazards[MAX_TRACK_HAZARDS];
static int16_t trailX[5];
static uint8_t trailCount = 0;
static int16_t airtime = 0;
static int16_t verticalVelocity = 0;
static uint16_t jumpLockout = 0;
static uint8_t warningFlash = 0;
static GroundPlane grounds[2];
/* Compact 16-bit X and 8-bit Y tables reproduce the supplied game's projection. */
static uint16_t originalX[4][PROJECTION_DEPTH];
static uint8_t originalY[4][PROJECTION_DEPTH];
static uint8_t objectCursor = 0;
static uint16_t animationDistance = 0;
static uint32_t originalTicks = 0;
static uint32_t speedFraction = 0;
static uint32_t moveFraction = 0;
static uint16_t targetSpeed = 250;
static uint16_t targetMoveSpeed = 1300;
static int16_t originalPlayerX = 107;
static int16_t originalPlayerY = 180;
static int16_t originalPlayerVelocity = 0;
static uint8_t speedNotice = 0;
static uint8_t patternNotice = 0;
static const char *patternNoticeText = "FATHERLESS TRIO";
static uint16_t tunnelVisualDistance = 0;
static uint32_t tunnelStartTravel = 0;
static int8_t deathMenuIndex = 0;
static uint32_t patternRng = 0x534C4F50u;
static int16_t patternSpawnOffset = 0;
static gfx_sprite_t *originalPlayerSprites[6] = {
    sprite_1, sprite_2, sprite_3, sprite_4, sprite_5, sprite_6
};

static uint8_t oldDirections = 0;
static uint8_t oldEnter = 0;
static uint8_t oldSecond = 0;
static uint8_t oldClear = 0;
static uint8_t oldCodeToken = 0;
static uint8_t oldKey8 = 0;
static uint8_t oldMenuNumber = 0;

/* Diagnostic FPS estimate, shown only during play. */
static uint32_t fpsStartTicks = 0;
static uint16_t fpsFrameCount = 0;
static uint8_t fpsPollDivider = 0;
static uint8_t fpsShown = 0;

typedef struct {
    uint32_t value;
    char digits[11];
    uint8_t length;
    uint8_t valid;
} NumberCache;

/* The play HUD redraws into a cleared backbuffer, but can reuse its formatted
 * digits between frames instead of repeating 32-bit decimal divisions. */
static NumberCache hudBestCache;
static NumberCache hudCoinsCache;
static NumberCache hudGemsCache;
static NumberCache hudSpeedCache;
static NumberCache hudFpsCache;
static NumberCache hudHeavenTimeCache;
static NumberCache pixelScoreCache;
/* Stage 13 state. */
static uint8_t quitRequested = 0;
static uint8_t gemsDirty = 0;
static uint8_t dailyMode = 0;
static uint8_t textScale = 1;
static int16_t starOffsetY = 0;
static uint16_t drawDepthLimit = MAX_STRUCTURE_DEPTH;
static int16_t patternLowestBlockZ = 32000;
static uint8_t patternLowestBlockLane = 0;
static uint8_t nearMissFrame = 0;
static uint16_t runNearMisses = 0;
static uint8_t runGems = 0;
static uint32_t hellBonus = 0;
static uint8_t hellSurvived = 0;
static uint16_t loreNew = 0;
static uint8_t heavenSeq = 0;
static uint8_t heavenPresses = 0;
static int16_t heavenSlide = 0;
static int8_t heavenDir = 0;
static uint8_t heavenView = 0;
static int8_t codexIndex = 0;
static uint8_t oldFkey = 0;

static uint16_t save_checksum_v1(const SaveDataV1 *s) {
    uint16_t v = (uint16_t)(s->magic ^ s->version ^ s->activePlanet);
    v ^= (uint16_t)s->coins;
    v ^= (uint16_t)(s->coins >> 16);
    v ^= s->gems;
    v ^= (uint16_t)s->bestScore;
    v ^= (uint16_t)(s->bestScore >> 16);
    v ^= (uint16_t)(s->ownedShapes << 1);
    v ^= (uint16_t)(s->activeShape << 3);
    v ^= (uint16_t)(s->colorTheme << 5);
    v ^= (uint16_t)(s->tracksBuilt[0] | (s->tracksBuilt[1] << 4) | (s->tracksBuilt[2] << 8));
    v ^= (uint16_t)(s->unlockedPlanets << 2);
    v ^= (uint16_t)(s->cheatsUnlocked << 7);
    v ^= (uint16_t)(s->settings << 8);
    v ^= (uint16_t)(s->upgrades << 12);
    return (uint16_t)(v ^ 0xA75Du);
}

static uint16_t save_checksum(const SaveData *s) {
    uint16_t v = (uint16_t)(s->magic ^ s->version ^ s->activePlanet);
    uint8_t i;
    v ^= (uint16_t)s->coins;
    v ^= (uint16_t)(s->coins >> 16);
    v ^= s->gems;
    v ^= (uint16_t)s->bestScore;
    v ^= (uint16_t)(s->bestScore >> 16);
    v ^= (uint16_t)(s->ownedShapes << 1);
    v ^= (uint16_t)(s->activeShape << 3);
    v ^= (uint16_t)(s->colorTheme << 5);
    v ^= (uint16_t)(s->tracksBuilt[0] | (s->tracksBuilt[1] << 4) | (s->tracksBuilt[2] << 8));
    v ^= (uint16_t)(s->unlockedPlanets << 2);
    v ^= (uint16_t)(s->cheatsUnlocked << 7);
    v ^= (uint16_t)(s->settings << 8);
    v ^= (uint16_t)(s->upgrades << 12);
    for (i = 0; i < 4u; ++i) {
        v = (uint16_t)((v << 1) | (v >> 15));
        v ^= (uint16_t)s->worldBest[i];
        v ^= (uint16_t)(s->worldBest[i] >> 16);
    }
    v ^= (uint16_t)s->dailyBest;
    v ^= (uint16_t)(s->dailyBest >> 16);
    v ^= s->dailyYear;
    v ^= (uint16_t)((s->dailyMonth << 8) | s->dailyDay);
    v ^= (uint16_t)(s->loreMask << 3);
    return (uint16_t)(v ^ 0xA75Du);
}

static void default_save(void) {
    memset(&save, 0, sizeof(save));
    save.magic = SAVE_MAGIC;
    save.version = SAVE_VERSION;
    save.coins = 0;
    save.gems = 0;
    save.ownedShapes = 0x01u;
    save.activeShape = SHAPE_SPHERE;
    save.colorTheme = 0;
    save.tracksBuilt[0] = 1; /* One starter track on the first planet. */
    save.unlockedPlanets = 0x05u;
    save.settings = 0;
    save.checksum = save_checksum(&save);
}

static void save_game(void) {
    FILE *f;
    save.magic = SAVE_MAGIC;
    save.version = SAVE_VERSION;
    save.checksum = save_checksum(&save);
    gemsDirty = 0;
    f = fopen(SAVE_NAME, "wb");
    if (f) {
        (void)fwrite(&save, sizeof(save), 1, f);
        (void)fclose(f);
    }
}

static void load_game(void) {
    FILE *f = fopen(SAVE_NAME, "rb");
    uint8_t valid = 0, migrated = 0;
    uint8_t savedCheats;
    if (f) {
        uint8_t raw[sizeof(SaveData)];
        size_t got = fread(raw, 1, sizeof(raw), f);
        if (got == sizeof(SaveData)) {
            memcpy(&save, raw, sizeof(save));
            valid = (uint8_t)(save.magic == SAVE_MAGIC && save.version == SAVE_VERSION &&
                              save.checksum == save_checksum(&save));
        } else if (got == sizeof(SaveDataV1)) {
            /* A Stage 12 (or older) save: carry everything across, add the new fields empty. */
            SaveDataV1 old;
            memcpy(&old, raw, sizeof(old));
            if (old.magic == SAVE_MAGIC && old.version == 1u &&
                old.checksum == save_checksum_v1(&old)) {
                memset(&save, 0, sizeof(save));
                save.magic = SAVE_MAGIC;
                save.version = SAVE_VERSION;
                save.activePlanet = old.activePlanet;
                save.coins = old.coins;
                save.gems = old.gems;
                save.bestScore = old.bestScore;
                save.ownedShapes = old.ownedShapes;
                save.activeShape = old.activeShape;
                save.colorTheme = old.colorTheme;
                save.tracksBuilt[0] = old.tracksBuilt[0];
                save.tracksBuilt[1] = old.tracksBuilt[1];
                save.tracksBuilt[2] = old.tracksBuilt[2];
                save.unlockedPlanets = old.unlockedPlanets;
                save.cheatsUnlocked = old.cheatsUnlocked;
                save.settings = old.settings;
                save.upgrades = old.upgrades;
                valid = 1;
                migrated = 1;
            }
        }
        (void)fclose(f);
    }
    if (!valid || save.activeShape > SHAPE_PYRAMID || save.colorTheme > 3 ||
        save.activePlanet > 2 || !(save.ownedShapes & 0x01u)) {
        default_save();
        save_game();
    } else if (migrated) {
        save_game();
    }
    if (!(save.unlockedPlanets & 0x01u)) save.unlockedPlanets |= 0x01u;
    /* Stage 10 exposes the existing Void world for immediate play. */
    if (!(save.unlockedPlanets & 0x04u)) {
        save.unlockedPlanets |= 0x04u;
        save_game();
    }
    if (!(save.ownedShapes & (uint8_t)(1u << save.activeShape))) save.activeShape = SHAPE_SPHERE;
    if (!(save.unlockedPlanets & (uint8_t)(1u << save.activePlanet))) save.activePlanet = 0;
    if (save.tracksBuilt[0] > 3) save.tracksBuilt[0] = 3;
    if (save.tracksBuilt[1] > 3) save.tracksBuilt[1] = 3;
    if (save.tracksBuilt[2] > 3) save.tracksBuilt[2] = 3;
    savedCheats = (uint8_t)(save.settings & (SET_INVULNERABLE | SET_SPEED_CHEAT |
                                             SET_DOUBLE_COIN | SET_SLOW_CHEAT));
    if (savedCheats) {
        save.settings &= (uint8_t)~(SET_INVULNERABLE | SET_SPEED_CHEAT |
                                    SET_DOUBLE_COIN | SET_SLOW_CHEAT);
        save_game();
    }
}

static void put_rgb(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    gfx_palette[index] = gfx_RGBTo1555(r, g, b);
}

static void set_palette(void) {
    put_rgb(C_BLACK, 0, 0, 0);
    put_rgb(C_BG_DARK, 0, 9, 3);
    put_rgb(C_GRID_DIM, 0, 58, 12);
    put_rgb(C_GRID, 0, 145, 25);
    put_rgb(C_NEON, 0, 255, 45);
    put_rgb(C_NEON_HI, 160, 255, 174);
    put_rgb(C_RED_DARK, 54, 0, 0);
    put_rgb(C_RED, 255, 0, 18);
    put_rgb(C_RED_HI, 255, 96, 90);
    put_rgb(C_WHITE, 242, 255, 244);
    put_rgb(C_GRAY, 120, 151, 128);
    put_rgb(C_GOLD, 255, 211, 45);
    put_rgb(C_CYAN, 0, 232, 255);
    put_rgb(C_PANEL, 0, 12, 5);
    put_rgb(C_STAR, 0, 91, 21);
    put_rgb(C_GREEN_DARK, 0, 48, 9);
    put_rgb(C_PAD, 25, 255, 75);
    put_rgb(C_PURPLE, 194, 64, 255);
    put_rgb(C_CREAM, 255, 251, 220);
    put_rgb(C_LIME, 166, 255, 0);
    put_rgb(C_SKY_TOP, 250, 204, 92);
    put_rgb(C_SKY_MID, 253, 223, 143);
    put_rgb(C_SKY_LOW, 255, 239, 190);
    put_rgb(C_MARBLE, 238, 234, 224);
    put_rgb(C_MARBLE_DK, 168, 160, 146);
    put_rgb(C_GOLD_DK, 170, 122, 14);

    /* Planet palettes change only a few entries; red remains the danger colour. */
    if (save.activePlanet == 1) {
        put_rgb(C_GRID_DIM, 0, 48, 77);
        put_rgb(C_GRID, 0, 139, 204);
        put_rgb(C_NEON, 0, 235, 255);
        put_rgb(C_NEON_HI, 188, 255, 255);
        put_rgb(C_PAD, 0, 255, 230);
        put_rgb(C_BG_DARK, 0, 7, 22);
    } else if (save.activePlanet == 2) {
        put_rgb(C_GRID_DIM, 48, 0, 79);
        put_rgb(C_GRID, 128, 0, 191);
        put_rgb(C_NEON, 221, 45, 255);
        put_rgb(C_NEON_HI, 255, 196, 255);
        put_rgb(C_PAD, 255, 64, 235);
        put_rgb(C_BG_DARK, 13, 0, 22);
    }
    /* Ball theme tints the ball but leaves the course palette planet-specific. */
    if (save.colorTheme == 1) {
        put_rgb(C_NEON, 0, 244, 255); put_rgb(C_NEON_HI, 201, 255, 255);
    } else if (save.colorTheme == 2) {
        put_rgb(C_NEON, 255, 40, 220); put_rgb(C_NEON_HI, 255, 208, 247);
    } else if (save.colorTheme == 3) {
        put_rgb(C_NEON, 255, 171, 35); put_rgb(C_NEON_HI, 255, 244, 195);
    }
    if (realmMode == REALM_HEAVEN) {
        put_rgb(C_BG_DARK, 16, 10, 1);
        put_rgb(C_GRID_DIM, 94, 66, 18);
        put_rgb(C_GRID, 190, 142, 45);
        put_rgb(C_NEON, 255, 226, 128);
        put_rgb(C_NEON_HI, 255, 255, 230);
        put_rgb(C_PAD, 255, 243, 173);
    } else if (realmMode == REALM_HELL) {
        put_rgb(C_BG_DARK, 12, 0, 0);
        put_rgb(C_GRID_DIM, 55, 0, 0);
        put_rgb(C_GRID, 132, 0, 0);
        put_rgb(C_NEON, 255, 28, 20);
        put_rgb(C_NEON_HI, 255, 100, 70);
        put_rgb(C_PAD, 220, 8, 0);
        put_rgb(C_CYAN, 150, 20, 12);
    }
    /* The six original rolling frames use palette index 0x27 for their wire. */
    gfx_palette[0x27] = gfx_palette[C_NEON];
}

static InputFrame poll_keys(void) {
    InputFrame k;
    uint8_t dirs, newlyPressed, codeNow = 0, menuNumberNow = 0, fkeyNow = 0;
    uint8_t enterNow, secondNow, clearNow, key8Now;
    k.direction = DIR_NONE;
    k.select = 0;
    k.back = 0;
    k.codeToken = 0;
    k.menuNumber = 0;
    k.fkey = 0;
    k.key8 = 0;
    k.leftHeld = 0;
    k.rightHeld = 0;
    kb_Scan();
    dirs = (uint8_t)(kb_Data[7] & (kb_Up | kb_Down | kb_Left | kb_Right));
    newlyPressed = (uint8_t)(dirs & (uint8_t)~oldDirections);
    if (newlyPressed & kb_Up) k.direction = DIR_UP;
    else if (newlyPressed & kb_Down) k.direction = DIR_DOWN;
    else if (newlyPressed & kb_Left) k.direction = DIR_LEFT;
    else if (newlyPressed & kb_Right) k.direction = DIR_RIGHT;
    k.codeToken = k.direction;
    if (kb_Data[3] & kb_4) codeNow = CODE_4;
    else if (kb_Data[4] & kb_5) codeNow = CODE_5;
    else if (kb_Data[5] & kb_6) codeNow = CODE_6;
    else if (kb_Data[3] & kb_Sin) codeNow = CODE_SIN;
    else if (kb_Data[4] & kb_Cos) codeNow = CODE_COS;
    else if (kb_Data[5] & kb_Tan) codeNow = CODE_TAN;
    else if (kb_Data[5] & kb_9) codeNow = CODE_9;
    if (!k.codeToken && codeNow && codeNow != oldCodeToken) k.codeToken = codeNow;
    if (kb_Data[3] & kb_1) menuNumberNow = 1;
    else if (kb_Data[4] & kb_2) menuNumberNow = 2;
    else if (kb_Data[5] & kb_3) menuNumberNow = 3;
    else if (kb_Data[3] & kb_4) menuNumberNow = 4;
    else if (kb_Data[4] & kb_5) menuNumberNow = 5;
    else if (kb_Data[5] & kb_6) menuNumberNow = 6;
    if (menuNumberNow && menuNumberNow != oldMenuNumber) k.menuNumber = menuNumberNow;
    /* F1-F5 all live in key group 1 together with 2nd, Mode and Del. */
    if (kb_Data[1] & kb_Yequ) fkeyNow = 1;
    else if (kb_Data[1] & kb_Window) fkeyNow = 2;
    else if (kb_Data[1] & kb_Zoom) fkeyNow = 3;
    else if (kb_Data[1] & kb_Trace) fkeyNow = 4;
    else if (kb_Data[1] & kb_Graph) fkeyNow = 5;
    if (fkeyNow && fkeyNow != oldFkey) k.fkey = fkeyNow;
    k.leftHeld = (uint8_t)((dirs & kb_Left) != 0);
    k.rightHeld = (uint8_t)((dirs & kb_Right) != 0);
    enterNow = (uint8_t)((kb_Data[6] & kb_Enter) != 0);
    secondNow = (uint8_t)((kb_Data[1] & kb_2nd) != 0);
    clearNow = (uint8_t)((kb_Data[6] & kb_Clear) != 0);
    key8Now = (uint8_t)((kb_Data[4] & kb_8) != 0);
    k.select = (uint8_t)((enterNow && !oldEnter) || (secondNow && !oldSecond));
    k.back = (uint8_t)(clearNow && !oldClear);
    k.key8 = (uint8_t)(key8Now && !oldKey8);
    oldDirections = dirs;
    oldEnter = enterNow;
    oldSecond = secondNow;
    oldClear = clearNow;
    oldCodeToken = codeNow;
    oldKey8 = key8Now;
    oldMenuNumber = menuNumberNow;
    oldFkey = fkeyNow;
    return k;
}

static void set_scale(uint8_t s) {
    textScale = s;
    gfx_SetTextScale(s, s);
}

static void print_at(const char *text, int16_t x, int16_t y, uint8_t fg, uint8_t bg) {
    /* Rows that would run off the top or bottom are skipped, so sliding screens never wrap text. */
    if (y < 0 || y + 8 * (int16_t)textScale > SH) return;
    gfx_SetTextFGColor(fg);
    gfx_SetTextBGColor(bg);
    gfx_SetTextXY(x, y);
    gfx_PrintString(text);
}

/* Centres on the 320 px screen using the current text scale (8 px per character at scale 1). */
static void print_center(const char *text, int16_t y, uint8_t fg, uint8_t bg) {
    print_at(text, (int16_t)((SW - (int16_t)(strlen(text) * 8u * textScale)) / 2), y, fg, bg);
}

static uint8_t format_u32(uint32_t value, char *out) {
    char reversed[10];
    uint8_t length = 0, i;
    if (value <= 65535u) {
        uint16_t small = (uint16_t)value;
        do {
            reversed[length++] = (char)('0' + (small % 10u));
            small = (uint16_t)(small / 10u);
        } while (small);
    } else {
        do {
            reversed[length++] = (char)('0' + (value % 10u));
            value /= 10u;
        } while (value && length < sizeof(reversed));
    }
    for (i = 0; i < length; ++i) out[i] = reversed[length - i - 1u];
    out[length] = '\0';
    return length;
}

static void cache_number(NumberCache *cache, uint32_t value) {
    if (!cache->valid || cache->value != value) {
        cache->value = value;
        cache->length = format_u32(value, cache->digits);
        cache->valid = 1;
    }
}

static void print_number(uint32_t value, int16_t x, int16_t y, uint8_t fg, uint8_t bg) {
    char digits[11];
    format_u32(value, digits);
    print_at(digits, x, y, fg, bg);
}

static void print_number_cached(NumberCache *cache, uint32_t value,
                                int16_t x, int16_t y, uint8_t fg, uint8_t bg) {
    cache_number(cache, value);
    print_at(cache->digits, x, y, fg, bg);
}
/* Right-aligned cached number; returns the x it was drawn at so a label can sit beside it. */
static int16_t print_number_right(NumberCache *cache, uint32_t value, int16_t rightX,
                                  int16_t y, uint8_t fg, uint8_t bg) {
    int16_t x;
    cache_number(cache, value);
    x = (int16_t)(rightX - (int16_t)cache->length * 8);
    print_at(cache->digits, x, y, fg, bg);
    return x;
}

static void print_stat_right(const char *label, NumberCache *cache, uint32_t value,
                             int16_t rightX, int16_t y, uint8_t labelFg, uint8_t valueFg, uint8_t bg) {
    int16_t x = print_number_right(cache, value, rightX, y, valueFg, bg);
    print_at(label, (int16_t)(x - (int16_t)strlen(label) * 8 - 4), y, labelFg, bg);
}

static void print_number_at_right(uint32_t value, int16_t rightX, int16_t y, uint8_t fg, uint8_t bg) {
    char digits[11];
    uint8_t n = format_u32(value, digits);
    print_at(digits, (int16_t)(rightX - (int16_t)n * 8), y, fg, bg);
}

/* 3x5 pixel digits keep the in-run score close to the original's digital readout. */
static const uint8_t digitBits[10][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
    {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7}
};

static void draw_pixel_score(uint32_t value, int16_t centerX, int16_t y) {
    uint8_t i;
    int16_t x;
    if (value > 999999u) value = 999999u;
    cache_number(&pixelScoreCache, value);
    x = (int16_t)(centerX - ((int16_t)pixelScoreCache.length * 8 - 2) / 2);
    gfx_SetColor(C_NEON);
    for (i = 0; i < pixelScoreCache.length; ++i) {
        uint8_t row, col, val = (uint8_t)(pixelScoreCache.digits[i] - '0');
        for (row = 0; row < 5; ++row) {
            uint8_t mask = digitBits[val][row];
            col = 0;
            while (col < 3) {
                if (mask & (uint8_t)(1u << (2u - col))) {
                    uint8_t startCol = col;
                    while (col < 3 && (mask & (uint8_t)(1u << (2u - col)))) ++col;
                    gfx_FillRectangle(x + (int16_t)startCol * 2, y + (int16_t)row * 2,
                                      (int16_t)(col - startCol) * 2, 2);
                } else ++col;
            }
        }
        x += 8;
    }
}

static int16_t clamp16(int32_t v, int16_t low, int16_t high) {
    if (v < low) return low;
    if (v > high) return high;
    return (int16_t)v;
}

/* GraphX documents NoClip lines as faster; fall back to clipped drawing at edges. */
static void fast_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    if ((uint16_t)x0 < SW && (uint16_t)x1 < SW &&
        (uint16_t)y0 < SH && (uint16_t)y1 < SH) {
        gfx_Line_NoClip((uint24_t)x0, (uint8_t)y0, (uint24_t)x1, (uint8_t)y1);
    } else {
        gfx_Line(x0, y0, x1, y1);
    }
}

/* Projection tables and mesh drawing translated from the supplied SLOPE CE
 * reference. A fixed-point reciprocal is shared by all eight grid lanes at
 * each depth, avoiding thousands of slow 32-bit divisions during game load. */
static int32_t reciprocal_project(int32_t numerator, uint16_t inverse, uint16_t divisor) {
    int32_t magnitude = numerator < 0 ? -numerator : numerator;
    int32_t quotient = (magnitude * inverse) >> 16;
    if ((quotient + 1) * divisor <= magnitude) ++quotient;
    return numerator < 0 ? -quotient : quotient;
}

static void build_original_projection(void) {
    static const int16_t xNumerators[4] = { -11400, -3500, 4400, 12300 };
    static const int16_t yNumerators[4] = { -120, 5780, 11680, 17580 };
    uint16_t z;
    uint8_t lane;
    for (z = 1; z < PROJECTION_DEPTH; ++z) {
        int32_t rz = (int32_t)z - 200;
        uint16_t divisor, inverse;
        if (rz < 20) rz = 20;
        divisor = (uint16_t)rz;
        inverse = (uint16_t)(65536UL / divisor);
        for (lane = 0; lane < 4u; ++lane) {
            int32_t projected = reciprocal_project(xNumerators[lane], inverse, divisor) + 160;
            if (projected < 0) projected = 0;
            if (projected > 319) projected = 319;
            originalX[lane][z] = (uint16_t)projected;

            projected = reciprocal_project(yNumerators[lane], inverse, divisor) + 120 - CAMERA_Y_SHIFT;
            if (projected < 0) projected = 0;
            if (projected > 239) projected = 239;
            originalY[lane][z] = (uint8_t)projected;
        }
    }
}

/* During a fall, lift the course in view as the ball drops away from it. */
static uint8_t scene_y(uint8_t y, uint16_t z) {
    int16_t v = (int16_t)originalY[y][z];
    if (!fallCameraPan) return (uint8_t)v;
    return (uint8_t)clamp16(v - fallCameraPan, 0, 239);
}

static int16_t slalom_offset_q(uint16_t z) {
    static const int16_t wave[8] = { 0, 388, 550, 388, 0, -388, -550, -388 };
    uint16_t phase = (uint16_t)(slalomCycle + z);
    uint8_t index;
    uint16_t part;
    int16_t start, end;
    while (phase >= 840u) phase = (uint16_t)(phase - 840u);
    index = (uint8_t)(phase / 105u);
    part = (uint16_t)(phase % 105u);
    start = wave[index];
    end = wave[(index + 1u) & 7u];
    /* The largest product is below 41,000, so the eZ80's native 24-bit
     * signed arithmetic is sufficient here. */
    return (int16_t)(start + ((int24_t)(end - start) * part) / 105);
}

static int16_t project_lane_q(int16_t laneQ, uint16_t z) {
    uint8_t lane;
    uint16_t part;
    int16_t x0, x1;
    laneQ = clamp16(laneQ, 0, 3000);
    lane = (uint8_t)(laneQ / 1000);
    if (lane >= 3u) return (int16_t)originalX[3][z];
    part = (uint16_t)(laneQ - (int16_t)lane * 1000);
    x0 = (int16_t)originalX[lane][z];
    x1 = (int16_t)originalX[lane + 1u][z];
    /* The interpolation product is bounded by 319 * 999 = 318,681. */
    return (int16_t)(x0 + ((int24_t)(x1 - x0) * part) / 1000);
}

static void slalom_road_bounds(uint16_t z, int16_t *left, int16_t *right) {
    int16_t centerQ = (int16_t)(1500 + slalom_offset_q(z));
    *left = project_lane_q((int16_t)(centerQ - 750), z);
    *right = project_lane_q((int16_t)(centerQ + 750), z);
}

/* Lower route: a narrow, smooth S-curve with a 1.5-lane world width. */
static void draw_slalom_road(void) {
    int16_t leftX[9], centerX[9], rightX[9], roadY[9];
    uint8_t i;
    uint16_t z;
    for (i = 0, z = 240; i < 9u; ++i, z = (uint16_t)(z + 120u)) {
        int16_t left, right;
        int16_t centerQ = (int16_t)(1500 + slalom_offset_q(z));
        left = project_lane_q((int16_t)(centerQ - 750), z);
        right = project_lane_q((int16_t)(centerQ + 750), z);
        leftX[i] = left;
        rightX[i] = right;
        centerX[i] = project_lane_q(centerQ, z);
        roadY[i] = clamp16((int16_t)scene_y(2, z) + 8, 0, 239);
    }
    gfx_SetColor(C_GRID);
    for (i = 0; i < 9u; ++i)
        fast_line(leftX[i], roadY[i], rightX[i], roadY[i]);
    gfx_SetColor(C_NEON);
    for (i = 0; i < 8u; ++i) {
        fast_line(leftX[i], roadY[i], leftX[i + 1u], roadY[i + 1u]);
        fast_line(rightX[i], roadY[i], rightX[i + 1u], roadY[i + 1u]);
    }
    gfx_SetColor(C_GRID_DIM);
    for (i = 0; i < 8u; ++i)
        fast_line(centerX[i], roadY[i], centerX[i + 1u], roadY[i + 1u]);
}

/* Draw the two recycled segments as one continuous mesh to avoid seam gaps. */
static void draw_normal_road(void) {
    int16_t zsteps[18];
    uint8_t yvals[18], count = 0, i, x;
    int24_t startA = grounds[0].z;
    int24_t startB = grounds[1].z;
    int24_t nearZ = startA < startB ? startA : startB;
    int24_t endA = startA + grounds[0].zlen;
    int24_t endB = startB + grounds[1].zlen;
    int24_t farZ = endA > endB ? endA : endB;
    int24_t z;
    for (z = nearZ; z <= farZ && count < 18u; z += GROUND_GRID_STEP) {
        if (z <= 200 || z >= (int24_t)PROJECTION_DEPTH) continue;
        zsteps[count] = (int16_t)z;
        yvals[count] = scene_y(2, (uint16_t)z);
        ++count;
    }
    gfx_SetColor(C_NEON);
    for (i = 0; i < count; ++i) {
        uint16_t cz = (uint16_t)zsteps[i];
        int16_t x0 = (int16_t)originalX[0][cz];
        int16_t x1 = (int16_t)originalX[3][cz];
        gfx_HorizLine_NoClip((uint24_t)(x0 < x1 ? x0 : x1), yvals[i], (uint24_t)(abs(x1 - x0) + 1));
    }
    if (nearZ < 220 && farZ >= 220) {
        uint8_t y = scene_y(2, 220u);
        int16_t x0 = (int16_t)originalX[0][220];
        int16_t x1 = (int16_t)originalX[3][220];
        gfx_HorizLine_NoClip((uint24_t)(x0 < x1 ? x0 : x1), y, (uint24_t)(abs(x1 - x0) + 1));
    }
    for (x = 0; x < 4u; ++x) {
        int16_t previousX = 0, previousY = 0;
        uint8_t havePrevious = 0;
        if (nearZ < 220 && farZ >= 220 && count) {
            previousX = (int16_t)originalX[x][220];
            previousY = scene_y(2, 220u);
            havePrevious = 1;
        }
        for (i = 0; i < count; ++i) {
            uint16_t cz = (uint16_t)zsteps[i];
            int16_t currentX = (int16_t)originalX[x][cz];
            int16_t currentY = yvals[i];
            if (havePrevious) fast_line(previousX, previousY, currentX, currentY);
            previousX = currentX;
            previousY = currentY;
            havePrevious = 1;
        }
    }
}

static void thick_black_edge(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    fast_line(x0, y0, x1, y1);
    if (abs(x1 - x0) >= abs(y1 - y0))
        fast_line(x0, (int16_t)(y0 + 1), x1, (int16_t)(y1 + 1));
    else
        fast_line((int16_t)(x0 + 1), y0, (int16_t)(x1 + 1), y1);
}

static void fill_quad(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, int16_t x3, int16_t y3) {
    gfx_FillTriangle(x0, y0, x1, y1, x2, y2);
    gfx_FillTriangle(x0, y0, x2, y2, x3, y3);
}

/* Solid red prism faces with black two-pixel seams; touching cells share a face. */
static void draw_original_prism(uint8_t x, uint8_t xEnd, uint8_t y, uint8_t yEnd,
                               int16_t zNear, int16_t zFar) {
    uint16_t nearZ, farZ;
    int24_t rzNear = (int24_t)zNear - 200;
    int24_t rzFar = (int24_t)zFar - 200;
    int16_t nlx, nrx, nty, nby, flx, frx, fty, fby;
    if (x >= xEnd || xEnd > 3u || y >= yEnd || yEnd > 3u ||
        zNear < 1 || zFar >= (int16_t)PROJECTION_DEPTH || zNear >= zFar || rzFar < 20) return;
    nearZ = (uint16_t)(rzNear < 20 ? 220 : zNear);
    farZ = (uint16_t)zFar;
    nlx = (int16_t)originalX[x][nearZ];
    nrx = (int16_t)originalX[xEnd][nearZ];
    nty = scene_y(y, nearZ); nby = scene_y(yEnd, nearZ);
    flx = (int16_t)originalX[x][farZ];
    frx = (int16_t)originalX[xEnd][farZ];
    fty = scene_y(y, farZ); fby = scene_y(yEnd, farZ);

    if (zNear > (int16_t)BLOCK_DETAIL_DEPTH) {
        int16_t top = nty < nby ? nty : nby;
        uint16_t width = (uint16_t)(abs(nrx - nlx) + 1);
        uint8_t height = (uint8_t)(abs(nby - nty) + 1);
        gfx_SetColor(C_RED);
        gfx_FillRectangle(nlx, top, width, height);
        if (width > 2u && height > 2u && width >= 8u) {   /* tiny blocks: fill only, no border */
            gfx_SetColor(C_BLACK);
            gfx_Rectangle(nlx, top, width, height);
        }
        return;
    }

    gfx_SetColor(C_RED_HI);
    fill_quad(nlx, nty, nrx, nty, frx, fty, flx, fty);
    gfx_SetColor(C_RED_DARK);
    if (x == 0u)
        fill_quad(nrx, nty, frx, fty, frx, fby, nrx, nby);
    else
        fill_quad(nlx, nty, flx, fty, flx, fby, nlx, nby);
    gfx_SetColor(C_RED);
    fill_quad(nlx, nty, nrx, nty, nrx, nby, nlx, nby);

    gfx_SetColor(C_BLACK);
    thick_black_edge(flx, fty, frx, fty);
    thick_black_edge(nlx, nty, nrx, nty); thick_black_edge(nrx, nty, nrx, nby);
    thick_black_edge(nrx, nby, nlx, nby); thick_black_edge(nlx, nby, nlx, nty);
    thick_black_edge(nlx, nty, flx, fty); thick_black_edge(nrx, nty, frx, fty);
    /* Only outline the visible side face; the back and hidden bottom edges
       were showing through and made solid blocks read like transparent cubes. */
    if (x == 0u) {
        thick_black_edge(frx, fty, frx, fby);
        thick_black_edge(nrx, nby, frx, fby);
    } else {
        thick_black_edge(flx, fty, flx, fby);
        thick_black_edge(nlx, nby, flx, fby);
    }
}

static void draw_original_ramp(uint8_t x, uint8_t y, int16_t z) {
    int24_t rzNear = (int24_t)z - 200;
    int24_t rzFar = rzNear + 30;
    uint16_t zFar;
    int16_t nx0 = 0, nx1 = 0, ny = 0;
    int16_t fx0, fx1, fyTop, fyBottom;
    if (z < 1 || (int24_t)z + 30 >= (int24_t)PROJECTION_DEPTH || rzFar < 20) return;
    gfx_SetColor(C_CYAN);
    if (rzNear >= 20) {
        nx0 = (int16_t)originalX[x][(uint16_t)z];
        nx1 = (int16_t)originalX[x + 1u][(uint16_t)z];
        ny = scene_y(y + 1u, (uint16_t)z);
        gfx_HorizLine_NoClip((uint24_t)(nx0 < nx1 ? nx0 : nx1), ny, (uint24_t)(abs(nx1 - nx0) + 1));
    }
    zFar = (uint16_t)(z + 30);
    fx0 = (int16_t)originalX[x][zFar]; fx1 = (int16_t)originalX[x + 1u][zFar];
    fyTop = scene_y(y, zFar); fyBottom = scene_y(y + 1u, zFar);
    gfx_HorizLine_NoClip((uint24_t)(fx0 < fx1 ? fx0 : fx1), fyTop, (uint24_t)(abs(fx1 - fx0) + 1));
    gfx_HorizLine_NoClip((uint24_t)(fx0 < fx1 ? fx0 : fx1), fyBottom, (uint24_t)(abs(fx1 - fx0) + 1));
    gfx_VertLine_NoClip(fx0, (uint8_t)(fyTop < fyBottom ? fyTop : fyBottom), (uint8_t)(abs(fyBottom - fyTop) + 1));
    gfx_VertLine_NoClip(fx1, (uint8_t)(fyTop < fyBottom ? fyTop : fyBottom), (uint8_t)(abs(fyBottom - fyTop) + 1));
    if (rzNear >= 20) {
        fast_line(nx0, ny, fx0, fyTop); fast_line(nx1, ny, fx1, fyTop);
    } else {
        int16_t cx0 = (int16_t)(160 + (((int32_t)x - 1) * 100 * 79 - 3500) / 20);
        int16_t cx1 = (int16_t)(160 + (((int32_t)x) * 100 * 79 - 3500) / 20);
        int16_t cy = (int16_t)(120 + ((int32_t)(y + 1u) * 100 * 59 - 120) / 20 - CAMERA_Y_SHIFT - fallCameraPan);
        fast_line(clamp16(cx0, 0, 319), clamp16(cy, 0, 239), fx0, fyTop);
        fast_line(clamp16(cx1, 0, 319), clamp16(cy, 0, 239), fx1, fyTop);
    }
}

/* Circular wireframe ribs and nine rails create a lightweight tunnel ceiling.
 * The tube is ball-sized: at the play plane its interior is about 2.5 ball
 * widths, and its walls are the climbable strips. */
static void draw_tunnel_tube(void) {
    static const uint16_t ringDepth[2] = { 1000, 1450 };
    static const int16_t laneQ[9] = { 1120, 1215, 1320, 1425, 1500, 1575, 1680, 1785, 1880 };
    static const uint8_t arcHeight[9] = { 0, 38, 71, 92, 100, 92, 71, 38, 0 };
    int16_t previousX[9], previousY[9];
    int16_t previousRampLeftX = 0, previousRampLeftY = 0;
    int16_t previousRampRightX = 0, previousRampRightY = 0;
    uint8_t havePrevious = 0;
    uint32_t moved = travelled - tunnelStartTravel;
    uint8_t ring;
    for (ring = 0; ring < 2u; ++ring) {
        uint32_t depth = ringDepth[ring];
        uint16_t z;
        z = (uint16_t)(moved + 230u >= depth ? 230u : depth - moved);
        int16_t x[9], y[9];
        int16_t rampLeftX = project_lane_q(1120, z);
        int16_t rampRightX = project_lane_q(1880, z);
        int16_t rampLeftY, rampRightY;
        int16_t floorY = scene_y(2, z);
        int16_t roofBaseY = scene_y(0, z);
        int16_t radiusY = (int16_t)(floorY - roofBaseY);
        uint8_t i;
        for (i = 0; i < 9u; ++i) {
        int16_t rise = (int16_t)(((int24_t)radiusY * arcHeight[i]) / 100);
            x[i] = project_lane_q(laneQ[i], z);
            y[i] = clamp16(floorY - rise, 0, 239);
        }
        /* The climbable strips are the tube walls themselves, at the same Q span. */
        rampLeftY = clamp16(floorY - (radiusY * 61) / 100, 0, 239);
        rampRightY = rampLeftY;
        gfx_SetColor(C_NEON);
        for (i = 0; i < 8u; ++i) fast_line(x[i], y[i], x[i + 1u], y[i + 1u]);
        gfx_SetColor(C_CYAN);
        fast_line(x[0], y[0], x[3], y[3]);
        fast_line(x[8], y[8], x[5], y[5]);
        gfx_SetColor(C_RED);
        fast_line(x[0], y[0], rampLeftX, rampLeftY);
        fast_line(rampRightX, rampRightY, x[8], y[8]);
        if (havePrevious) {
            gfx_SetColor(C_GRID);
            for (i = 0; i < 9u; ++i) fast_line(previousX[i], previousY[i], x[i], y[i]);
            gfx_SetColor(C_RED);
            fast_line(previousRampLeftX, previousRampLeftY, rampLeftX, rampLeftY);
            fast_line(previousRampRightX, previousRampRightY, rampRightX, rampRightY);
        }
        memcpy(previousX, x, sizeof(x));
        memcpy(previousY, y, sizeof(y));
        previousRampLeftX = rampLeftX;
        previousRampLeftY = rampLeftY;
        previousRampRightX = rampRightX;
        previousRampRightY = rampRightY;
        havePrevious = 1;
    }
}

static void draw_original_gap(const TrackHazard *h) {
    uint16_t nearZ, farZ;
    uint8_t lane = (uint8_t)h->lane;
    int16_t nl, nr, fl, fr, ny, fy;
    if (h->depth < 230u || h->depth > MAX_STRUCTURE_DEPTH) return;
    nearZ = (uint16_t)(h->depth > 70u ? h->depth - 55u : 220u);
    farZ = (uint16_t)(h->depth + 55u);
    if (farZ >= PROJECTION_DEPTH) farZ = PROJECTION_DEPTH - 1u;
    nl = (int16_t)originalX[lane][nearZ];
    nr = (int16_t)originalX[lane + 1u][nearZ];
    fl = (int16_t)originalX[lane][farZ];
    fr = (int16_t)originalX[lane + 1u][farZ];
    ny = scene_y(2, nearZ);
    fy = scene_y(2, farZ);
    gfx_SetColor(C_BLACK);
    gfx_FillTriangle(nl, ny, nr, ny, fr, fy);
    gfx_FillTriangle(nl, ny, fr, fy, fl, fy);
    gfx_SetColor(C_RED);
    fast_line(nl, ny, nr, ny);
    fast_line(fl, fy, fr, fy);
    gfx_SetColor(C_NEON);
    fast_line(nl, ny, fl, fy);
    fast_line(nr, ny, fr, fy);
}

static void fps_begin_run(void) {
    fpsStartTicks = timer_Get(1);
    fpsFrameCount = 0;
    fpsPollDivider = 0;
    fpsShown = 0;
}

static void fps_update(void) {
    uint32_t now, elapsed, measured;
    ++fpsFrameCount;
    /* Poll the same 32 kHz timer used by simulation; clock() is unsupported or
     * inconsistently scaled on calculator targets and adds a library call. */
    if (++fpsPollDivider < 8u) return;
    fpsPollDivider = 0;
    now = timer_Get(1);
    elapsed = now - fpsStartTicks; /* unsigned subtraction also handles timer wrap */
    if (elapsed >= 32768u) {
        measured = (uint32_t)fpsFrameCount * 32768u / elapsed;
        fpsShown = (uint8_t)(measured > 99u ? 99u : measured);
        fpsFrameCount = 0;
        fpsStartTicks = now;
    }
}


static void draw_stars(uint8_t count) {
    uint8_t i;
    uint8_t twinklePhase = (uint8_t)(uiFrame / 10u);
    for (i = 0; i < count; ++i) {
        int16_t x = (int16_t)((i * 47u + 19u) % SW);
        int16_t y = (int16_t)((i * 31u + 7u) % 92u);
        if ((y > 33) && (x > 95) && (x < 225)) continue;
        y = (int16_t)(y + starOffsetY);
        if (y >= SH) continue;
        gfx_SetColor(((twinklePhase + i) % 3u) == 0 ? C_NEON_HI : C_STAR);
        gfx_SetPixel((uint24_t)x, (uint8_t)y);
    }
}


static uint8_t random_range(uint8_t count) {
    patternRng = patternRng * 1664525u + 1013904223u;
    return (uint8_t)((patternRng >> 16) % count);
}

static void add_original_object(uint8_t x, uint8_t y, int16_t z, uint8_t type) {
    uint8_t i, slot = objectCursor;
    for (i = 0; i < MAX_TRACK_HAZARDS; ++i) {
        slot = (uint8_t)((objectCursor + i) & (MAX_TRACK_HAZARDS - 1u));
        if (!trackHazards[slot].active) break;
    }
    /* A full pool must not silently replace a live obstacle. */
    if (i == MAX_TRACK_HAZARDS) return;
    if (type == HAZARD_BLOCK && z < patternLowestBlockZ) { patternLowestBlockZ = z; patternLowestBlockLane = x; }
    trackHazards[slot].lane = x;
    trackHazards[slot].heightLane = y;
    trackHazards[slot].depth = (uint16_t)(z < 0 ? 0 : z);
    trackHazards[slot].type = type;
    trackHazards[slot].active = 1;
    objectCursor = (uint8_t)((slot + 1u) & (MAX_TRACK_HAZARDS - 1u));
}

static void pattern_three_blocks(void) {
    add_original_object(0, 1, (int16_t)(330 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(2, 1, (int16_t)(330 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(1, 1, (int16_t)(540 + patternSpawnOffset), ORIGINAL_BLOCK);
}

static void pattern_tunnel(void) {
    uint8_t row = 0;
    int16_t z;
    for (z = 330; z < 571; z += 60, ++row) {
        uint8_t lane = (row & 1u) ? 2u : 0u;
        add_original_object(lane, 1, (int16_t)(z + patternSpawnOffset), ORIGINAL_BLOCK);
    }
    /* Exit ramp spans all three lanes: any line out of the tube launches. */
    add_original_object(0, 1, (int16_t)(600 + patternSpawnOffset), ORIGINAL_RAMP);
    add_original_object(1, 1, (int16_t)(600 + patternSpawnOffset), ORIGINAL_RAMP);
    add_original_object(2, 1, (int16_t)(600 + patternSpawnOffset), ORIGINAL_RAMP);
}

static void pattern_side_route(void) {
    add_original_object(1, 1, (int16_t)(470 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(1, 1, (int16_t)(530 + patternSpawnOffset), ORIGINAL_BLOCK);
}

static void pattern_ramp(void) {
    uint8_t x;
    int16_t z;
    /* Full-width ramp: any lane position launches, so the wall behind is
       always clearable instead of a 1-in-3 dice roll. */
    for (x = 0; x < 3; ++x)
        add_original_object(x, 1, (int16_t)(330 + patternSpawnOffset), ORIGINAL_RAMP);
    for (z = 390; z < 480; z += 30) {
        for (x = 0; x < 3; ++x)
            add_original_object(x, 1, (int16_t)(z + patternSpawnOffset), ORIGINAL_BLOCK);
    }
}

static void pattern_ramp_split(void) {
    uint8_t x;
    int16_t z;
    add_original_object(2, 1, (int16_t)(330 + patternSpawnOffset), ORIGINAL_RAMP);
    add_original_object(0, 1, (int16_t)(330 + patternSpawnOffset), ORIGINAL_RAMP);
    for (z = 360; z < 450; z += 30)
        add_original_object(1, 1, (int16_t)(z + patternSpawnOffset), ORIGINAL_BLOCK);
    /* The catch ramp now sits BEFORE the wall and spans all lanes: it fires
       with the ball freshly jumped, so the wall at 600 arrives mid-air. */
    for (x = 0; x < 3; ++x)
        add_original_object(x, 1, (int16_t)(560 + patternSpawnOffset), ORIGINAL_RAMP);
    for (x = 0; x < 3; ++x)
        add_original_object(x, 1, (int16_t)(600 + patternSpawnOffset), ORIGINAL_BLOCK);
}

/* Original-style single-lane slalom: two lanes remain open at every row. */
static void pattern_slalom(void) {
    add_original_object(0, 1, (int16_t)(350 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(2, 1, (int16_t)(540 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(1, 1, (int16_t)(730 + patternSpawnOffset), ORIGINAL_BLOCK);
}

/* Successive two-block gates leave a clear lane that shifts by one each time. */
static void pattern_crossfire(void) {
    add_original_object(0, 1, (int16_t)(350 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(1, 1, (int16_t)(350 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(0, 1, (int16_t)(550 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(2, 1, (int16_t)(550 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(1, 1, (int16_t)(750 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(2, 1, (int16_t)(750 + patternSpawnOffset), ORIGINAL_BLOCK);
}

/* Center lane, then both outer lanes, making a readable two-step lane swap. */
static void pattern_center_switch(void) {
    add_original_object(1, 1, (int16_t)(380 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(0, 1, (int16_t)(600 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(2, 1, (int16_t)(600 + patternSpawnOffset), ORIGINAL_BLOCK);
    add_original_object(1, 1, (int16_t)(820 + patternSpawnOffset), ORIGINAL_BLOCK);
}

/* A center-lane drop that carries the ball through a short lower-route dip. */
static void pattern_drop(void) {
    int16_t z = (int16_t)(500 + patternSpawnOffset);
    add_original_object(0, 1, z, ORIGINAL_BLOCK);
    add_original_object(1, 1, z, HAZARD_GAP);
    add_original_object(2, 1, z, ORIGINAL_BLOCK);
}

/* Hell only draws from patterns that always leave an open lane and never change speed. */
static uint8_t hell_pick(void) {
    static const uint8_t choices[5] = { 0, 2, 5, 6, 7 };
    return choices[random_range(5)];
}

static void spawn_pattern(uint8_t choice, int16_t offset) {
    patternSpawnOffset = offset;
    patternNotice = 50;
    patternLowestBlockZ = 32000;
    switch (choice) {
    case 0:
        pattern_three_blocks();
        patternNoticeText = "FATHERLESS TRIO";
        break;
    case 1:
        pattern_tunnel();
        patternNoticeText = "TUNNEL RUN";
        tunnelVisualDistance = TUNNEL_VISUAL_LENGTH;
        tunnelStartTravel = travelled;
        if (targetSpeed < 350u) targetSpeed = (uint16_t)(targetSpeed + 15u);       /* capped: was uncapped */
        if (targetMoveSpeed < 1500u) targetMoveSpeed = (uint16_t)(targetMoveSpeed + 10u);
        speedNotice = 35;
        boostTimer = 125;
        break;
    case 2: pattern_side_route(); patternNoticeText = "SIDE ROUTE"; break;
    case 3: pattern_ramp(); patternNoticeText = "RAMP RUN"; break;
    case 4: pattern_ramp_split(); patternNoticeText = "SPLIT RAMPS"; break;
    case 5: pattern_slalom(); patternNoticeText = "SLALOM"; break;
    case 6: pattern_crossfire(); patternNoticeText = "CROSS GATES"; break;
    case 7: pattern_center_switch(); patternNoticeText = "CENTER SWITCH"; break;
    default: pattern_drop(); patternNoticeText = "DROP AHEAD"; break;
    }
    /* Rare gem: parked in the lane of the first block row, just before it, so grabbing it is a gamble. */
    if (realmMode == REALM_NORMAL && patternLowestBlockZ < 32000 && random_range(GEM_CHANCE) == 0)
        add_original_object(patternLowestBlockLane, 1, (int16_t)(patternLowestBlockZ - 75), HAZARD_GEM);
}

/* Structures unlock as a world's tracks are built: tier 1 = the starter set,
 * tier 2 adds the gates, tier 3 adds tunnels, ramp runs, split ramps, the drop. */
static uint8_t pattern_min_build(uint8_t choice) {
    static const uint8_t need[9] = { 1, 3, 1, 3, 3, 1, 2, 2, 3 };
    return need[choice];
}

static void generate_original_pattern(void) {
    uint8_t built, choice;
    if (realmMode == REALM_HELL) { spawn_pattern(hell_pick(), 900); return; }
    if (dailyMode) { spawn_pattern(random_range(9), 900); return; }  /* daily: same hill for everyone */
    /* Same expression as run_planet(), inlined: run_planet() is defined further
       down the file, and generate_original_pattern runs before it in this order. */
    built = save.tracksBuilt[dailyMode ? 0u : save.activePlanet];
    if (built < 1u) built = 1u;
    do { choice = random_range(9); } while (pattern_min_build(choice) > built);
    spawn_pattern(choice, 900);
}

static uint8_t drawnBlocks[MAX_TRACK_HAZARDS];
static uint8_t blockLookup[128];

static uint8_t block_hash(uint8_t lane, uint8_t height, uint16_t depth) {
    return (uint8_t)(((uint24_t)depth * 37u + (uint24_t)lane * 11u + (uint24_t)height * 5u) & 127u);
}

static int16_t find_block_cell(uint8_t lane, uint8_t height, uint16_t depth) {
    uint8_t slot = block_hash(lane, height, depth);
    uint8_t probes;
    for (probes = 0; probes < 128u; ++probes) {
        uint8_t index = blockLookup[slot];
        const TrackHazard *h;
        if (index == 255u) return -1;
        h = &trackHazards[index];
        if (!drawnBlocks[index] && h->lane == lane && h->heightLane == height && h->depth == depth)
            return (int16_t)index;
        slot = (uint8_t)((slot + 1u) & 127u);
    }
    return -1;
}

/* Merge adjacent cells in all three axes, then draw the merged prisms in
 * painter's order (far first) so near faces cover the seams behind them. */
static void draw_merged_blocks(void) {
    uint8_t i, blockCount = 0;
    static struct { uint16_t zNear, zFar; uint8_t x0, x1, y0, y1; } order[64];
    uint8_t prismCount = 0;
    for (i = 0; i < MAX_TRACK_HAZARDS; ++i) {
        const TrackHazard *h = &trackHazards[i];
        if (h->active && h->type == HAZARD_BLOCK && h->depth <= drawDepthLimit &&
            h->lane >= 0 && h->lane <= 2 && h->heightLane <= 2u) ++blockCount;
    }
    if (!blockCount) return;
    memset(drawnBlocks, 0, sizeof(drawnBlocks));
    memset(blockLookup, 255, sizeof(blockLookup));
    /* Build a small open-addressed cell map so merging does not repeatedly scan
       all 64 hazards for every neighboring block. Duplicate cells are ignored. */
    for (i = 0; i < MAX_TRACK_HAZARDS; ++i) {
        const TrackHazard *h = &trackHazards[i];
        uint8_t slot, probes;
        if (!h->active || h->type != HAZARD_BLOCK || h->depth > drawDepthLimit ||
            h->lane < 0 || h->lane > 2 || h->heightLane > 2u) continue;
        slot = block_hash((uint8_t)h->lane, h->heightLane, h->depth);
        for (probes = 0; probes < 128u; ++probes) {
            uint8_t index = blockLookup[slot];
            if (index == 255u) {
                blockLookup[slot] = i;
                break;
            }
            if (trackHazards[index].lane == h->lane &&
                trackHazards[index].heightLane == h->heightLane &&
                trackHazards[index].depth == h->depth) {
                drawnBlocks[i] = 1;
                break;
            }
            slot = (uint8_t)((slot + 1u) & 127u);
        }
    }
    for (i = 0; i < MAX_TRACK_HAZARDS; ++i) {
        const TrackHazard *base = &trackHazards[i];
        uint8_t xStart, xEnd, yStart, yEnd, lane, height;
        uint16_t zNear, zFar;
        int16_t match;
        if (drawnBlocks[i] || !base->active || base->type != HAZARD_BLOCK ||
            base->depth > drawDepthLimit || base->lane < 0 || base->lane > 2 || base->heightLane > 2u) continue;
        xStart = (uint8_t)base->lane;
        xEnd = (uint8_t)(xStart + 1u);
        yStart = base->heightLane;
        yEnd = (uint8_t)(yStart + 1u);
        zNear = base->depth;
        zFar = (uint16_t)(zNear + 30u);
        drawnBlocks[i] = 1;

        while (xStart > 0u) {
            match = find_block_cell((uint8_t)(xStart - 1u), yStart, zNear);
            if (match < 0) break;
            drawnBlocks[(uint8_t)match] = 1;
            --xStart;
        }
        while (xEnd < 3u) {
            match = find_block_cell(xEnd, yStart, zNear);
            if (match < 0) break;
            drawnBlocks[(uint8_t)match] = 1;
            ++xEnd;
        }

        while (yStart > 0u) {
            uint8_t rowIndices[3], rowCount = 0;
            for (lane = xStart; lane < xEnd; ++lane) {
                match = find_block_cell(lane, (uint8_t)(yStart - 1u), zNear);
                if (match < 0) break;
                rowIndices[rowCount++] = (uint8_t)match;
            }
            if (rowCount != (uint8_t)(xEnd - xStart)) break;
            for (lane = 0; lane < rowCount; ++lane) drawnBlocks[rowIndices[lane]] = 1;
            --yStart;
        }
        while (yEnd < 3u) {
            uint8_t rowIndices[3], rowCount = 0;
            for (lane = xStart; lane < xEnd; ++lane) {
                match = find_block_cell(lane, yEnd, zNear);
                if (match < 0) break;
                rowIndices[rowCount++] = (uint8_t)match;
            }
            if (rowCount != (uint8_t)(xEnd - xStart)) break;
            for (lane = 0; lane < rowCount; ++lane) drawnBlocks[rowIndices[lane]] = 1;
            ++yEnd;
        }

        while (zNear >= 30u) {
            uint8_t rowIndices[12], rowCount = 0;
            uint16_t previousDepth = (uint16_t)(zNear - 30u);
            for (height = yStart; height < yEnd; ++height) {
                for (lane = xStart; lane < xEnd; ++lane) {
                    match = find_block_cell(lane, height, previousDepth);
                    if (match < 0) break;
                    rowIndices[rowCount++] = (uint8_t)match;
                }
                if (match < 0) break;
            }
            if (rowCount != (uint8_t)((xEnd - xStart) * (yEnd - yStart))) break;
            for (lane = 0; lane < rowCount; ++lane) drawnBlocks[rowIndices[lane]] = 1;
            zNear = previousDepth;
            zFar = (uint16_t)(zNear + 30u);
        }
        while (zFar + 30u <= MAX_STRUCTURE_DEPTH) {
            uint8_t rowIndices[12], rowCount = 0;
            for (height = yStart; height < yEnd; ++height) {
                for (lane = xStart; lane < xEnd; ++lane) {
                    match = find_block_cell(lane, height, zFar);
                    if (match < 0) break;
                    rowIndices[rowCount++] = (uint8_t)match;
                }
                if (match < 0) break;
            }
            if (rowCount != (uint8_t)((xEnd - xStart) * (yEnd - yStart))) break;
            for (lane = 0; lane < rowCount; ++lane) drawnBlocks[rowIndices[lane]] = 1;
            zFar = (uint16_t)(zFar + 30u);
        }
        order[prismCount].zNear = zNear;
        order[prismCount].zFar = zFar;
        order[prismCount].x0 = xStart;  order[prismCount].x1 = xEnd;
        order[prismCount].y0 = yStart;  order[prismCount].y1 = yEnd;
        ++prismCount;
    }
    /* Painter's order: far prisms first, so near faces cover the seams of the
       prisms behind them. This is what removes the extra "3rd dimension" lines. */
    {
        uint8_t idx[64];
        uint8_t n = prismCount, a, b;
        for (a = 0; a < n; ++a) idx[a] = a;
        for (a = 1; a < n; ++a) {
            uint8_t key = idx[a];
            b = a;
            while (b > 0 && order[idx[b - 1u]].zNear < order[key].zNear) {
                idx[b] = idx[b - 1u];
                --b;
            }
            idx[b] = key;
        }
        for (a = 0; a < n; ++a) {
            uint8_t k = idx[a];
            draw_original_prism(order[k].x0, order[k].x1, order[k].y0, order[k].y1,
                                (int16_t)order[k].zNear, (int16_t)order[k].zFar);
        }
    }
}

/* Draw non-block events in the original's 3D world coordinates. */
static void draw_gem(const TrackHazard *h) {
    uint8_t lane = (uint8_t)h->lane;
    uint16_t z = h->depth;
    int16_t x0, x1, cx, cy, s;
    if (z < 230u || z > drawDepthLimit || z >= PROJECTION_DEPTH) return;
    x0 = (int16_t)originalX[lane][z];
    x1 = (int16_t)originalX[lane + 1u][z];
    cx = (int16_t)((x0 + x1) / 2);
    s = (int16_t)((x1 - x0) / 6 + 2);
    cy = (int16_t)((int16_t)scene_y(2, z) - s - 1);
    gfx_SetColor(C_CYAN);
    gfx_FillTriangle(cx, (int16_t)(cy - s), (int16_t)(cx - s), cy, (int16_t)(cx + s), cy);
    gfx_FillTriangle((int16_t)(cx - s), cy, (int16_t)(cx + s), cy, cx, (int16_t)(cy + s));
    gfx_SetColor(C_WHITE);
    fast_line(cx, (int16_t)(cy - s), (int16_t)(cx - s), cy);
    fast_line((int16_t)(cx - s), cy, cx, (int16_t)(cy + s));
    fast_line(cx, (int16_t)(cy + s), (int16_t)(cx + s), cy);
    fast_line((int16_t)(cx + s), cy, cx, (int16_t)(cy - s));
}

static void draw_track_hazard(const TrackHazard *h) {
    if (!h->active) return;
    if (h->type == HAZARD_GAP) {
        draw_original_gap(h);
        return;
    }
    if (h->type == HAZARD_GEM) {
        draw_gem(h);
        return;
    }
    if (h->type == HAZARD_RAMP)
        draw_original_ramp((uint8_t)h->lane, h->heightLane, (int16_t)h->depth);
}


static void draw_wire_sphere(int16_t x, int16_t y, uint8_t r) {
    static const int8_t phaseTable[8] = { 0, 38, 71, 92, 100, 92, 71, 38 };
    uint8_t phase = (uint8_t)((frameNumber / 2u) & 7u);
    int16_t bandW = (int16_t)(3 + (phaseTable[phase] * r) / 100);
    int16_t shift = (int16_t)((phaseTable[(phase + 2u) & 7u] * r) / 220);
    gfx_SetColor(C_BLACK);
    gfx_FillCircle(x, y, r);
    gfx_SetColor(C_NEON);
    gfx_Circle(x, y, r);
    /* Curved surface bands wrap a solid black sphere and shift as it rolls. */
    gfx_Ellipse(x, y, r, (uint8_t)(r / 3u));
    gfx_Ellipse((int16_t)(x + shift), y, (uint24_t)bandW, r - 2u);
    gfx_Ellipse((int16_t)(x - shift), y, (uint24_t)(bandW / 2 + 2), (uint8_t)((r * 2u) / 3u));
    gfx_SetColor(C_NEON_HI);
    gfx_SetPixel((uint24_t)(x - r / 3), (uint8_t)(y - r / 3));
}

static void draw_wire_cube(int16_t x, int16_t y, uint8_t r) {
    int16_t x0 = (int16_t)(x - r), x1 = (int16_t)(x + r);
    int16_t y0 = (int16_t)(y - r), y1 = (int16_t)(y + r);
    int16_t dx = (int16_t)(r * 2 / 3), dy = (int16_t)(r * 2 / 3);
    gfx_SetColor(C_NEON);
    gfx_Rectangle(x0, y0, r * 2, r * 2);
    gfx_Rectangle((int16_t)(x0 + dx), (int16_t)(y0 - dy), r * 2, r * 2);
    fast_line(x0, y0, (int16_t)(x0 + dx), (int16_t)(y0 - dy));
    fast_line(x1, y0, (int16_t)(x1 + dx), (int16_t)(y0 - dy));
    fast_line(x0, y1, (int16_t)(x0 + dx), (int16_t)(y1 - dy));
    fast_line(x1, y1, (int16_t)(x1 + dx), (int16_t)(y1 - dy));
}

static void draw_wire_pyramid(int16_t x, int16_t y, uint8_t r) {
    int16_t apexX = (int16_t)(x + r / 3), apexY = (int16_t)(y - r);
    int16_t lx = (int16_t)(x - r), rx = (int16_t)(x + r), by = (int16_t)(y + r / 2);
    gfx_SetColor(C_NEON);
    fast_line(apexX, apexY, lx, by);
    fast_line(apexX, apexY, rx, by);
    fast_line(apexX, apexY, (int16_t)(x - r / 3), (int16_t)(y + r));
    fast_line(lx, by, (int16_t)(x - r / 3), (int16_t)(y + r));
    fast_line((int16_t)(x - r / 3), (int16_t)(y + r), rx, by);
    fast_line(lx, by, rx, by);
}

static void draw_player(int16_t x, int16_t y) {
    uint8_t r = (uint8_t)(save.activeShape == SHAPE_CUBE ? 18 : (save.activeShape == SHAPE_PYRAMID ? 20 : 23));
    uint8_t i;
    if (save.settings & SET_TRAIL) {
        for (i = 0; i < trailCount; ++i) {
            gfx_SetColor(C_GRID_DIM);
            gfx_FillCircle(trailX[i], (int16_t)(y + 11 + i), (uint24_t)(2u + i));
        }
    }
    gfx_SetColor(C_BG_DARK);
    gfx_FillEllipse(x, (int16_t)(y + 17), (uint24_t)(r + 7u), 4);
    if (save.activeShape == SHAPE_CUBE) draw_wire_cube(x, y, r);
    else if (save.activeShape == SHAPE_PYRAMID) draw_wire_pyramid(x, y, r);
    else {
        uint8_t frame = (uint8_t)((animationDistance / 16u) % 6u);
        gfx_SetTransparentColor(255);
#if PLAYER_TILT
        int16_t angle = clamp16((int16_t)(160 - x) / 32, -3, 3);
        uint8_t angleByte = (uint8_t)angle;
        if (angleByte && (x < 20 || x > SW - 20 || y < 20 || y > SH - 20)) {
            gfx_RotatedTransparentSprite(originalPlayerSprites[frame], (int16_t)(x - 20), (int16_t)(y - 20), angleByte);
        } else if (angleByte) {
            gfx_RotatedTransparentSprite_NoClip(originalPlayerSprites[frame], (int16_t)(x - 20), (int16_t)(y - 20), angleByte);
        } else if (x < 20 || x > SW - 20 || y < 20 || y > SH - 20) {
            gfx_TransparentSprite(originalPlayerSprites[frame], (int16_t)(x - 20), (int16_t)(y - 20));
        } else {
            gfx_TransparentSprite_NoClip(originalPlayerSprites[frame], (uint24_t)(x - 20), (uint8_t)(y - 20));
        }
#else
        /* Rotated sprite blits are expensive on the calculator. Keep the
         * original rolling animation and use the clipped fast path by default. */
        if (x < 20 || x > SW - 20 || y < 20 || y > SH - 20)
            gfx_TransparentSprite(originalPlayerSprites[frame], (int16_t)(x - 20), (int16_t)(y - 20));
        else
            gfx_TransparentSprite_NoClip(originalPlayerSprites[frame], (uint24_t)(x - 20), (uint8_t)(y - 20));
#endif
    }
}

static void draw_wing(int16_t x, int16_t y, int8_t s) {
    fast_line(x + s * 12, y - 2, x + s * 27, y - 13);
    fast_line(x + s * 27, y - 13, x + s * 37, y - 10);
    fast_line(x + s * 13, y + 3, x + s * 31, y + 1);
    fast_line(x + s * 31, y + 1, x + s * 39, y + 6);
    fast_line(x + s * 10, y + 8, x + s * 26, y + 13);
}

static void draw_angel_wings(int16_t x, int16_t y) {
    gfx_SetColor(C_NEON_HI);
    draw_wing(x, y, -1);
    draw_wing(x, y, 1);
}

static uint16_t current_speed(void) {
    uint32_t s = targetSpeed;
    if (realmMode == REALM_HEAVEN) s += s / 4u;
    else if (realmMode == REALM_HELL) s = s * 3u / 5u;
    if (boostTimer) s += 5u;
    if (save.cheatsUnlocked && (save.settings & SET_SPEED_CHEAT)) s += 100u;
    if (save.cheatsUnlocked && (save.settings & SET_SLOW_CHEAT)) s = s * 3u / 4u;
    return (uint16_t)(s / 10u);
}
static uint8_t heaven_visited(void) {
    return (uint8_t)((save.unlockedPlanets >> HEAVEN_WORLD_INDEX) & 1u);
}

/* Daily runs always use NEON rules so every player faces the same hill. */
static uint8_t run_planet(void) {
    return dailyMode ? 0u : save.activePlanet;
}

static uint8_t lore_has(uint8_t i) {
    return (uint8_t)((save.loreMask & (uint16_t)(1u << i)) != 0);
}

static void unlock_lore(uint8_t i) {
    uint16_t bit = (uint16_t)(1u << i);
    if (save.loreMask & bit) return;
    save.loreMask |= bit;
    loreNew |= bit;
    save_game();
}

static void today_date(uint8_t *day, uint8_t *month, uint16_t *year) {
    boot_GetDate(day, month, year);
}

static uint8_t daily_is_today(void) {
    uint8_t d, m;
    uint16_t y;
    today_date(&d, &m, &y);
    return (uint8_t)(save.dailyDay == d && save.dailyMonth == m && save.dailyYear == y);
}

static uint32_t daily_best_today(void) {
    return daily_is_today() ? save.dailyBest : 0u;
}

static uint32_t daily_seed(void) {
    uint8_t d, m;
    uint16_t y;
    today_date(&d, &m, &y);
    return (((uint32_t)y * 512u + (uint32_t)m * 32u + d) * 2654435761u) ^ 0x534C4F50u;
}

static void format_date(char *out, uint8_t m, uint8_t d, uint16_t y) {
    out[0] = (char)('0' + (m / 10u) % 10u); out[1] = (char)('0' + m % 10u); out[2] = '/';
    out[3] = (char)('0' + (d / 10u) % 10u); out[4] = (char)('0' + d % 10u); out[5] = '/';
    out[6] = (char)('0' + (y / 1000u) % 10u); out[7] = (char)('0' + (y / 100u) % 10u);
    out[8] = (char)('0' + (y / 10u) % 10u);   out[9] = (char)('0' + y % 10u);
    out[10] = '\0';
}

/* Codex entries. The wording is a first pass; swap in your own lore freely. */
static const char *const loreTitle[LORE_COUNT] = {
    "FIRST DESCENT", "THE GRID", "ASCENSION", "THE FALL",
    "BLOOD IS FUEL", "GRACE", "THE VOID", "THE DAILY HILL"
};
static const char *const loreHint[LORE_COUNT] = {
    "DIE ONCE", "SCORE 20 IN ONE RUN", "REACH HEAVEN", "FALL FROM HEAVEN",
    "NEAR-MISS IN HELL", "SURVIVE HELL", "SCORE 10 ON VOID", "FINISH A DAILY RUN"
};
static const char *const loreText[LORE_COUNT][3] = {
    { "Every roll begins at the top.", "Nobody remembers who pushed.", "The slope does not answer." },
    { "Green lines hold the world still.", "Break one, and the world forgets", "you were ever standing on it." },
    { "Five walls climbed. The air thins.", "A golden gate opens for a sphere.", "It was never built for one." },
    { "Wings are a loan, never a gift.", "Heaven always comes to collect.", "Down is the only way left." },
    { "Below the green, the red begins.", "Here the nearer the end you run,", "the brighter the engine burns." },
    { "Five seconds. That is all it asks.", "Persevere, and the red lets go.", "Prevail, and it remembers you." },
    { "Past the cyan, the lines stop", "agreeing on which way is down.", "Steer by what you cannot see." },
    { "The same hill returns each day.", "Everyone rolls the same slope,", "and no two fall alike." }
};

static void reset_run(void) {
    uint8_t i;
    travelled = score = runScore = frameNumber = 0;
    scoreDistance = 0;
    boostTimer = 0;
    airtime = 0;
    verticalVelocity = 0;
    jumpLockout = 0;
    warningFlash = 0;
    patternNotice = 0;
    dropping = 0;
    dropFrames = 0;
    slalomRoadTimer = 0;
    slalomCycle = 0;
    crashed = 0;
    falling = 0;
    fallElapsedTicks = 0;
    fallCameraPan = 0;
    deathPhase = 0;
    deathCounter = 0;
    coinCounter = 0;
    coinPayout = 0;
    payoutApplied = 0;
    realmMode = REALM_NORMAL;
    eventType = EVENT_NONE;
    eventStep = eventFrame = 0;
    eventElapsedTicks = realmElapsedTicks = hellElapsedTicks = 0;
    wallRideSide = 0;
    wallRideProgress = wallRideCooldown = 0;
    trailCount = 0;
    runNearMisses = 0; runGems = 0; hellBonus = 0; hellSurvived = 0; nearMissFrame = 0;
    fps_begin_run();
    for (i = 0; i < MAX_TRACK_HAZARDS; ++i) {
        trackHazards[i].active = 0;
    }
    /* Keep both 480-unit segments aligned to the 60-unit mesh spacing. */
    grounds[0].z = 200; grounds[1].z = 680;
    grounds[0].zlen = grounds[1].zlen = 480;
    objectCursor = 0;
    /* Spawn drop: the ball enters high inside the ball-sized tube and falls
       onto the road under normal gravity, like the original's opening seconds.
       X = 144 is dead centre of the tube's ~[134,195] interior at the play plane. */
    originalPlayerX = 144; originalPlayerY = 48; originalPlayerVelocity = 0;
    wallClimbCount = 0;   /* five climbs must happen within ONE run, never across deaths */
    targetSpeed = 250; targetMoveSpeed = 1300;
    speedFraction = moveFraction = animationDistance = 0;
    originalTicks = timer_Get(1);
    if (dailyMode) patternRng = daily_seed();
    else patternRng ^= originalTicks + (uint32_t)(uiFrame * 2654435761u);
    patternSpawnOffset = 900;
    tunnelVisualDistance = TUNNEL_VISUAL_LENGTH;
    tunnelStartTravel = travelled;
    /* Tunnel-end ramp spans all three lanes: any entry line launches. */
    add_original_object(0, 1, (int16_t)(420 + patternSpawnOffset), ORIGINAL_RAMP);
    add_original_object(1, 1, (int16_t)(420 + patternSpawnOffset), ORIGINAL_RAMP);
    add_original_object(2, 1, (int16_t)(420 + patternSpawnOffset), ORIGINAL_RAMP);
    patternNoticeText = "TUNNEL + RAMP";
    patternNotice = 50;
    if (run_planet() == 1u) patternNoticeText = "SLIPPERY ICE";
    else if (run_planet() == 2u) patternNoticeText = "FOGGY VOID";
    speedNotice = 0;
}

static void begin_death(void) {
    runScore = score;
    if (runScore > save.bestScore) save.bestScore = runScore;
    if (dailyMode) {
        uint8_t d, m;
        uint16_t y;
        today_date(&d, &m, &y);
        if (!daily_is_today()) {
            save.dailyDay = d; save.dailyMonth = m; save.dailyYear = y;
            save.dailyBest = 0;
        }
        if (runScore > save.dailyBest) save.dailyBest = runScore;
        unlock_lore(7);
    } else if (save.activePlanet < 3u && runScore > save.worldBest[save.activePlanet]) {
        save.worldBest[save.activePlanet] = runScore;
    }
    unlock_lore(0);
    if (runScore >= 20u) unlock_lore(1);
    if (!dailyMode && save.activePlanet == 2u && runScore >= 10u) unlock_lore(6);
    deathCounter = 0;
    coinCounter = 0;
    deathPhase = 0;
    deathMenuIndex = 0;
    heavenSeq = 0;
    payoutApplied = 0;
    coinPayout = runScore;
    if (coinPayout > COIN_CAP) coinPayout = COIN_CAP;
    if ((save.upgrades & 0x01u) || (save.cheatsUnlocked && (save.settings & SET_DOUBLE_COIN))) {
        if (coinPayout < COIN_CAP / 2u) coinPayout *= 2u;
        else coinPayout = COIN_CAP;
    }
    screen = SCREEN_DEATH;
}

static uint32_t credit_phase_coins(uint32_t amount) {
    if ((save.upgrades & 0x01u) || (save.cheatsUnlocked && (save.settings & SET_DOUBLE_COIN))) {
        if (amount < COIN_CAP / 2u) amount *= 2u;
        else amount = COIN_CAP;
    }
    if (amount > COIN_CAP) amount = COIN_CAP;
    if (save.coins < COIN_CAP - amount) save.coins += amount;
    else save.coins = COIN_CAP;
    save_game();
    return amount;
}

static uint32_t bank_phase_score(void) {
    uint32_t amount = score;
    if (score > save.bestScore) save.bestScore = score;
    if (realmMode == REALM_HEAVEN) {
        if (score > save.worldBest[3]) save.worldBest[3] = score;
    } else if (!dailyMode && save.activePlanet < 3u && score > save.worldBest[save.activePlanet]) {
        save.worldBest[save.activePlanet] = score;
    }
    return credit_phase_coins(amount);
}

static void start_ascension_event(uint8_t firstStep) {
    wallClimbCount = 0;
    eventType = EVENT_ASCENSION;
    eventStep = firstStep;
    eventFrame = 0;
    eventElapsedTicks = 0;
    originalTicks = timer_Get(1);
    screen = SCREEN_EVENT;
}

static void enter_heaven(void) {
    score = runScore = 0;
    scoreDistance = 0;
    realmMode = REALM_HEAVEN;
    dailyMode = 0;
    if (!heaven_visited()) {
        save.unlockedPlanets |= (uint8_t)(1u << HEAVEN_WORLD_INDEX);
        save_game();
        unlock_lore(2);
    }
    realmElapsedTicks = 0;
    falling = dropping = 0;
    crashed = 0;
    wallRideSide = 0;
    wallRideCooldown = 0;
    set_palette();
    eventType = EVENT_NONE;
    screen = SCREEN_GAME;
    originalTicks = timer_Get(1);
}

/* Worlds-screen slide and the death-screen F1..F5 code both start here: a fresh run,
 * the ascension cutscene from its white-out onward, then HEAVEN. */
static void begin_heaven_ascent(void) {
    dailyMode = 0;
    reset_run();
    start_ascension_event(1);
}

static void start_wings_event(void) {
    eventType = EVENT_WINGS;
    eventStep = 0;
    eventFrame = 0;
    eventElapsedTicks = 0;
    screen = SCREEN_EVENT;
    originalTicks = timer_Get(1);
}

static void start_hell_descent(void) {
    lastPhasePayout = bank_phase_score();
    unlock_lore(3);
    score = runScore = 0;
    scoreDistance = 0;
    realmMode = REALM_HELL;
    set_palette();
    eventType = EVENT_HELL_DESCENT;
    eventStep = eventFrame = 0;
    eventElapsedTicks = 0;
    screen = SCREEN_EVENT;
    originalTicks = timer_Get(1);
}

/* Everything left over from the Heaven run (hazards, ball position, camera, speed) is wiped,
 * so Hell always starts from a known, fair state instead of wherever the ball fell. */
static void clear_track(void) {
    uint8_t i;
    for (i = 0; i < MAX_TRACK_HAZARDS; ++i) trackHazards[i].active = 0;
    objectCursor = 0;
    grounds[0].z = 200; grounds[1].z = 680;
    grounds[0].zlen = grounds[1].zlen = 480;
    slalomRoadTimer = 0;
    slalomCycle = 0;
    dropping = 0;
    dropFrames = 0;
    tunnelVisualDistance = 0;
    wallRideSide = 0;
    wallRideProgress = 0;
}

static void enter_hell(void) {
    realmMode = REALM_HELL;
    hellElapsedTicks = 0;
    hellBonus = 0;
    hellSurvived = 0;
    falling = dropping = crashed = 0;
    fallElapsedTicks = 0;
    fallCameraPan = 0;
    clear_track();
    originalPlayerX = 144;      /* centre lane */
    originalPlayerY = 180;
    originalPlayerVelocity = 0;
    verticalVelocity = 0;
    airtime = 0;
    jumpLockout = 0;
    warningFlash = 0;
    boostTimer = 0;
    speedNotice = 0;
    targetSpeed = 250;
    targetMoveSpeed = 1300;
    speedFraction = moveFraction = 0;
    trailCount = 0;
    /* Two readable patterns: the first row arrives after ~1.5 s, so there is time to settle. */
    spawn_pattern(hell_pick(), HELL_FIRST_ROW_OFFSET);
    spawn_pattern(hell_pick(), (int16_t)(HELL_FIRST_ROW_OFFSET + 480));
    patternNoticeText = "SURVIVE";
    patternNotice = 50;
    set_palette();
    eventType = EVENT_NONE;
    screen = SCREEN_GAME;
    originalTicks = timer_Get(1);
}

static void return_from_hell(void) {
    uint32_t reward = (hellElapsedTicks * 100UL) / 32768UL + hellBonus;
    hellSurvived = (uint8_t)(hellElapsedTicks >= HELL_MAX_TICKS);
    if (hellSurvived) unlock_lore(5);
    lastPhasePayout = credit_phase_coins(reward);
    score = runScore = 0;
    scoreDistance = 0;
    realmMode = REALM_NORMAL;
    set_palette();
    eventType = EVENT_RETURN;
    eventFrame = 0;
    eventElapsedTicks = 0;
    screen = SCREEN_EVENT;
    originalTicks = timer_Get(1);
}

/* Climbing happens on the ball-sized tube's own walls: the engage zones and
 * the in-climb clamp band are derived from the tube's Q span (1120..1880),
 * so the climbable strips sit exactly where the ball actually runs. */
static uint8_t update_wall_ride(uint16_t speed) {
    int16_t centerX = (int16_t)(originalPlayerX + 20);
    int16_t wallLeft = project_lane_q(1120, 300u);
    int16_t wallRight = project_lane_q(1880, 300u);
    int16_t strip = (int16_t)((wallRight - wallLeft) / 4);
    uint8_t finished = 0;
    if (wallRideCooldown > speed) wallRideCooldown = (uint16_t)(wallRideCooldown - speed);
    else wallRideCooldown = 0;
    if (wallRideSide) {
        uint16_t next = (uint16_t)(wallRideProgress + speed);
        if (wallRideSide == 1u)
            originalPlayerX = clamp16(originalPlayerX,
                                      (int16_t)(wallLeft - 8),
                                      (int16_t)(wallLeft + strip));
        else
            originalPlayerX = clamp16(originalPlayerX,
                                      (int16_t)(wallRight - strip),
                                      (int16_t)(wallRight + 8));
        wallRideProgress = next > WALL_RIDE_TOTAL ? WALL_RIDE_TOTAL : next;
        if (wallRideProgress <= WALL_RIDE_CLIMB) {
            originalPlayerY = (int16_t)(180 - (wallRideProgress * 45u) / WALL_RIDE_CLIMB);
        } else if (wallRideProgress <= WALL_RIDE_TOP) {
            originalPlayerY = 135;
        } else {
            originalPlayerY = (int16_t)(135 + ((wallRideProgress - WALL_RIDE_TOP) * 45u) /
                                       (WALL_RIDE_TOTAL - WALL_RIDE_TOP));
        }
        verticalVelocity = 0;
        if (wallRideProgress >= WALL_RIDE_TOTAL) {
            wallRideSide = 0;
            wallRideProgress = 0;
            wallRideCooldown = WALL_RIDE_COOLDOWN;
            if (wallClimbCount < 5u) ++wallClimbCount;
            patternNoticeText = "WALL RAMP CLEARED";
            patternNotice = 70;
            originalPlayerY = 180;
            finished = 1;
            if (wallClimbCount >= 5u && realmMode == REALM_NORMAL) {
                start_ascension_event(0);
                return 2;
            }
        }
        return finished;
    }
    if (realmMode != REALM_NORMAL || wallRideCooldown || tunnelVisualDistance <= WALL_RIDE_TOTAL + 40u)
        return 0;
    if (centerX > wallLeft - 10 && centerX < wallLeft + strip) {
        wallRideSide = 1;
    } else if (centerX > wallRight - strip && centerX < wallRight + 10) {
        wallRideSide = 2;
    }
    if (wallRideSide) {
        wallRideProgress = 0;
        patternNoticeText = wallRideSide == 1u ? "LEFT WALL RAMP" : "RIGHT WALL RAMP";
        patternNotice = 60;
        return 1;
    }
    return 0;
}

static void update_game(const InputFrame *keys) {
    uint8_t i;
    uint32_t now, dt;
    uint32_t runSpeed, moveSpeed;
    uint32_t accumulated;
    uint16_t spd, mspeed;
    uint8_t slippery;
    if (falling) {
        now = timer_Get(1); dt = now - originalTicks; originalTicks = now;
        if (dt > 8192u) dt = 8192u;
        if (!dt) dt = 1u;
        fallElapsedTicks += dt;
        if (realmMode == REALM_HEAVEN) realmElapsedTicks += dt;
        originalPlayerY = (int16_t)(originalPlayerY + ((int32_t)verticalVelocity * (int32_t)dt) / 32768);
        verticalVelocity = (int16_t)(verticalVelocity + (1024L * (int32_t)dt) / 32768);
        if (fallElapsedTicks >= 5UL * 32768UL) {
            if (realmMode == REALM_HEAVEN) start_hell_descent();
            else begin_death();
        } else {
            fallCameraPan = (int16_t)((fallElapsedTicks * 48UL) / (5UL * 32768UL));
        }
        return;
    }
    now = timer_Get(1); dt = now - originalTicks; originalTicks = now;
    if (dt > 8192u) dt = 8192u;
    if (dt == 0u) dt = 1u;
    if (realmMode == REALM_HEAVEN) {
        realmElapsedTicks += dt;
        if (realmElapsedTicks >= HEAVEN_DURATION_TICKS) {
            lastPhasePayout = bank_phase_score();
            start_wings_event();
            return;
        }
    } else if (realmMode == REALM_HELL) {
        hellElapsedTicks += dt;
        if (hellElapsedTicks >= HELL_MAX_TICKS) {
            return_from_hell();
            return;
        }
    }
    runSpeed = targetSpeed;
    moveSpeed = targetMoveSpeed;
    if (realmMode == REALM_HEAVEN) {
        runSpeed += runSpeed / 4u;
        moveSpeed += moveSpeed / 3u;
    } else if (realmMode == REALM_HELL) {
        runSpeed = runSpeed * 3u / 5u;
        moveSpeed = moveSpeed * 3u / 5u;
    }
    if (boostTimer) runSpeed += 5u;
    if (save.cheatsUnlocked && (save.settings & SET_SPEED_CHEAT)) { runSpeed += 100u; moveSpeed += 300u; }
    if (save.cheatsUnlocked && (save.settings & SET_SLOW_CHEAT)) { runSpeed = runSpeed * 3u / 4u; moveSpeed = moveSpeed * 3u / 4u; }
    slippery = (uint8_t)(realmMode == REALM_NORMAL && run_planet() == 1u);
    if (slippery) moveSpeed = moveSpeed * 3u / 5u;   /* CYAN: slower to steer, slower to stop */

    /* Match the reference controls: acceleration while held, then frame-time
     * friction. Position, road travel, and ramps all use the 32 kHz clock. */
    mspeed = (uint16_t)((moveSpeed * dt + moveFraction) >> 15);
    moveFraction = (moveSpeed * dt + moveFraction) & 0x7FFFu;
    if (keys->rightHeld) originalPlayerVelocity += (int16_t)mspeed;
    if (keys->leftHeld) originalPlayerVelocity -= (int16_t)mspeed;
    originalPlayerVelocity = clamp16(originalPlayerVelocity, -1400, 1400);
    originalPlayerX = (int16_t)(originalPlayerX + ((int32_t)originalPlayerVelocity * (int32_t)dt) / 32768);
    {
        int32_t friction = 32768L - (slippery ? 3L : 5L) * (int32_t)dt;
        if (friction < 0) friction = 0;
        originalPlayerVelocity = (int16_t)(((int32_t)originalPlayerVelocity * friction) / 32768L);
    }
    originalPlayerX = clamp16(originalPlayerX, -120, 420);

    accumulated = speedFraction + runSpeed * dt;
    spd = (uint16_t)(accumulated >> 15);
    speedFraction = accumulated & 0x7FFFu;
    if (!spd) spd = 1;
    animationDistance = (uint16_t)(animationDistance + spd);
    ++frameNumber;
    travelled += spd;
    scoreDistance = (uint16_t)(scoreDistance + spd);
    if (scoreDistance >= SCORE_DISTANCE_STEP) {
        scoreDistance = (uint16_t)(scoreDistance - SCORE_DISTANCE_STEP);
        score += realmMode == REALM_HEAVEN ? 2u : 1u;
    }
    slalomCycle = (uint16_t)(slalomCycle + spd);
    while (slalomCycle >= 840u) slalomCycle = (uint16_t)(slalomCycle - 840u);

    if (dropping) {
        if (dropFrames < 9u) originalPlayerY = (int16_t)(originalPlayerY + 3);
        else if (dropFrames < 27u) originalPlayerY = 207;
        else if (dropFrames < 41u) originalPlayerY = (int16_t)(originalPlayerY - 2);
        else {
            originalPlayerY = 180;
            dropping = 0;
            slalomRoadTimer = SLALOM_ROAD_LENGTH;
            originalPlayerX = (int16_t)(project_lane_q((int16_t)(1500 + slalom_offset_q(300u)), 300u) - 20);
            originalPlayerVelocity = 0;
        }
        ++dropFrames;
        verticalVelocity = 0;
    } else {
        originalPlayerY = (int16_t)(originalPlayerY + ((int32_t)verticalVelocity * (int32_t)dt) / 32768);
        verticalVelocity = (int16_t)(verticalVelocity + (1024L * (int32_t)dt) / 32768);
        if (originalPlayerY >= 180) {
            originalPlayerY = 180;
            verticalVelocity = 0;
            airtime = 0;
        } else if (airtime) --airtime;
    }

    for (i = 0; i < 2u; ++i)
        grounds[i].z = (int16_t)(grounds[i].z - (int16_t)spd);
    for (i = 0; i < 2u; ++i) {
        if ((int32_t)grounds[i].z + grounds[i].zlen < 200) {
            uint8_t other = (uint8_t)(1u - i);
            grounds[i].z = (int16_t)((int32_t)grounds[other].z + grounds[other].zlen);
            if (i == 1u && !slalomRoadTimer && !dropping) generate_original_pattern();
        }
    }

    if (jumpLockout) --jumpLockout;
    if (boostTimer) --boostTimer;
    if (speedNotice) --speedNotice;
    if (patternNotice) --patternNotice;
    if (warningFlash) --warningFlash;
    if (tunnelVisualDistance > spd) tunnelVisualDistance = (uint16_t)(tunnelVisualDistance - spd);
    else tunnelVisualDistance = 0;
    if (update_wall_ride(spd) == 2u) return;
    if (slalomRoadTimer > spd) slalomRoadTimer = (uint16_t)(slalomRoadTimer - spd);
    else if (slalomRoadTimer) {
        slalomRoadTimer = 0;
        generate_original_pattern();
    }
    nearMissFrame = 0;
    for (i = 0; i < MAX_TRACK_HAZARDS; ++i) {
        TrackHazard *h = &trackHazards[i];
        int16_t oldZ, newZ;
        uint8_t crossed;
        if (!h->active) continue;
        if (wallRideSide) continue;
        oldZ = (int16_t)h->depth;
        newZ = (int16_t)(oldZ - (int16_t)spd);
        h->depth = (uint16_t)(newZ < 0 ? 0 : newZ);
        crossed = (uint8_t)(oldZ > 260 && newZ <= 280);
        /* Resolve a crossed collision window before culling. A large timer
         * step can move a hazard from ahead of the player past the near cutoff. */
        if (dropping && h->type != HAZARD_GAP) {
            if (newZ < 170) h->active = 0;
            continue;
        }
        if (crossed && h->type == HAZARD_GEM) {
            uint8_t lane = (uint8_t)h->lane;
            int16_t left = (int16_t)originalX[lane][300];
            int16_t right = (int16_t)originalX[lane + 1u][300];
            if (originalPlayerX + 8 < right && originalPlayerX + 32 > left && originalPlayerY >= 165) {
                h->active = 0;
                if (save.gems < GEM_CAP) ++save.gems;
                gemsDirty = 1;
                ++runGems;
                patternNoticeText = "GEM +1";
                patternNotice = 30;
            }
        } else if (crossed && !(save.cheatsUnlocked && (save.settings & SET_INVULNERABLE)) &&
                   (realmMode != REALM_HEAVEN || h->type == HAZARD_GAP)) {
            uint8_t lane = (uint8_t)h->lane;
            int16_t left = (int16_t)originalX[lane][300];
            int16_t right = (int16_t)originalX[lane + 1u][300];
            int16_t playerLeft = (int16_t)(originalPlayerX + 8);
            int16_t playerRight = (int16_t)(originalPlayerX + 32);
            int16_t playerTop = (int16_t)(originalPlayerY + 8 - CAMERA_Y_SHIFT);
            int16_t playerBottom = (int16_t)(originalPlayerY + 32 - CAMERA_Y_SHIFT);
            uint8_t overlapsLane = (uint8_t)(playerLeft < right && playerRight > left);
            if (overlapsLane && h->type == HAZARD_GAP && originalPlayerY >= 178 && !dropping) {
                dropping = 1;
                dropFrames = 0;
                verticalVelocity = 0;
                warningFlash = 18;
            } else if (overlapsLane && h->type == HAZARD_RAMP && originalPlayerY >= 178 && !jumpLockout) {
                verticalVelocity = -400;
                airtime = 42;
                jumpLockout = 8;
                warningFlash = 10;
            } else if (overlapsLane && h->type == HAZARD_BLOCK) {
                int16_t top = originalY[h->heightLane][300];
                int16_t bottom = originalY[h->heightLane + 1u][300];
                if (playerBottom > top && playerTop < bottom) crashed = 1;
            } else if (!overlapsLane && h->type == HAZARD_BLOCK && !nearMissFrame) {
                /* Scraped past a block without touching it: a reward for risky lines. */
                int16_t top = originalY[h->heightLane][300];
                int16_t bottom = originalY[h->heightLane + 1u][300];
                int16_t gap = playerRight <= left ? (int16_t)(left - playerRight)
                                                  : (int16_t)(playerLeft - right);
                if (gap < NEAR_MISS_PX && playerBottom > top && playerTop < bottom) {
                    nearMissFrame = 1;
                    if (realmMode == REALM_HELL) {
                        hellBonus += HELL_NEAR_MISS_COINS;
                        patternNoticeText = "BLOOD +25";
                        unlock_lore(4);
                    } else {
                        score += NEAR_MISS_SCORE;
                        ++runNearMisses;
                        patternNoticeText = "NEAR MISS +5";
                    }
                    patternNotice = 25;
                }
            }
        }
        if (newZ < 170) h->active = 0;
    }

    {
        int16_t left, right;
        if (slalomRoadTimer) slalom_road_bounds(300u, &left, &right);
        else {
            left = (int16_t)originalX[0][300];
            right = (int16_t)originalX[3][300];
        }
        if (!(originalPlayerX + 32 > left && originalPlayerX + 8 < right) && originalPlayerY > 178 && !dropping) {
            if (realmMode == REALM_HELL) {
                return_from_hell();
                return;
            }
            falling = 1;
            fallElapsedTicks = 0;
            fallCameraPan = 0;
            verticalVelocity = 120;
        }
    }
    if (save.settings & SET_TRAIL) {
        if (trailCount < 5) ++trailCount;
        for (i = (uint8_t)(trailCount - 1u); i > 0; --i) trailX[i] = trailX[i - 1u];
        trailX[0] = (int16_t)(originalPlayerX + 20);
    }
    if (crashed) {
        if (realmMode == REALM_HEAVEN) start_hell_descent();
        else if (realmMode == REALM_HELL) return_from_hell();
        else begin_death();
    }
}

static void draw_hell_sidebar(void) {
    uint32_t remaining = hellElapsedTicks >= HELL_MAX_TICKS ? 0u :
                         HELL_MAX_TICKS - hellElapsedTicks;
    uint16_t barHeight = (uint16_t)((remaining * 150UL) / HELL_MAX_TICKS);
    gfx_SetColor(C_RED);
    gfx_FillRectangle(298, 0, 22, SH);
    gfx_SetColor(C_BLACK);
    gfx_Rectangle(298, 0, 22, SH);
    /* Horned beast badge. */
    gfx_FillTriangle(300, 34, 300, 17, 308, 28);
    gfx_FillTriangle(318, 34, 318, 17, 310, 28);
    gfx_FillCircle(309, 34, 9);
    gfx_SetColor(C_RED_HI);
    gfx_FillRectangle(305, 32, 2, 2);
    gfx_FillRectangle(312, 32, 2, 2);
    gfx_SetColor(C_BLACK);
    gfx_FillTriangle(307, 37, 312, 37, 309, 41);
    gfx_FillRectangle(303, 57, 12, 154);
    gfx_SetColor(C_RED_HI);
    if (barHeight) gfx_FillRectangle(305, (int16_t)(209 - barHeight), 8, barHeight);
    print_at("10X", 296, 221, C_BLACK, C_RED);
}

static void draw_game_world(void) {
    int16_t i;
    uint8_t nextRamp = 0;
    int16_t rightEdge = realmMode == REALM_HELL ? 294 : 316; /* Hell's sidebar owns x >= 298 */
    fps_update();
    drawDepthLimit = MAX_STRUCTURE_DEPTH;
    if (run_planet() == 2u && realmMode == REALM_NORMAL) {
        /* VOID: a slow fog pulse hides distant hazards (draw limit breathes with travel). */
        uint16_t p = (uint16_t)(travelled % 3000u);
        uint16_t fog = p < 1500u ? p : (uint16_t)(3000u - p);
        drawDepthLimit = (uint16_t)(MAX_STRUCTURE_DEPTH - (fog * 24u) / 25u);
    }
    gfx_FillScreen(C_BLACK);
    if (tunnelVisualDistance) draw_tunnel_tube();
    /* Use the narrow, lower slalom mesh after the gap; skip the wide floor. */
    if (slalomRoadTimer || dropping) draw_slalom_road();
    else draw_normal_road();
    draw_merged_blocks();
    for (i = MAX_TRACK_HAZARDS - 1; i >= 0; --i) {
        uint8_t slot = (uint8_t)((objectCursor + (uint8_t)i) & (MAX_TRACK_HAZARDS - 1u));
        TrackHazard *h = &trackHazards[slot];
        if (h->active && h->type != HAZARD_BLOCK && h->depth <= drawDepthLimit) {
            draw_track_hazard(h);
            if (h->type == HAZARD_RAMP && h->depth > 280u) nextRamp = 1;
        }
    }
    gfx_SetColor(C_BLACK);
    gfx_HorizLine_NoClip(0, 0, SW); gfx_HorizLine_NoClip(0, SH - 1, SW);
    gfx_VertLine_NoClip(0, 0, SH); gfx_VertLine_NoClip(SW - 1, 0, SH);
    if (falling) {
        uint32_t progress = fallElapsedTicks * 100UL / (5UL * 32768UL);
        uint8_t radius = (uint8_t)(20u - (progress * 12u) / 100u);
        int16_t fallY = (int16_t)(-10 + (progress * 205u) / 100u);
        draw_wire_sphere(160, fallY, radius);
    } else {
        int16_t playerX = (int16_t)(originalPlayerX + 20);
        int16_t playerY = (int16_t)(originalPlayerY + 20 - CAMERA_Y_SHIFT);
        if (realmMode == REALM_HEAVEN) draw_angel_wings(playerX, playerY);
        draw_player(playerX, playerY);
    }
    /* HUD: left block is label + number (label width 8 px per character, so numbers start
     * after it); right block is right-aligned so 8-digit coin totals never run off screen. */
    draw_pixel_score(score, 160, 4);
    print_at("BEST", 5, 5, C_GRID, C_BLACK);
    print_number_cached(&hudBestCache, save.bestScore, 40, 5, C_WHITE, C_BLACK);
    print_at("SPD", 5, 18, C_GRAY, C_BLACK);
    print_number_cached(&hudSpeedCache, current_speed(), 32, 18, C_NEON_HI, C_BLACK);
    print_stat_right("C", &hudCoinsCache, save.coins, rightEdge, 5, C_GOLD, C_WHITE, C_BLACK);
    print_stat_right("G", &hudGemsCache, save.gems, rightEdge, 17, C_CYAN, C_WHITE, C_BLACK);
    print_stat_right("FPS", &hudFpsCache, fpsShown, rightEdge, 31, C_GRAY,
                     fpsShown < 20u ? C_RED : C_NEON_HI, C_BLACK);
    if (realmMode == REALM_HEAVEN) {
        uint32_t seconds = realmElapsedTicks >= HEAVEN_DURATION_TICKS ? 0u :
                           (HEAVEN_DURATION_TICKS - realmElapsedTicks + 32767u) / 32768u;
        print_at("HEAVEN X2", 8, 40, C_NEON_HI, C_BLACK);
        print_number_cached(&hudHeavenTimeCache, seconds, 84, 40, C_GOLD, C_BLACK);
    } else if (realmMode == REALM_HELL) {
        print_at("HELL X10", 8, 40, C_RED_HI, C_BLACK);
        draw_hell_sidebar();
    } else if (wallRideSide) {
        print_center(wallRideSide == 1u ? "LEFT WALL RAMP" : "RIGHT WALL RAMP", 40, C_CYAN, C_BLACK);
    }
    if (dailyMode) print_at("DAILY", 8, 52, C_GOLD, C_BLACK);
    if (speedNotice) print_center("SPEED UP!", 26, C_PAD, C_BLACK);
    else if (patternNotice) print_center(patternNoticeText, 26, C_NEON_HI, C_BLACK);
    else if (nextRamp) print_center("RAMP AHEAD", 26, C_CYAN, C_BLACK);
    if (falling) print_center("FALLING", 55, C_RED, C_BLACK);
    else if (dropping) print_center("DROPPING", 55, C_PAD, C_BLACK);
    else if (slalomRoadTimer) print_center("SLALOM ROAD", 55, C_CYAN, C_BLACK);
    else if (originalPlayerY < 178 && !wallRideSide) print_center("AIRBORNE", 55, C_NEON_HI, C_BLACK);
    else if (warningFlash) print_center("RAMP!", 55, C_CYAN, C_BLACK);
    if (save.cheatsUnlocked && (save.settings & SET_INVULNERABLE))
        print_at("SHIELD", (int16_t)(rightEdge - 48), 43, C_NEON_HI, C_BLACK);
}


static void draw_title_ball(int16_t x, int16_t y) {
    draw_wire_sphere(x, y, 10);
}

static void draw_menu_background(void) {
    uint8_t i;
    gfx_FillScreen(C_BLACK);
    draw_stars(18);
    /* Miniature road and city grid, deliberately kept behind the menu text. */
    for (i = 0; i < 6; ++i) {
        int16_t y = (int16_t)(112 + i * 23);
        gfx_SetColor(i == 5 ? C_GRID : C_GRID_DIM);
        fast_line((int16_t)(160 - (i * 25)), y, (int16_t)(160 + (i * 25)), y);
    }
    gfx_SetColor(C_GRID);
    fast_line(0, 239, 160, 86);
    fast_line(319, 239, 160, 86);
    for (i = 0; i < 7; ++i) {
        int16_t x = (int16_t)(12 + i * 18);
        gfx_SetColor(C_GRID_DIM);
        fast_line(x, 239, (int16_t)(160 + (x - 160) / 4), 86);
        fast_line((int16_t)(319 - x), 239, (int16_t)(160 - (x - 160) / 4), 86);
    }
}

static void draw_side_tab(int16_t y, const char *text, uint8_t fg) {
    gfx_SetColor(C_GRID);
    gfx_Rectangle(232, y, 82, 18);
    print_at(text, 237, (int16_t)(y + 5), fg, C_BLACK);
}

static void draw_title(void) {
    static const char *items[6] = { "PLAY", "GARAGE", "WORLDS", "SHOP", "SETTINGS", "QUIT" };
    uint8_t i;
    draw_menu_background();
    set_scale(3);
    print_center("SLOPE", 12, C_NEON, C_BLACK);
    set_scale(1);
    print_center("CE EDITION", 43, C_GRAY, C_BLACK);
    draw_title_ball(265, 47);
    for (i = 0; i < 6; ++i) {
        int16_t y = (int16_t)(69 + i * 23);
        if (menuIndex == (int8_t)i) {
            gfx_SetColor(C_NEON);
            gfx_FillRectangle(94, y - 3, 132, 19);
            gfx_SetColor(C_BLACK);
            gfx_Rectangle(94, y - 3, 132, 19);
            print_at(items[i], 119, y + 2, C_BLACK, C_NEON);
            gfx_SetColor(C_NEON_HI);
            gfx_FillTriangle(82, (int16_t)(y + 6), 88, (int16_t)(y + 2), 88, (int16_t)(y + 10));
        } else {
            print_at(items[i], 119, y + 2, C_WHITE, C_BLACK);
        }
    }
    draw_side_tab(84, "F2 DAILY", C_GOLD);
    draw_side_tab(108, "F3 BESTS", C_NEON_HI);
    draw_side_tab(132, loreNew ? "F4 CODEX*" : "F4 CODEX", loreNew ? C_GOLD : C_NEON_HI);
    print_at("ARROWS: MENU / STEER", 8, 209, C_GRAY, C_BLACK);
    print_at("2ND/ENTER SELECT  1-6 QUICK", 8, 222, C_GRAY, C_BLACK);
    print_stat_right("BEST", &hudBestCache, save.bestScore, 316, 209, C_GRID, C_NEON_HI, C_BLACK);
    print_stat_right("C", &hudCoinsCache, save.coins, 316, 222, C_GOLD, C_WHITE, C_BLACK);
    if (save.cheatsUnlocked) print_at("CHEATS READY", 208, 58, C_NEON_HI, C_BLACK);
    else if (cheatNotice) print_at("CHEATS UNLOCKED!", 188, 58, C_NEON_HI, C_BLACK);
    if (cheatNotice) --cheatNotice;
}

static void draw_death(void) {
    static const char *items[3] = { "PLAY AGAIN", "WORLDS", "BACK" };
    uint8_t i;
    uint32_t shownScore = deathPhase == 0 ? deathCounter : runScore;
    char digits[11];
    uint8_t length;
    gfx_FillScreen(C_BLACK);
    set_scale(3);
    print_center("GAME OVER", 8, C_NEON, C_BLACK);
    set_scale(2);
    print_center("YOUR SCORE:", 44, C_NEON, C_BLACK);
    set_scale(3);
    length = format_u32(shownScore, digits);
    print_at(digits, (int16_t)((SW - (int16_t)length * 24) / 2), 81, C_NEON, C_BLACK);
    set_scale(1);
    if (deathPhase < 2) {
        print_center("SAVING SCORE...", 112, C_GRID, C_BLACK);
        return;
    }
    if (loreNew) print_center("NEW CODEX LOG!  (F4 ON TITLE)", 108, C_GOLD, C_BLACK);
    for (i = 0; i < 3u; ++i) {
        int16_t x = 54, y = (int16_t)(123 + i * 37), w = 212, h = 30, cut = 8;
        gfx_SetColor(i == (uint8_t)deathMenuIndex ? C_NEON : C_GRID_DIM);
        fast_line(x + cut, y, x + w - cut, y);
        fast_line(x + w - cut, y, x + w, y + cut);
        fast_line(x + w, y + cut, x + w, y + h - cut);
        fast_line(x + w, y + h - cut, x + w - cut, y + h);
        fast_line(x + w - cut, y + h, x + cut, y + h);
        fast_line(x + cut, y + h, x, y + h - cut);
        fast_line(x, y + h - cut, x, y + cut);
        fast_line(x, y + cut, x + cut, y);
        if (i == (uint8_t)deathMenuIndex) {
            gfx_SetColor(C_NEON);
            gfx_FillTriangle(x + w - 22, y + 5, x + w - 8, y + 5, x + w - 15, y + 13);
        }
        set_scale(2);
        print_center(items[i], (int16_t)(y + 7), i == (uint8_t)deathMenuIndex ? C_NEON : C_GRID_DIM, C_BLACK);
    }
    set_scale(1);
}

static void update_death(void) {
    if (deathPhase == 0) {
        uint32_t diff = runScore - deathCounter;
        uint32_t step = diff / 9u + 1u;
        if (deathCounter + step >= runScore) {
            deathCounter = runScore;
            deathPhase = 1;
        } else deathCounter += step;
    } else if (deathPhase == 1) {
        uint32_t diff = coinPayout - coinCounter;
        uint32_t step = diff / 10u + 1u;
        if (coinCounter + step >= coinPayout) {
            coinCounter = coinPayout;
            deathPhase = 2;
            if (!payoutApplied) {
                if (save.coins < COIN_CAP - coinPayout) save.coins += coinPayout;
                else save.coins = COIN_CAP;
                save_game();
                payoutApplied = 1;
            }
        } else coinCounter += step;
    }
}

static void update_event(const InputFrame *keys) {
    uint32_t now = timer_Get(1);
    uint32_t dt = now - originalTicks;
    originalTicks = now;
    if (dt > 8192u) dt = 8192u;
    if (!dt) dt = 1u;
    if (eventType == EVENT_RETURN) {
        if (eventFrame < 10u) ++eventFrame;
        if (eventFrame >= 10u) {
            /* Back to the regular green slope on a fresh run, never mid-Hell geometry. */
            eventType = EVENT_NONE;
            reset_run();
            set_palette();
            screen = SCREEN_GAME;
        }
        return;
    }
    if (eventType == EVENT_WINGS) {
        eventElapsedTicks += dt;
        if (eventStep == 0u) {
            if (eventElapsedTicks >= WINGS_CRACK_TICKS) {
                eventStep = 1;
                eventElapsedTicks = 0;
                realmMode = REALM_NORMAL;
                set_palette();          /* the gold drains out and the green world returns */
            }
        } else if (eventElapsedTicks >= WINGS_FALL_TICKS) {
            eventType = EVENT_NONE;
            reset_run();
            set_palette();
            screen = SCREEN_GAME;
        }
        return;
    }
    if (eventType == EVENT_HELL_DESCENT && eventStep == 0u) {
        if (keys->select) {
            eventStep = 1;
            eventElapsedTicks = 0;
            originalTicks = now;
        }
        return;
    }
    eventElapsedTicks += dt;
    if (eventType == EVENT_ASCENSION) {
        static const uint32_t duration[4] = {
            4UL * 32768UL, 2UL * 32768UL, 2UL * 32768UL, 2UL * 32768UL
        };
        if (eventElapsedTicks >= duration[eventStep]) {
            eventElapsedTicks = 0;
            if (eventStep == 0u) lastPhasePayout = bank_phase_score();
            if (++eventStep >= 4u) enter_heaven();
        }
    } else if (eventType == EVENT_HELL_DESCENT) {
        if (eventElapsedTicks >= 32768UL) {
            eventElapsedTicks = 0;
            if (++eventStep >= 7u) enter_hell();
        }
    }
}

static void draw_hell_fall_effect(void) {
    uint8_t i;
    uint16_t phase = (uint16_t)((eventElapsedTicks >> 9) + (uint16_t)(eventStep * 17u));
    int16_t vanishingY = (int16_t)(88 + eventStep * 7u);
    gfx_FillScreen(C_RED_DARK);
    gfx_SetColor(C_BLACK);
    for (i = 0; i < 9u; ++i) {
        int16_t bottomX = (int16_t)(-15 + i * 44);
        int16_t topX = (int16_t)(160 + (bottomX - 160) / 5);
        fast_line(bottomX, 239, topX, vanishingY);
    }
    gfx_SetColor(C_RED);
    for (i = 0; i < 12u; ++i) {
        int16_t x = (int16_t)((i * 31u + phase * 5u) % SW);
        int16_t y = (int16_t)((i * 43u + phase * 11u) % SH);
        int16_t length = (int16_t)(9 + ((i + eventStep) % 4u) * 5u);
        fast_line(x, y, (int16_t)(x - 7), (int16_t)(y - length));
    }
    gfx_SetColor(C_RED_HI);
    fast_line(0, 0, SW - 1, SH - 1);
    fast_line(SW - 1, 0, 0, SH - 1);
    draw_wire_sphere(160, (int16_t)(112 + eventStep * 8u), (uint8_t)(5u + eventStep * 2u));
}

static void draw_event(void) {
    if (eventType == EVENT_ASCENSION) {
        if (eventStep == 0u) {
            draw_game_world();
        } else if (eventStep == 1u) {
            gfx_FillScreen(C_CREAM);
        } else if (eventStep == 2u) {
            gfx_FillScreen(C_CREAM);
            set_scale(2);
            print_center("-------------", 53, C_NEON, C_CREAM);
            print_center("ASCENSION", 88, C_NEON, C_CREAM);
            print_center("-------------", 123, C_NEON, C_CREAM);
            set_scale(1);
        } else {
            uint32_t progress = eventElapsedTicks * 160UL / (2UL * 32768UL);
            int16_t y = (int16_t)(210 - progress);
            gfx_FillScreen(C_CREAM);
            draw_angel_wings(160, y);
            draw_wire_sphere(160, y, 18);
            print_center("ASCENDING", 25, C_NEON, C_CREAM);
        }
        return;
    }
    if (eventType == EVENT_HELL_DESCENT) {
        static const char *fallWords[5] = {
            "ddddddddddddddddddddddd",
            "rrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrr",
            "oooooooooooooooooooooo",
            "pppppppppppppppppppppp",
            "FALL"
        };
        if (eventStep == 0u) {
            static const char *message[6] = {
                "You have fallen.",
                "You are now in HELL.",
                "In HELL, BLOOD IS FUEL.",
                "You must survive five seconds",
                "for grace.",
                "Try. Persevere. Prevail."
            };
            uint8_t i;
            gfx_FillScreen(C_RED_DARK);
            gfx_SetColor(C_RED);
            gfx_Rectangle(8, 24, 304, 190);
            for (i = 0; i < 6u; ++i) {
                int16_t x = (int16_t)((SW - (int16_t)(strlen(message[i]) * 8u)) / 2);
                int16_t y = (int16_t)(54 + i * 22u + (i > 3u ? 8u : 0u));
                print_at(message[i], x, y, C_WHITE, C_RED_DARK);
            }
            if ((uiFrame & 16u) == 0u)
                print_center("<Press 2nd/enter to fall further>", 193, C_RED_HI, C_RED_DARK);
        } else {
            if (eventStep < 6u) {
                const char *word = fallWords[eventStep - 1u];
                int16_t x = (int16_t)((SW - (int16_t)(strlen(word) * 8u)) / 2);
                draw_hell_fall_effect();
                print_at(word, x, 110, C_RED_HI, C_RED_DARK);
                print_center("FALLING INTO HELL", 210, C_RED_HI, C_RED_DARK);
            } else {
                gfx_FillScreen(C_WHITE);
            }
        }
        return;
    }
    if (eventType == EVENT_WINGS) {
        if (eventStep == 0u) {
            int16_t shake = (int16_t)((uiFrame & 1u) ? 2 : -2);
            gfx_FillScreen(C_CREAM);
            draw_angel_wings((int16_t)(160 + shake), 110);
            draw_wire_sphere(160, 110, 18);
            gfx_SetColor(C_GOLD_DK);   /* cracks racing through the feathers */
            fast_line(124, 104, 130, 112); fast_line(130, 112, 126, 118);
            fast_line(196, 104, 190, 112); fast_line(190, 112, 194, 118);
            print_center("THE WINGS CANNOT HOLD", 40, C_GOLD_DK, C_CREAM);
        } else {
            int16_t c = (int16_t)(eventElapsedTicks * 100UL / 32768UL);
            int16_t ballY, wingY;
            if (c > 200) c = 200;
            ballY = (int16_t)(110 + (c * c) / 70);
            wingY = (int16_t)(110 + (c * c) / 45);
            draw_menu_background();
            gfx_SetColor(C_GOLD);
            draw_wing((int16_t)(160 - c / 3), wingY, -1);
            draw_wing((int16_t)(160 + c / 3), (int16_t)(wingY + c / 4), 1);
            if (ballY < 250) draw_wire_sphere(160, ballY, 18);
            print_center("THE WINGS BREAK", 30, C_GOLD, C_BLACK);
            if (c > 100) print_center("BACK TO THE SLOPE", 110, C_NEON_HI, C_BLACK);
        }
        return;
    }
    if (eventType == EVENT_RETURN) {
        uint8_t i;
        int16_t horizon = (int16_t)(48 + eventFrame * 10u);
        gfx_FillScreen(C_LIME);
        gfx_SetColor(C_BLACK);
        for (i = 0; i < 7u; ++i) {
            int16_t bottomX = (int16_t)(i * 53u);
            int16_t topX = (int16_t)(160 + ((bottomX - 160) * (eventFrame + 1u)) / 10);
            fast_line(topX, horizon, bottomX, 226);
        }
        for (i = 1; i <= 7u; ++i) {
            int16_t y = (int16_t)(horizon + ((226 - horizon) * i) / 8);
            fast_line(0, y, 319, y);
        }
        draw_wire_sphere(160, (int16_t)(20 + eventFrame * 14u), 16);
        if (eventFrame >= 3u) {
            char line[24];
            uint8_t n;
            print_center(hellSurvived ? "GRACE" : "ESCAPED", 90, C_BLACK, C_LIME);
            line[0] = '+';
            n = format_u32(lastPhasePayout, line + 1);
            memcpy(line + 1 + n, " COINS", 7);
            print_center(line, 108, C_BLACK, C_LIME);
        }
    }
}

static const char *shape_name(uint8_t shape) {
    if (shape == SHAPE_CUBE) return "WIRE CUBE";
    if (shape == SHAPE_PYRAMID) return "PYRAMID";
    return "WIRE SPHERE";
}
static uint32_t shape_cost(uint8_t shape) {
    if (shape == SHAPE_CUBE) return 250u;
    if (shape == SHAPE_PYRAMID) return 500u;
    return 0u;
}
static uint8_t shape_owned(uint8_t shape) {
    return (uint8_t)((save.ownedShapes & (uint8_t)(1u << shape)) != 0);
}

static void draw_garage(void) {
    gfx_FillScreen(C_BLACK);
    draw_stars(15);
    set_scale(2);
    print_center("GARAGE", 15, C_WHITE, C_BLACK);
    set_scale(1);
    print_at("UP/DOWN: CATEGORY", 80, 46, C_GRAY, C_BLACK);
    if (garageTab == 0) {
        print_at("SHAPE", 135, 64, C_NEON, C_BLACK);
        if (garageShapeIndex == SHAPE_SPHERE) draw_wire_sphere(160, 116, 25);
        else if (garageShapeIndex == SHAPE_CUBE) draw_wire_cube(151, 115, 19);
        else draw_wire_pyramid(160, 116, 25);
        print_at(shape_name(garageShapeIndex), 102, 151, C_NEON_HI, C_BLACK);
        if (shape_owned(garageShapeIndex)) {
            print_at(garageShapeIndex == save.activeShape ? "EQUIPPED - SELECT AGAIN" : "OWNED - SELECT TO EQUIP", 69, 174, C_WHITE, C_BLACK);
        } else {
            print_at("PRICE", 118, 173, C_GRAY, C_BLACK);
            print_number(shape_cost(garageShapeIndex), 164, 173, C_GOLD, C_BLACK);
            print_at("2ND / ENTER: BUY", 95, 189, C_NEON, C_BLACK);
        }
        print_at("LEFT/RIGHT: CHANGE SHAPE", 70, 210, C_GRAY, C_BLACK);
    } else {
        print_at("COLOUR", 134, 64, C_NEON, C_BLACK);
        draw_wire_sphere(160, 119, 25);
        {
            static const char *colourNames[4] = { "NEON GREEN", "CYAN", "HOT PINK", "AMBER" };
            print_at(colourNames[save.colorTheme], 112, 162, C_NEON_HI, C_BLACK);
        }
        print_at("LEFT/RIGHT: CHANGE COLOUR", 68, 188, C_GRAY, C_BLACK);
    }
    print_at("C", 8, 8, C_GOLD, C_BLACK); print_number(save.coins, 19, 8, C_WHITE, C_BLACK);
    print_at("G", 258, 8, C_CYAN, C_BLACK); print_number(save.gems, 270, 8, C_WHITE, C_BLACK);
    print_at("SELECT: BUY / CLEAR: BACK", 74, 226, C_GRAY, C_BLACK);
}

static uint32_t build_cost(uint8_t planet, uint8_t built) {
    (void)planet;
    if (built == 0) return 100u;
    if (built == 1) return 150u;
    return 300u;
}

static void world_action_execute(void) {
    uint8_t bit;
    if (worldPlanet < 0 || worldPlanet > 2) return;
    bit = (uint8_t)(1u << (uint8_t)worldPlanet);
    if (!(save.unlockedPlanets & bit)) return;
    save.activePlanet = (uint8_t)worldPlanet;
    set_palette();
    if (worldAction == 0) {
        uint8_t built = save.tracksBuilt[worldPlanet];
        uint32_t cost;
        if (built >= 3u) return;
        cost = build_cost((uint8_t)worldPlanet, built);
        if (save.coins >= cost) {
            save.coins -= cost;
            ++save.tracksBuilt[worldPlanet];
            save_game();
        }
    } else {
        uint8_t next = (uint8_t)(worldPlanet + 1);
        if (save.tracksBuilt[worldPlanet] >= 3u && next < 3u && !(save.unlockedPlanets & (uint8_t)(1u << next)) && save.gems >= GEM_COST_ROCKET) {
            save.gems = (uint16_t)(save.gems - GEM_COST_ROCKET);
            save.unlockedPlanets |= (uint8_t)(1u << next);
            save.tracksBuilt[next] = 1;
            save.activePlanet = next;
            worldPlanet = (int8_t)next;
            save_game();
            set_palette();
        }
    }
}

static void draw_planet_icon(uint8_t world, int16_t x, int16_t y,
                             uint8_t unlocked, uint8_t selected) {
    uint8_t color;
    if (world == 0u) color = C_NEON;
    else if (world == 1u) color = C_CYAN;
    else if (world == 2u) color = C_PURPLE;
    else color = C_GOLD;

    if (selected) {
        gfx_SetColor(C_WHITE);
        gfx_Circle(x, y, 29);
        gfx_SetColor(C_NEON);
        gfx_Circle(x, y, 31);
    }
    gfx_SetColor(world == 2u || world == 3u ? C_BG_DARK : C_GREEN_DARK);
    gfx_FillCircle(x, y, 24);
    gfx_SetColor(color);
    gfx_Circle(x, y, 24);

    if (world == 0u) {
        gfx_SetColor(C_GRID);
        fast_line(x - 19, y - 7, x + 19, y - 7);
        fast_line(x - 22, y + 2, x + 22, y + 2);
        fast_line(x - 17, y + 11, x + 17, y + 11);
    } else if (world == 1u) {
        gfx_SetColor(C_WHITE);
        gfx_Circle(x - 5, y - 3, 13);
        gfx_SetColor(C_GRID);
        gfx_FillCircle(x + 9, y + 7, 4);
        gfx_FillCircle(x - 10, y + 11, 2);
    } else if (world == 2u) {
        gfx_SetColor(C_PURPLE);
        gfx_Circle(x, y, 16);
        gfx_Circle(x, y, 9);
        gfx_SetColor(C_NEON_HI);
        gfx_FillCircle(x + 11, y - 9, 3);
    } else {
        /* Heaven's icon combines clouds with a small golden gate. */
        gfx_SetColor(C_CREAM);
        gfx_FillCircle(x - 11, y + 9, 8);
        gfx_FillCircle(x - 2, y + 5, 10);
        gfx_FillCircle(x + 9, y + 10, 8);
        gfx_SetColor(C_GOLD);
        fast_line(x - 8, y + 2, x - 8, y - 13);
        fast_line(x + 8, y + 2, x + 8, y - 13);
        fast_line(x - 8, y - 13, x + 8, y - 13);
        fast_line(x, y - 13, x, y + 2);
        fast_line(x - 5, y - 9, x - 5, y + 2);
        fast_line(x + 5, y - 9, x + 5, y + 2);
    }

    if (!unlocked) {
        int8_t i;
        gfx_SetColor(C_GRAY);
        for (i = -15; i <= 15; i += 10) {
            fast_line((int16_t)(x + i - 2), (int16_t)(y + i - 2),
                      (int16_t)(x + i + 4), (int16_t)(y + i + 4));
            fast_line((int16_t)(x + i - 2), (int16_t)(y - i + 2),
                      (int16_t)(x + i + 4), (int16_t)(y - i - 4));
        }
        gfx_SetColor(C_RED);
        fast_line(x - 14, y - 14, x + 14, y + 14);
        fast_line(x - 14, y + 14, x + 14, y - 14);
    }
}

static void draw_cloud(int16_t x, int16_t y, int16_t r, uint8_t color) {
    gfx_SetColor(color);
    gfx_FillCircle(x, y, (uint24_t)r);
    gfx_FillCircle((int16_t)(x - r), (int16_t)(y + r / 3), (uint24_t)(r * 3 / 4));
    gfx_FillCircle((int16_t)(x + r), (int16_t)(y + r / 3), (uint24_t)(r * 3 / 4));
    gfx_FillRectangle((int16_t)(x - r), (int16_t)(y + r / 3), (int16_t)(r * 2), (int16_t)(r * 2 / 3));
}

/* A six-winged seraph: two wings raised, two folded over the face, two over the feet. */
static void draw_seraph(int16_t x, int16_t y) {
    gfx_SetColor(C_WHITE);
    gfx_FillTriangle((int16_t)(x - 6), (int16_t)(y - 56), (int16_t)(x - 46), (int16_t)(y - 88), (int16_t)(x - 26), (int16_t)(y - 30));
    gfx_FillTriangle((int16_t)(x + 6), (int16_t)(y - 56), (int16_t)(x + 46), (int16_t)(y - 88), (int16_t)(x + 26), (int16_t)(y - 30));
    gfx_FillTriangle(x, (int16_t)(y - 66), (int16_t)(x - 18), y, (int16_t)(x + 18), y);
    gfx_SetColor(C_GOLD);
    fast_line((int16_t)(x - 6), (int16_t)(y - 56), (int16_t)(x - 46), (int16_t)(y - 88));
    fast_line((int16_t)(x - 46), (int16_t)(y - 88), (int16_t)(x - 26), (int16_t)(y - 30));
    fast_line((int16_t)(x + 6), (int16_t)(y - 56), (int16_t)(x + 46), (int16_t)(y - 88));
    fast_line((int16_t)(x + 46), (int16_t)(y - 88), (int16_t)(x + 26), (int16_t)(y - 30));
    fast_line((int16_t)(x - 14), (int16_t)(y - 56), (int16_t)(x - 38), (int16_t)(y - 64));
    fast_line((int16_t)(x + 14), (int16_t)(y - 56), (int16_t)(x + 38), (int16_t)(y - 64));
    gfx_SetColor(C_MARBLE_DK);
    fast_line(x, (int16_t)(y - 66), (int16_t)(x - 18), y);
    fast_line(x, (int16_t)(y - 66), (int16_t)(x + 18), y);
    gfx_SetColor(C_CREAM);
    gfx_FillCircle(x, (int16_t)(y - 72), 7);
    gfx_FillTriangle((int16_t)(x - 1), (int16_t)(y - 80), (int16_t)(x - 15), (int16_t)(y - 50), (int16_t)(x - 1), (int16_t)(y - 46));
    gfx_FillTriangle((int16_t)(x + 1), (int16_t)(y - 80), (int16_t)(x + 15), (int16_t)(y - 50), (int16_t)(x + 1), (int16_t)(y - 46));
    gfx_FillTriangle((int16_t)(x - 1), (int16_t)(y - 26), (int16_t)(x - 17), (int16_t)(y + 2), (int16_t)(x - 1), (int16_t)(y + 4));
    gfx_FillTriangle((int16_t)(x + 1), (int16_t)(y - 26), (int16_t)(x + 17), (int16_t)(y + 2), (int16_t)(x + 1), (int16_t)(y + 4));
    gfx_SetColor(C_GOLD_DK);
    gfx_Circle(x, (int16_t)(y - 72), 7);
    gfx_SetColor(C_GOLD);
    gfx_Circle(x, (int16_t)(y - 86), 10);
    gfx_Circle(x, (int16_t)(y - 86), 9);
}

/* Full-screen HEAVEN: a golden gate in a halo of light, marble stairs rising out of the
 * clouds, and a seraph keeping watch on the left. Only clipped primitives are used so the
 * worlds screen can slide over it. */
static void draw_heaven_scene(void) {
    uint8_t i;
    int16_t x;
    gfx_SetColor(C_SKY_TOP);  gfx_FillRectangle(0, 0, SW, 64);
    gfx_SetColor(C_SKY_MID);  gfx_FillRectangle(0, 64, SW, 64);
    gfx_SetColor(C_SKY_LOW);  gfx_FillRectangle(0, 128, SW, 64);
    gfx_SetColor(C_CREAM);    gfx_FillRectangle(0, 192, SW, 48);
    gfx_SetColor(C_SKY_LOW);
    for (i = 0; i < 9u; ++i) fast_line(160, 112, (int16_t)(i * 40), 0);
    gfx_SetColor(C_CREAM); gfx_FillCircle(160, 112, 62);
    gfx_SetColor(C_WHITE); gfx_FillCircle(160, 112, 46);
    /* gate: pediment, lintel, pillars, doors ajar */
    gfx_SetColor(C_GOLD);   gfx_FillTriangle(92, 52, 228, 52, 160, 28);
    gfx_SetColor(C_GOLD_DK); fast_line(92, 52, 160, 28); fast_line(160, 28, 228, 52);
    gfx_SetColor(C_WHITE);  gfx_FillCircle(160, 43, 5);
    gfx_SetColor(C_GOLD);   gfx_FillRectangle(96, 52, 128, 8);
    gfx_SetColor(C_GOLD_DK); gfx_Rectangle(96, 52, 128, 8);
    gfx_SetColor(C_CREAM);  gfx_FillRectangle(120, 60, 80, 110);
    gfx_SetColor(C_WHITE);  gfx_FillRectangle(152, 60, 16, 110);
    gfx_SetColor(C_GOLD);
    for (x = 124; x <= 148; x = (int16_t)(x + 8)) fast_line(x, 62, x, 168);
    for (x = 172; x <= 196; x = (int16_t)(x + 8)) fast_line(x, 62, x, 168);
    fast_line(120, 92, 152, 92);  fast_line(120, 128, 152, 128);
    fast_line(168, 92, 200, 92);  fast_line(168, 128, 200, 128);
    gfx_FillRectangle(102, 60, 18, 110); gfx_FillRectangle(200, 60, 18, 110);
    gfx_SetColor(C_GOLD_DK);
    gfx_Rectangle(102, 60, 18, 110); gfx_Rectangle(200, 60, 18, 110);
    fast_line(108, 64, 108, 166); fast_line(114, 64, 114, 166);
    fast_line(206, 64, 206, 166); fast_line(212, 64, 212, 166);
    gfx_FillRectangle(98, 60, 26, 4); gfx_FillRectangle(196, 60, 26, 4);
    gfx_FillRectangle(98, 166, 26, 5); gfx_FillRectangle(196, 166, 26, 5);
    /* marble stairs */
    for (i = 0; i < 6u; ++i) {
        int16_t half = (int16_t)(46 + i * 14);
        int16_t y = (int16_t)(170 + i * 8);
        gfx_SetColor(C_MARBLE);    gfx_FillRectangle((int16_t)(160 - half), y, (int16_t)(half * 2), 5);
        gfx_SetColor(C_MARBLE_DK); gfx_FillRectangle((int16_t)(160 - half), (int16_t)(y + 5), (int16_t)(half * 2), 3);
        gfx_SetColor(C_GOLD);      fast_line((int16_t)(160 - half), y, (int16_t)(159 + half), y);
    }
    /* clouds, then the seraph standing on one */
    draw_cloud(276, 146, 13, C_CREAM);
    draw_cloud(30, 70, 10, C_SKY_LOW);
    draw_cloud(52, 184, 20, C_WHITE);
    draw_seraph(52, 176);
    gfx_SetColor(C_WHITE); gfx_FillRectangle(0, 216, SW, 24);
    draw_cloud(24, 216, 18, C_WHITE);  draw_cloud(80, 224, 20, C_CREAM);
    draw_cloud(150, 228, 24, C_WHITE); draw_cloud(222, 224, 20, C_CREAM);
    draw_cloud(292, 216, 18, C_WHITE);
    gfx_SetColor(C_WHITE); gfx_FillRectangle(0, 220, SW, 20);
    set_scale(3);
    print_center("HEAVEN", 2, C_GOLD_DK, C_SKY_TOP);
    set_scale(1);
    if (heavenSlide >= SH) print_center("2ND/ENTER: ASCEND    CLEAR: BACK", 226, C_GOLD_DK, C_WHITE);
}

#define WY(v) ((int16_t)((v) + dy))
static void draw_worlds(void) {
    uint8_t i;
    int16_t dy = heavenSlide;
    static const char *names[3] = { "NEON", "CYAN", "VOID" };
    static const int16_t planetX[3] = { 53, 160, 267 };
    if (dy > 0) draw_heaven_scene();
    if (dy >= SH) return;
    if (dy == 0) gfx_FillScreen(C_BLACK);
    else { gfx_SetColor(C_BLACK); gfx_FillRectangle(0, dy, SW, (int16_t)(SH - dy)); }
    starOffsetY = dy;
    draw_stars(12);
    starOffsetY = 0;
    set_scale(2);
    print_center("WORLDS", WY(13), C_WHITE, C_BLACK);
    set_scale(1);
    print_at("C", 9, WY(8), C_GOLD, C_BLACK); print_number(save.coins, 21, WY(8), C_WHITE, C_BLACK);
    print_at("G", 258, WY(8), C_CYAN, C_BLACK); print_number(save.gems, 270, WY(8), C_WHITE, C_BLACK);
    for (i = 0; i < 3u; ++i) {
        uint8_t unlocked = (uint8_t)((save.unlockedPlanets & (uint8_t)(1u << i)) != 0);
        uint8_t labelLen = (uint8_t)strlen(names[i]);
        int16_t labelX = (int16_t)(planetX[i] - (int16_t)(labelLen * 4u));
        draw_planet_icon(i, planetX[i], WY(75), unlocked, (uint8_t)(worldPlanet == (int8_t)i));
        print_at(names[i], labelX, WY(105),
                 unlocked ? (i == save.activePlanet ? C_NEON_HI : C_WHITE) : C_GRAY,
                 C_BLACK);
        print_number(save.tracksBuilt[i], (int16_t)(planetX[i] - 7), WY(118),
                     unlocked ? C_NEON : C_GRAY, C_BLACK);
        print_at("/3", (int16_t)(planetX[i] + 1), WY(118), C_GRAY, C_BLACK);
    }
    print_at("LEFT/RIGHT: PLANET  UP/DOWN: ACTION", 17, WY(145), C_GRAY, C_BLACK);
    print_at(worldAction == 0 ? "> BUILD TRACK" : "  BUILD TRACK", 37, WY(165),
             worldAction == 0 ? C_NEON_HI : C_GRAY, C_BLACK);
    print_at(worldAction == 1 ? "> LAUNCH ROCKET" : "  LAUNCH ROCKET", 37, WY(183),
             worldAction == 1 ? C_NEON_HI : C_GRAY, C_BLACK);
    if ((save.unlockedPlanets & (uint8_t)(1u << worldPlanet)) &&
        worldAction == 0 && save.tracksBuilt[worldPlanet] < 3u) {
        print_at("COST", 231, WY(165), C_GRAY, C_BLACK);
        print_number(build_cost((uint8_t)worldPlanet, save.tracksBuilt[worldPlanet]),
                     266, WY(165), C_GOLD, C_BLACK);
    } else if (worldAction == 1) {
        print_at("3 GEMS", 246, WY(183), C_CYAN, C_BLACK);
    }
    if (save.tracksBuilt[worldPlanet] >= 3u && worldAction == 1 && worldPlanet < 2)
        print_center("NEXT WORLD READY", WY(202), C_NEON, C_BLACK);
    print_center("2ND/ENTER: ACTION   CLEAR: BACK", WY(226), C_GRAY, C_BLACK);
}
#undef WY

static void enter_worlds(void) {
    worldPlanet = (int8_t)save.activePlanet;
    worldAction = 0;
    heavenSlide = 0;
    heavenDir = 0;
    heavenView = 0;
    heavenPresses = 0;
    screen = SCREEN_WORLDS;
}

static void shop_execute(void) {
    if (shopIndex == 0 && save.coins >= 250u && save.gems < 65530u) {
        save.coins -= 250u; ++save.gems;
    } else if (shopIndex == 1 && save.coins >= 1000u && save.gems <= 65525u) {
        save.coins -= 1000u; save.gems = (uint16_t)(save.gems + 5u);
    } else if (shopIndex == 2 && !(save.upgrades & 0x01u) && save.gems >= 10u) {
        save.gems = (uint16_t)(save.gems - 10u); save.upgrades |= 0x01u;
    }
    save_game();
}

static void draw_shop(void) {
    static const char *items[4] = { "250 COINS -> 1 GEM", "1000 COINS -> 5 GEMS", "2X COIN EARNINGS (10G)", "BACK" };
    uint8_t i;
    gfx_FillScreen(C_BLACK);
    draw_stars(12);
    set_scale(2);
    print_center("SHOP", 15, C_WHITE, C_BLACK);
    set_scale(1);
    for (i = 0; i < 4; ++i) {
        int16_t y = (int16_t)(77 + i * 30);
        uint8_t fg = (shopIndex == (int8_t)i) ? C_NEON : C_WHITE;
        if (shopIndex == (int8_t)i) {
            gfx_SetColor(C_GRID_DIM); gfx_FillRectangle(13, y - 4, 294, 22);
            gfx_SetColor(C_NEON); gfx_Rectangle(13, y - 4, 294, 22);
        }
        print_at(items[i], 22, y + 2, fg, C_BLACK);
        if (i == 2 && (save.upgrades & 0x01u)) print_at("OWNED", 251, y + 2, C_NEON_HI, C_BLACK);
    }
    print_at("COINS", 14, 40, C_GOLD, C_BLACK); print_number(save.coins, 58, 40, C_WHITE, C_BLACK);
    print_at("GEMS", 220, 40, C_CYAN, C_BLACK); print_number(save.gems, 258, 40, C_WHITE, C_BLACK);
    print_at("UP/DOWN SELECT  2ND BUY", 60, 218, C_GRAY, C_BLACK);
}

static void toggle_setting(int8_t item) {
    uint8_t bit = 0;
    if (item == 0) bit = SET_TRAIL;
    else if (item == 1) bit = SET_INVULNERABLE;
    else if (item == 2) bit = SET_SPEED_CHEAT;
    else if (item == 3) bit = SET_DOUBLE_COIN;
    else if (item == 4) bit = SET_SLOW_CHEAT;
    if (item == 5) { screen = SCREEN_TITLE; return; }
    if ((item == 1 || item == 2 || item == 3 || item == 4) &&
        (!save.cheatsUnlocked || cheatRevealStage < CHEAT_COUNT)) return;
    save.settings ^= bit;
    save_game();
}

static void update_cheat_reveal(void) {
    if (cheatShakeTicks) --cheatShakeTicks;
    if (cheatRevealStage >= CHEAT_COUNT) return;
    ++cheatRevealFrame;
    if (cheatRevealFrame == CHEAT_SLIDE_TICKS) cheatShakeTicks = 4u;
    if (cheatRevealFrame >= CHEAT_SLIDE_TICKS + CHEAT_SLIDE_GAP) {
        cheatRevealFrame = 0;
        ++cheatRevealStage;
    }
}

/* Cheats are invisible until the code is entered: pre-code the screen offers
 * only BALL TRAIL and BACK, and navigation skips the hidden rows entirely. */
static void draw_settings(void) {
    static const char *labels[6] = { "BALL TRAIL", "BLOCK SHIELD", "SPEED BOOST", "DOUBLE COINS", "SLOW-MO CHEAT", "BACK" };
    static const uint8_t bits[5] = { SET_TRAIL, SET_INVULNERABLE, SET_SPEED_CHEAT, SET_DOUBLE_COIN, SET_SLOW_CHEAT };
    uint8_t i;
    int16_t y = 52;
    int16_t shakeX = cheatShakeTicks ? ((cheatShakeTicks & 1u) ? -2 : 2) : 0;
    gfx_FillScreen(C_BLACK);
    draw_stars(12);
    set_scale(2);
    print_at("SETTINGS", 96 + shakeX, 13, C_WHITE, C_BLACK);
    set_scale(1);
    for (i = 0; i < 6u; ++i) {
        int16_t x = shakeX;
        uint8_t revealRow = (uint8_t)(i > 0u && i < 5u);
        if (revealRow && !save.cheatsUnlocked) continue;   /* hidden until the code: not LOCKED, gone */
        if (revealRow && cheatRevealStage < CHEAT_COUNT) {
            uint8_t rowStage = (uint8_t)(i - 1u);
            if (rowStage > cheatRevealStage) continue;
            if (rowStage == cheatRevealStage) {
                uint8_t progress = cheatRevealFrame > CHEAT_SLIDE_TICKS ? CHEAT_SLIDE_TICKS : cheatRevealFrame;
                x = (int16_t)(x + ((CHEAT_SLIDE_TICKS - progress) * SW) / CHEAT_SLIDE_TICKS);
            }
        }
        if (settingsIndex == (int8_t)i) {
            gfx_SetColor(C_GRID_DIM); gfx_FillRectangle((int16_t)(22 + x), y - 3, 276, 21);
            gfx_SetColor(C_NEON); gfx_Rectangle((int16_t)(22 + x), y - 3, 276, 21);
        }
        print_at(labels[i], (int16_t)(31 + x), y + 2,
                 settingsIndex == (int8_t)i ? C_NEON_HI : C_WHITE, C_BLACK);
        if (i < 5)
            print_at((save.settings & bits[i]) ? "ON" : "OFF", (int16_t)(258 + x), y + 2,
                     (save.settings & bits[i]) ? C_NEON : C_GRAY, C_BLACK);
        y = (int16_t)(y + 25);
    }
}

static void check_cheat_code(uint8_t token) {
    static const uint8_t code[CHEAT_CODE_LENGTH] = {
        DIR_UP, DIR_UP, DIR_UP, DIR_DOWN, DIR_DOWN, DIR_DOWN,
        DIR_LEFT, DIR_LEFT, DIR_LEFT, DIR_RIGHT, DIR_RIGHT, DIR_RIGHT,
        CODE_4, CODE_5, CODE_6, CODE_SIN, CODE_COS, CODE_TAN, CODE_6, CODE_9
    };
    if (!token) return;
    if (token == code[cheatProgress]) {
        ++cheatProgress;
        if (cheatProgress == CHEAT_CODE_LENGTH) {
            save.cheatsUnlocked = 1;
            save.settings &= (uint8_t)~(SET_INVULNERABLE | SET_SPEED_CHEAT |
                                        SET_DOUBLE_COIN | SET_SLOW_CHEAT);
            cheatNotice = 180;
            cheatRevealStage = 0;
            cheatRevealFrame = 0;
            cheatShakeTicks = 0;
            cheatProgress = 0;
            save_game();
        }
    } else cheatProgress = (token == code[0]) ? 1u : 0u;
}

static void navigate_title(enum Direction d) {
    if (d == DIR_UP || d == DIR_LEFT) menuIndex = (int8_t)((menuIndex + 5) % 6);
    else if (d == DIR_DOWN || d == DIR_RIGHT) menuIndex = (int8_t)((menuIndex + 1) % 6);
}

static void start_daily(void) {
    dailyMode = 1;
    reset_run();
    screen = SCREEN_GAME;
}

static void handle_title_select(void) {
    if (menuIndex == 0) { dailyMode = 0; reset_run(); screen = SCREEN_GAME; }
    else if (menuIndex == 1) { garageTab = 0; garageShapeIndex = save.activeShape; screen = SCREEN_GARAGE; }
    else if (menuIndex == 2) enter_worlds();
    else if (menuIndex == 3) { shopIndex = 0; screen = SCREEN_SHOP; }
    else if (menuIndex == 4) { settingsIndex = 0; screen = SCREEN_SETTINGS; }
    else if (menuIndex == 5) quitRequested = 1;
}

static void handle_garage(const InputFrame *k) {
    if (k->back) { screen = SCREEN_TITLE; return; }
    if (k->direction == DIR_UP || k->direction == DIR_DOWN) garageTab = (int8_t)!garageTab;
    if (garageTab == 0) {
        if (k->direction == DIR_LEFT) garageShapeIndex = (uint8_t)((garageShapeIndex + 2u) % 3u);
        if (k->direction == DIR_RIGHT) garageShapeIndex = (uint8_t)((garageShapeIndex + 1u) % 3u);
        /* Browsing never equips; locked shapes need enough coins and one explicit purchase. */
        if (k->select) {
            uint8_t shape = garageShapeIndex;
            uint32_t cost = shape_cost(shape);
            if (shape_owned(shape)) {
                save.activeShape = shape;
            } else if (save.coins >= cost) {
                save.coins -= cost;
                save.ownedShapes |= (uint8_t)(1u << shape);
                save.activeShape = shape;
            }
            save_game();
        }
    } else {
        if (k->direction == DIR_LEFT) save.colorTheme = (uint8_t)((save.colorTheme + 3u) % 4u);
        if (k->direction == DIR_RIGHT) save.colorTheme = (uint8_t)((save.colorTheme + 1u) % 4u);
        if (k->direction == DIR_LEFT || k->direction == DIR_RIGHT) { set_palette(); save_game(); }
    }
}

/* HEAVEN is a hidden screen beneath the worlds list. Once it has been visited, pressing 8
 * three times in a row slides the worlds away to reveal it. */
static void handle_worlds(const InputFrame *k) {
    if (heavenDir) {
        heavenSlide = (int16_t)(heavenSlide + heavenDir * HEAVEN_SLIDE_STEP);
        if (heavenSlide >= SH) { heavenSlide = SH; heavenDir = 0; heavenView = 1; }
        else if (heavenSlide <= 0) { heavenSlide = 0; heavenDir = 0; heavenView = 0; }
        return;
    }
    if (heavenView) {
        if (k->select) begin_heaven_ascent();
        else if (k->back || k->direction != DIR_NONE) heavenDir = -1;
        return;
    }
    if (k->back) { screen = SCREEN_TITLE; return; }
    if (k->key8 && heaven_visited()) {
        if (++heavenPresses >= 3u) { heavenPresses = 0; heavenDir = 1; }
        return;
    }
    if (k->direction != DIR_NONE || k->select) heavenPresses = 0;
    if (k->direction == DIR_LEFT)  worldPlanet = (int8_t)((worldPlanet + 2) % 3);
    if (k->direction == DIR_RIGHT) worldPlanet = (int8_t)((worldPlanet + 1) % 3);
    if (k->direction == DIR_UP || k->direction == DIR_DOWN) worldAction = (int8_t)!worldAction;
    if (k->select) world_action_execute();
}

static void handle_shop(const InputFrame *k) {
    if (k->back) { screen = SCREEN_TITLE; return; }
    if (k->direction == DIR_UP) shopIndex = (int8_t)((shopIndex + 3) % 4);
    if (k->direction == DIR_DOWN) shopIndex = (int8_t)((shopIndex + 1) % 4);
    if (k->select) {
        if (shopIndex == 3) screen = SCREEN_TITLE;
        else shop_execute();
    }
}

/* Skips the cheat rows while they are hidden, so UP/DOWN cycles only what exists. */
static int8_t next_settings_index(int8_t cur, int8_t delta) {
    int8_t i = cur;
    do { i = (int8_t)((((i + delta) % 6) + 6) % 6); }
    while (!save.cheatsUnlocked && i > 0 && i < 5);
    return i;
}

static void handle_settings(const InputFrame *k) {
    uint8_t codeInput;
    if (k->back) { screen = SCREEN_TITLE; cheatProgress = 0; return; }
    codeInput = (uint8_t)(cheatProgress || k->codeToken == DIR_UP);
    check_cheat_code(k->codeToken);
    if (k->direction == DIR_UP) settingsIndex = next_settings_index(settingsIndex, -1);
    if (k->direction == DIR_DOWN) settingsIndex = next_settings_index(settingsIndex, 1);
    if (k->select || ((k->direction == DIR_LEFT || k->direction == DIR_RIGHT) && !codeInput)) {
        toggle_setting(settingsIndex);
    }
    update_cheat_reveal();
}
static void draw_bests(void) {
    static const char *names[3] = { "NEON", "CYAN", "VOID" };
    char date[11];
    uint8_t day, month, i;
    uint16_t year;
    today_date(&day, &month, &year);
    format_date(date, month, day, year);
    gfx_FillScreen(C_BLACK);
    draw_stars(12);
    set_scale(2);
    print_center("BESTS", 13, C_WHITE, C_BLACK);
    set_scale(1);
    print_at("OVERALL", 30, 52, C_WHITE, C_BLACK);
    print_number_at_right(save.bestScore, 290, 52, C_NEON_HI, C_BLACK);
    print_at("DAILY RUN", 30, 74, C_GOLD, C_BLACK);
    print_number_at_right(daily_best_today(), 290, 74, C_NEON_HI, C_BLACK);
    print_at(date, 30, 86, C_GRAY, C_BLACK);
    print_at("BY WORLD", 30, 108, C_GRID, C_BLACK);
    for (i = 0; i < 3u; ++i) {
        print_at(names[i], 46, (int16_t)(124 + i * 16), C_WHITE, C_BLACK);
        print_number_at_right(save.worldBest[i], 290, (int16_t)(124 + i * 16), C_NEON_HI, C_BLACK);
    }
    if (heaven_visited()) {
        print_at("HEAVEN", 46, 172, C_GOLD, C_BLACK);
        print_number_at_right(save.worldBest[3], 290, 172, C_GOLD, C_BLACK);
    }
    print_center("2ND/CLEAR: BACK", 224, C_GRAY, C_BLACK);
}

static void draw_codex(void) {
    uint8_t i;
    uint8_t got = lore_has((uint8_t)codexIndex);
    gfx_FillScreen(C_BLACK);
    draw_stars(12);
    set_scale(2);
    print_center("CODEX", 10, C_WHITE, C_BLACK);
    set_scale(1);
    for (i = 0; i < LORE_COUNT; ++i) {
        int16_t y = (int16_t)(36 + i * 15);
        uint8_t have = lore_has(i);
        char num[2];
        if (codexIndex == (int8_t)i) {
            gfx_SetColor(C_GRID_DIM); gfx_FillRectangle(40, y - 2, 240, 14);
            gfx_SetColor(C_NEON);     gfx_Rectangle(40, y - 2, 240, 14);
        }
        num[0] = (char)('1' + i); num[1] = '\0';
        print_at(num, 48, y, have ? C_GOLD : C_GRAY, C_BLACK);
        print_at(have ? loreTitle[i] : "???", 66, y,
                 codexIndex == (int8_t)i ? C_NEON_HI : (have ? C_WHITE : C_GRAY), C_BLACK);
    }
    gfx_SetColor(C_GRID);
    gfx_Rectangle(8, 160, 304, 62);
    if (got) {
        for (i = 0; i < 3u; ++i)
            print_at(loreText[codexIndex][i], 16, (int16_t)(168 + i * 14), C_WHITE, C_BLACK);
    } else {
        char line[40];
        print_at("LOCKED", 16, 168, C_GRAY, C_BLACK);
        memcpy(line, "UNLOCK: ", 8);
        strcpy(line + 8, loreHint[codexIndex]);
        print_at(line, 16, 184, C_GRAY, C_BLACK);
    }
    print_center("UP/DOWN: READ   CLEAR: BACK", 226, C_GRAY, C_BLACK);
}

static void handle_codex(const InputFrame *k) {
    if (k->back) { screen = SCREEN_TITLE; return; }
    if (k->direction == DIR_UP) codexIndex = (int8_t)((codexIndex + LORE_COUNT - 1) % LORE_COUNT);
    if (k->direction == DIR_DOWN) codexIndex = (int8_t)((codexIndex + 1) % LORE_COUNT);
}

/* One frame of the whole game; returns 0 when Quit was chosen. Kept separate from main()
 * so the simulation tests can drive the real game loop. */
static uint8_t frame_step(const InputFrame *keys) {
    if (screen == SCREEN_TITLE) {
        if (keys->menuNumber) {
            menuIndex = (int8_t)(keys->menuNumber - 1u);
            handle_title_select();
        } else if (keys->fkey == 2u) start_daily();
        else if (keys->fkey == 3u) screen = SCREEN_BESTS;
        else if (keys->fkey == 4u) { codexIndex = 0; loreNew = 0; screen = SCREEN_CODEX; }
        else {
            if (keys->direction != DIR_NONE) navigate_title((enum Direction)keys->direction);
            if (keys->select) handle_title_select();
        }
        if (quitRequested) return 0;
        if (screen == SCREEN_TITLE) draw_title();
    } else if (screen == SCREEN_GAME) {
        if (keys->back && realmMode == REALM_NORMAL) {
            if (gemsDirty) save_game();
            screen = SCREEN_TITLE;
        } else update_game(keys);
        if (screen == SCREEN_GAME) draw_game_world();
    } else if (screen == SCREEN_EVENT) {
        update_event(keys);
        if (screen == SCREEN_EVENT) draw_event();
    } else if (screen == SCREEN_DEATH) {
        if (keys->back && deathPhase == 2) { loreNew = 0; screen = SCREEN_TITLE; }
        else {
            update_death();
            if (deathPhase == 2) {
                /* F1 F2 F3 F4 F5 in order: the way to HEAVEN. Only after the payout is saved. */
                if (keys->fkey) {
                    if (keys->fkey == heavenSeq + 1u) {
                        if (++heavenSeq == 5u) { heavenSeq = 0; loreNew = 0; begin_heaven_ascent(); }
                    } else heavenSeq = (uint8_t)(keys->fkey == 1u ? 1u : 0u);
                }
                if (keys->direction == DIR_UP) deathMenuIndex = (int8_t)((deathMenuIndex + 2) % 3);
                if (keys->direction == DIR_DOWN) deathMenuIndex = (int8_t)((deathMenuIndex + 1) % 3);
                if (keys->select && screen == SCREEN_DEATH) {
                    loreNew = 0;
                    if (deathMenuIndex == 0) { reset_run(); screen = SCREEN_GAME; }
                    else if (deathMenuIndex == 1) enter_worlds();
                    else screen = SCREEN_TITLE;
                }
            }
        }
        if (screen == SCREEN_DEATH) draw_death();
    } else if (screen == SCREEN_GARAGE) {
        handle_garage(keys);
        if (screen == SCREEN_GARAGE) draw_garage();
    } else if (screen == SCREEN_WORLDS) {
        handle_worlds(keys);
        if (screen == SCREEN_WORLDS) draw_worlds();
    } else if (screen == SCREEN_SHOP) {
        handle_shop(keys);
        if (screen == SCREEN_SHOP) draw_shop();
    } else if (screen == SCREEN_SETTINGS) {
        handle_settings(keys);
        if (screen == SCREEN_SETTINGS) draw_settings();
    } else if (screen == SCREEN_BESTS) {
        if (keys->back || keys->select) screen = SCREEN_TITLE;
        if (screen == SCREEN_BESTS) draw_bests();
    } else if (screen == SCREEN_CODEX) {
        handle_codex(keys);
        if (screen == SCREEN_CODEX) draw_codex();
    }
    return 1;
}

int main(void) {
    InputFrame keys;
    load_game();
    timer_Enable(1, TIMER_32K, TIMER_NOINT, TIMER_UP);
    gfx_Begin();
    gfx_SetDrawBuffer();
    set_palette();
    build_original_projection();
    gfx_SetTransparentColor(255);

    while (1) {
        keys = poll_keys();
        ++uiFrame;
        if (!frame_step(&keys)) break;
        gfx_SwapDraw();
    }

    save_game();
    gfx_End();
    return 0;
}