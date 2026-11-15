#define _CRT_SECURE_NO_WARNINGS

#include "plugin.h"
#include "common.h"
#include "CSprite2d.h"
#include "CTxdStore.h"
#include "CStats.h"
#include "CWorld.h"
#include "CFont.h"
#include "CCamera.h"
#include "CTheScripts.h"
#include "CText.h"
#include "CMenuManager.h"
#include "CHud.h"
#include "CTimer.h"
#include "CClock.h"
#include "CCutsceneMgr.h"
#include "RenderWare.h"
#include "CPlayerData.h"
#include "CPlayerInfo.h"
#include "CPlayerPed.h"
#include "extensions/ScriptCommands.h"

#include <windows.h>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>

using namespace plugin;

// -------------------------------------------------------------
// Ustawienia HUD
// -------------------------------------------------------------

bool bHudEnabledInIni = true;
bool g_bFinalHudStatus = true;
bool bMenuSettingMemory = true;
bool moneyDiffVisibleShared = false;
bool bMoneyPadZeros = true;

std::string sHudMode = "lcs";
int HudAlpha = 225;

int outlineR = 255, outlineG = 255, outlineB = 255;
int healthR = 255, healthG = 255, healthB = 255;
int armorR = 255, armorG = 255, armorB = 255;

int breathR = 0, breathG = 100, breathB = 255;
int breathBgR = 0, breathBgG = 20, breathBgB = 60;

int plusHealthR = 255, plusHealthG = 255, plusHealthB = 255;
int plusArmorR = 255, plusArmorG = 255, plusArmorB = 255;

// -------------------------------------------------------------
// Tlen
// -------------------------------------------------------------

static float lastBreath = -1.0f;
static float breathVisibilityTimer = 0.0f;
static float breathGracePeriod = 200.0f;
static float colorFadeFactor = 0.0f;

// -------------------------------------------------------------
// Dane przekazywane do Radar.cpp
// -------------------------------------------------------------

float g_LeedsRadarHealthPercent = 1.0f;
float g_LeedsRadarArmorPercent = 0.0f;
float g_LeedsRadarBreathPercent = 1.0f;

bool g_LeedsRadarIndicatorsEnabled = false;
bool g_LeedsRadarShowArmor = false;
bool g_LeedsRadarShowBreath = false;

bool g_LeedsRadarShowHealthPlus = false;
bool g_LeedsRadarShowArmorPlus = false;

unsigned char g_LeedsRadarIndicatorAlpha = 225;

// -------------------------------------------------------------
// Pozycje i rozmiary elementów HUD
// -------------------------------------------------------------

const float valWepX = 69.00f;
const float valWepY = 84.80f;
const float valWepW = 177.00f;
const float valWepH = 173.00f;

const float valHFX = 423.50f;
const float valHFY = 157.50f;
const float valHFW = 173.80f;
const float valHFH = 39.60f;

const float valHBX = 401.50f;
const float valHBY = 157.50f;
const float valHBW = 150.40f;
const float valHBH = 38.60f;

const float valAFX = 423.50f;
const float valAFY = 108.50f;
const float valAFW = 173.80f;
const float valAFH = 39.60f;

const float valABX = 401.50f;
const float valABY = 108.50f;
const float valABW = 150.40f;
const float valABH = 38.60f;

const float valAmX = 125.50f;
const float valAmY = 167.00f;
const float valAmW = 0.71f;
const float valAmH = 0.84f;

const float valClX = 249.00f;
const float valClY = 62.50f;
const float valClW = 1.822f;
const float valClH = 2.05f;

const float valMnX = 118.00f;
const float valMnY = 206.00f;
const float valMnW = 1.826f;
const float valMnH = 2.05f;

const float valStX = 110.70f;
const float valStY = 145.00f;
const float valStW = 37.50f;
const float valStH = 7.40f;

// -------------------------------------------------------------
// Tekstury broni
// -------------------------------------------------------------

static const char* weaponTextureNames[] = {
    "fist", "brassknuckle", "golfclub", "nitestick",
    "knifecur", "batcur", "shovel", "poolcue",
    "katana", "chnsaw", "gun_dildo1", "gun_dildo2",
    "gun_vibe1", "gun_vibe2", "flower", "cane",
    "grenade", "teargas", "molotov", "null",
    "null", "null", "pistol", "silenced",
    "desert_eagle", "chromegun", "sawnoff", "shotgspa",
    "micro_uzi", "mp5lng", "ak47", "m4",
    "tec9", "cuntgun", "sniper", "rocketla",
    "hsrocket", "flamethrower", "minigun", "satchel",
    "detonator", "spraycan", "fire_ex", "camera",
    "nvgoog", "irgoog", "parachute"
};

static RwTexture* (__cdecl* _RwTextureRead)(const char*, const char*) =
    (RwTexture* (__cdecl*)(const char*, const char*))0x7F3AC0;

// -------------------------------------------------------------
// Własna czcionka HUD
// -------------------------------------------------------------

void DrawCustomFontString(
    CSprite2d& sprite,
    const std::string& text,
    float startX,
    float startY,
    float charWidth,
    float charHeight,
    bool alignRight,
    bool isCash,
    float spacing,
    unsigned char targetAlpha,
    CRGBA tintOverride = CRGBA(255, 255, 255, 255))
{
    if (!sprite.m_pTexture)
        return;

    const float texW = 128.0f;
    const float texH = 64.0f;
    const float charW = 16.0f;
    const float charH = 17.0f;

    auto getSpacing = [&](char c) -> float {
        return (c == ':') ? 0.5f :
            (c == '*') ? 1.2f :
            (c == '-') ? 1.0f : spacing;
    };

    float totalWidth = 0.0f;

    for (char c : text)
        totalWidth += charWidth * getSpacing(c);

    float currentX = alignRight ? (startX - totalWidth) : startX;

    for (char c : text) {
        float u1, v1, u2, v2, u3, v3, u4, v4;

        if (c == '-') {
            u1 = 0.0f / texW;
            v1 = 51.0f / texH;
            u2 = 13.0f / texW;
            v2 = 51.0f / texH;
            u3 = 0.0f / texW;
            v3 = 64.0f / texH;
            u4 = 13.0f / texW;
            v4 = 64.0f / texH;
        }
        else {
            int g =
                (c >= '0' && c <= '9') ? (isCash ? (c - '0') + 11 : (c - '0')) :
                (c == ':') ? 10 :
                (c == '$') ? 21 :
                (c == '*') ? 22 : -1;

            if (g == -1) {
                currentX += charWidth * getSpacing(c);
                continue;
            }

            int col = g % 8;
            int row = g / 8;

            float glyphW = (c == '*') ? 16.0f : 13.0f;
            float glyphH = 16.0f;

            u1 = (col * charW) / texW;
            v1 = (row * charH) / texH;
            u2 = ((col * charW) + glyphW) / texW;
            v2 = v1;
            u3 = u1;
            v3 = ((row * charH) + glyphH) / texH;
            u4 = u2;
            v4 = v3;
        }

        CRGBA glyphColor;

        if (c == '*') {
            bool callerOverrode =
                !(tintOverride.r == 255 &&
                  tintOverride.g == 255 &&
                  tintOverride.b == 255);

            glyphColor = callerOverrode
                ? CRGBA(tintOverride.r, tintOverride.g, tintOverride.b, targetAlpha)
                : CRGBA(187, 149, 60, targetAlpha);
        }
        else {
            glyphColor = CRGBA(
                tintOverride.r,
                tintOverride.g,
                tintOverride.b,
                targetAlpha
            );
        }

        sprite.Draw(
            CRect(
                currentX,
                startY,
                currentX + charWidth,
                startY + charHeight
            ),
            glyphColor,
            u1, v1, u2, v2, u3, v3, u4, v4
        );

        currentX += charWidth * getSpacing(c);
    }
}

// -------------------------------------------------------------
// Status ManhuntHUD
// -------------------------------------------------------------

class LeedsHUDManStatus {
public:
    static CSprite2d statusSprites[5];

    static bool bStatusLoaded;

    static int nCurrentState;
    static int nPreviousState;
    static int nPendingState;

    static float fStabilityTimer;
    static float fStateLockTimer;
    static float fFadeProgress;

    static float fStatusOffsetX;
    static float fStatusOffsetY;
    static float fStatusScale;

    static float StatusRes(float value) {
        return value * ((float)RsGlobal.maximumHeight / 960.0f);
    }

    // ---------------------------------------------------------
    // Odczyt wartości typu float z LeedsHUD.ini
    // ---------------------------------------------------------

    static float ReadConfigFloat(
        const char* iniPath,
        const char* key,
        float defaultValue)
    {
        char defaultBuffer[64];
        char valueBuffer[128];

        snprintf(
            defaultBuffer,
            sizeof(defaultBuffer),
            "%.6f",
            static_cast<double>(defaultValue)
        );

        GetPrivateProfileStringA(
            "ManhuntHUD",
            key,
            defaultBuffer,
            valueBuffer,
            sizeof(valueBuffer),
            iniPath
        );

        char* end = nullptr;
        float value = std::strtof(valueBuffer, &end);

        if (end == valueBuffer || !std::isfinite(value))
            return defaultValue;

        return value;
    }

    // ---------------------------------------------------------
    // Wczytywanie ustawień ManhuntHUD z LeedsHUD.ini
    // ---------------------------------------------------------

    static void LoadConfig(const char* iniPath) {
        if (!iniPath || !iniPath[0])
            return;

        fStatusOffsetX = ReadConfigFloat(
            iniPath,
            "StatusOffsetX",
            -1455.0f
        );

        fStatusOffsetY = ReadConfigFloat(
            iniPath,
            "StatusOffsetY",
            -151.0f
        );

        fStatusScale = ReadConfigFloat(
            iniPath,
            "StatusScale",
            0.65f
        );

        if (fStatusScale <= 0.0f)
            fStatusScale = 0.65f;
    }

    // ---------------------------------------------------------
    // Ładowanie tekstur ManhuntHUD
    // ---------------------------------------------------------

    static void Load(const std::string& modPath) {
        bStatusLoaded = false;

        int txdSlot = CTxdStore::AddTxdSlot("mhud_status");

        if (!CTxdStore::LoadTxd(
            txdSlot,
            (modPath + "MHud.TXD").c_str()))
        {
            return;
        }

        CTxdStore::AddRef(txdSlot);
        CTxdStore::PushCurrentTxd();
        CTxdStore::SetCurrentTxd(txdSlot);

        statusSprites[0].SetTexture((char*)"manstand");
        statusSprites[1].SetTexture((char*)"manjog");
        statusSprites[2].SetTexture((char*)"manrun");
        statusSprites[3].SetTexture((char*)"manjump");
        statusSprites[4].SetTexture((char*)"mancrouch");

        CTxdStore::PopCurrentTxd();

        bStatusLoaded = true;
    }

    // ---------------------------------------------------------
    // Zwalnianie tekstur
    // ---------------------------------------------------------

    static void Shutdown() {
        for (int i = 0; i < 5; i++)
            statusSprites[i].Delete();

        bStatusLoaded = false;
    }

    // ---------------------------------------------------------
    // Rysowanie pojedynczej ikony statusu
    // ---------------------------------------------------------

    static void DrawSprite(int state, float masterAlpha, CRGBA color, CPlayerPed* player) {
        if (!player || state < 0 || state >= 5)
            return;

        CSprite2d* currentSprite = &statusSprites[state];

        if (!currentSprite->m_pTexture)
            return;

        float hX = (float)RsGlobal.maximumWidth - StatusRes(80.0f);
        float hY = (float)RsGlobal.maximumHeight - StatusRes(55.0f);
        float xOffset = StatusRes(100.0f);

        if (state == 0)
            xOffset -= StatusRes(26.0f);
        else if (state == 1)
            xOffset -= StatusRes(12.0f);
        else if (state == 2)
            xOffset -= StatusRes(6.0f);
        else if (state == 4)
            xOffset -= StatusRes(19.0f);

        if (player->m_fArmour > 1.0f)
            xOffset += StatusRes(23.0f);

        if (breathVisibilityTimer > 0.0f)
            xOffset += StatusRes(23.0f);

        float w = StatusRes(80.0f);
        float h = StatusRes(158.0f);

        if (state == 3) {
            w = StatusRes(158.0f);
            h = StatusRes(158.0f);
        }
        else if (state == 2) {
            w = StatusRes(86.0f);
            h = StatusRes(167.0f);
        }

        // Apply custom scale to all status icons.
        w *= fStatusScale;
        h *= fStatusScale;

        float posX = (hX - xOffset) - (w / 2.0f);
        float posY = (hY - StatusRes(25.0f)) - h - StatusRes(15.0f);

        // Apply custom position from LeedsHUD.ini.
        posX += StatusRes(fStatusOffsetX);
        posY += StatusRes(fStatusOffsetY);

        color.a = (unsigned char)std::clamp(masterAlpha, 0.0f, 255.0f);

        currentSprite->Draw(
            CRect(posX, posY, posX + w, posY + h),
            color
        );
    }

    // ---------------------------------------------------------
    // Aktualizacja stanu gracza
    // ---------------------------------------------------------

    static void UpdateState(CPlayerPed* p) {
        if (!p)
            return;

        static unsigned int nOldTime = 0;

        unsigned int nCurrentTime = CTimer::m_snTimeInMilliseconds;

        float fDelta = (float)(nCurrentTime - nOldTime);
        nOldTime = nCurrentTime;

        if (fDelta <= 0.0f || fDelta > 100.0f)
            fDelta = 16.66f;

        if (fStateLockTimer > 0.0f) {
            fStateLockTimer -= fDelta;
            return;
        }

        float speed = p->m_vecMoveSpeed.Magnitude();
        float vSpeed = p->m_vecMoveSpeed.z;

        int targetState = nCurrentState;

        const float jogStop = 0.015f;

        if (Command<0x0597>(p))
            targetState = 4;
        else if (vSpeed > 0.02f || vSpeed < -0.18f)
            targetState = 3;
        else if (p->m_nMoveState == PEDMOVE_SPRINT)
            targetState = 2;
        else {
            if (speed < jogStop)
                targetState = 0;
            else
                targetState = 1;
        }

        if (targetState != nCurrentState) {
            if (targetState == nPendingState) {
                fStabilityTimer += fDelta;

                float fRequiredTime = 360.0f;

                if (targetState == 3 || targetState == 0)
                    fRequiredTime = 0.0f;

                if (fStabilityTimer >= fRequiredTime) {
                    nPreviousState = nCurrentState;
                    nCurrentState = targetState;

                    fFadeProgress = 0.0f;
                    fStabilityTimer = 0.0f;
                    fStateLockTimer = 40.0f;
                }
            }
            else {
                nPendingState = targetState;
                fStabilityTimer = 0.0f;
            }
        }
        else {
            fStabilityTimer = 0.0f;
        }
    }

    // ---------------------------------------------------------
    // Główne rysowanie ManhuntHUD
    // ---------------------------------------------------------

    static void Draw(CPlayerPed* player) {
        if (!player || !bStatusLoaded)
            return;

        if (CCutsceneMgr::ms_cutsceneProcessing ||
            TheCamera.m_bWideScreenOn ||
            !g_bFinalHudStatus ||
            CHud::bScriptDontDisplayRadar)
            return;

        UpdateState(player);

        CRGBA drawColor(255, 255, 255, 255);

        if (player->IsHidden())
            drawColor = CRGBA(50, 60, 127, 255);

        int wantedLevel = 0;

        if (player->m_pPlayerData &&
            player->m_pPlayerData->m_pWanted)
        {
            wantedLevel = player->m_pPlayerData->m_pWanted->m_nWantedLevel;
        }

        if (wantedLevel > 0) {
            int timer = CTimer::m_snTimeInMilliseconds % 300;

            if (timer < 100)
                drawColor = CRGBA(255, 255, 255, 255);
            else if (timer < 200)
                drawColor = CRGBA(255, 150, 0, 255);
            else
                drawColor = CRGBA(200, 0, 0, 255);
        }

        if (fFadeProgress < 1.0f) {
            fFadeProgress += CTimer::ms_fTimeStep * 0.12f;

            if (fFadeProgress > 1.0f)
                fFadeProgress = 1.0f;

            DrawSprite(
                nPreviousState,
                (1.0f - fFadeProgress) * 255.0f,
                drawColor,
                player
            );

            DrawSprite(
                nCurrentState,
                fFadeProgress * 255.0f,
                drawColor,
                player
            );
        }
        else {
            DrawSprite(
                nCurrentState,
                255.0f,
                drawColor,
                player
            );
        }
    }
};

// -------------------------------------------------------------
// Definicje statycznych elementów ManhuntHUD
// -------------------------------------------------------------

CSprite2d LeedsHUDManStatus::statusSprites[5];

bool LeedsHUDManStatus::bStatusLoaded = false;

int LeedsHUDManStatus::nCurrentState = 0;
int LeedsHUDManStatus::nPreviousState = 0;
int LeedsHUDManStatus::nPendingState = 0;

float LeedsHUDManStatus::fStabilityTimer = 0.0f;
float LeedsHUDManStatus::fStateLockTimer = 0.0f;
float LeedsHUDManStatus::fFadeProgress = 1.0f;

float LeedsHUDManStatus::fStatusOffsetX = -1455.0f;
float LeedsHUDManStatus::fStatusOffsetY = -151.0f;
float LeedsHUDManStatus::fStatusScale = 0.65f;

// -------------------------------------------------------------
// Główny HUD
// -------------------------------------------------------------

class LeedsHUDMain {
public:
    static CSprite2d outlineSprite;
    static CSprite2d weaponSprites[47];
    static CSprite2d hudNumbersSprite;
    static CSprite2d healthBarSprite;
    static CSprite2d armorBarSprite;
    static CSprite2d ammoFontSprite;
    static CSprite2d plusSprite;

    static bool bLoaded;

    static int s_LastWeaponSlot;
    static int s_LastWeaponType;
    static unsigned int s_WeaponSelectorChangeTime;
    static bool s_WeaponSelectorInitialized;

    static constexpr unsigned int WEAPON_SELECTOR_VISIBLE_TIME = 2000;
    static constexpr unsigned int WEAPON_SELECTOR_FADE_TIME = 300;

    static float Res(float value) {
        return value * ((float)RsGlobal.maximumHeight / 1080.0f);
    }

    static float ResW(float value) {
        return value * ((float)RsGlobal.maximumHeight / 1080.0f);
    }

    static std::string GetAsiFolder() {
        char buffer[MAX_PATH];
        HMODULE hm = NULL;

        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&bLoaded,
            &hm
        );

        GetModuleFileNameA(hm, buffer, MAX_PATH);

        std::string path(buffer);
        size_t lastSlash = path.find_last_of("\\/");

        return path.substr(0, lastSlash) + "\\";
    }

    static bool IsWeaponSlotUsable(CPlayerPed* player, int slot) {
        if (!player || slot < 0 || slot >= 13)
            return false;

        int weaponId = player->m_aWeapons[slot].m_eWeaponType;

        if (weaponId < 0 || weaponId >= 47)
            return false;

        if (weaponId == 0 && slot != 0)
            return false;

        if (strcmp(weaponTextureNames[weaponId], "null") == 0)
            return false;

        return weaponSprites[weaponId].m_pTexture != nullptr;
    }

    static void DrawWeaponSelectorIcon(
        int weaponId,
        float centerX,
        float centerY,
        float width,
        float height,
        unsigned char alpha,
        bool selected = false)
    {
        if (weaponId < 0 || weaponId >= 47)
            return;

        if (!weaponSprites[weaponId].m_pTexture)
            return;

        float left = centerX - width * 0.5f;
        float right = centerX + width * 0.5f;
        float top = centerY - height * 0.5f;
        float bottom = centerY + height * 0.5f;

        CRGBA color(255, 255, 255, alpha);

        if (selected) {
            weaponSprites[weaponId].Draw(
                CRect(left, top, right, bottom),
                color,
                0.0f, 0.0f,
                1.0f, 0.0f,
                0.0f, 1.0f,
                1.0f, 1.0f
            );
        }
        else {
            weaponSprites[weaponId].Draw(
                left, bottom,
                right, bottom,
                left, top,
                right, top,
                color
            );
        }
    }

    static void DrawWeaponSelector(CPlayerPed* player) {
        if (!player || !player->m_pPlayerData)
            return;

        const int WEAPON_SLOT_COUNT = 13;

        int selectedSlot = player->m_nSelectedWepSlot;

        if (selectedSlot < 0 || selectedSlot >= WEAPON_SLOT_COUNT)
            return;

        int selectedWeapon =
            player->m_aWeapons[selectedSlot].m_eWeaponType;

        unsigned int now = CTimer::m_snTimeInMilliseconds;

        if (!s_WeaponSelectorInitialized) {
            s_LastWeaponSlot = selectedSlot;
            s_LastWeaponType = selectedWeapon;
            s_WeaponSelectorChangeTime = now;
            s_WeaponSelectorInitialized = true;
        }
        else if (
            selectedSlot != s_LastWeaponSlot ||
            selectedWeapon != s_LastWeaponType
        ) {
            s_LastWeaponSlot = selectedSlot;
            s_LastWeaponType = selectedWeapon;
            s_WeaponSelectorChangeTime = now;
        }

        float centerX =
            ResW(valWepX) + ResW(valWepW) * 0.5f;

        float centerY =
            static_cast<float>(RsGlobal.maximumHeight) -
            Res(valWepY) -
            Res(valWepH) * 0.5f -
            Res(35.0f);

        if (IsWeaponSlotUsable(player, selectedSlot)) {
            unsigned char selectedAlpha =
                static_cast<unsigned char>(
                    std::clamp(
                        HudAlpha * (245.0f / 255.0f),
                        0.0f,
                        255.0f
                    )
                );

            DrawWeaponSelectorIcon(
                selectedWeapon,
                centerX,
                centerY,
                ResW(155.0f),
                Res(152.93f),
                selectedAlpha,
                true
            );
        }

        unsigned int elapsed = now - s_WeaponSelectorChangeTime;

        if (elapsed >=
            WEAPON_SELECTOR_VISIBLE_TIME + WEAPON_SELECTOR_FADE_TIME)
            return;

        float fade = 1.0f;

        if (elapsed > WEAPON_SELECTOR_VISIBLE_TIME) {
            float fadeProgress =
                static_cast<float>(elapsed - WEAPON_SELECTOR_VISIBLE_TIME) /
                static_cast<float>(WEAPON_SELECTOR_FADE_TIME);

            fade = 1.0f - std::clamp(fadeProgress, 0.0f, 1.0f);
        }

        unsigned char sideAlpha =
            static_cast<unsigned char>(
                std::clamp(
                    (HudAlpha / 255.0f) * 245.0f * fade * 0.72f,
                    0.0f,
                    255.0f
                )
            );

        if (sideAlpha == 0)
            return;

        int foundAbove = 0;
        int slotAbove = selectedSlot;

        for (int checked = 0;
            checked < WEAPON_SLOT_COUNT - 1 && foundAbove < 2;
            ++checked) {

            slotAbove--;

            if (slotAbove < 0)
                slotAbove = WEAPON_SLOT_COUNT - 1;

            if (!IsWeaponSlotUsable(player, slotAbove))
                continue;

            int weaponId =
                player->m_aWeapons[slotAbove].m_eWeaponType;

            float offset = Res(100.0f + foundAbove * 58.0f);

            DrawWeaponSelectorIcon(
                weaponId,
                centerX,
                centerY - offset,
                ResW(54.0f),
                Res(53.0f),
                sideAlpha
            );

            ++foundAbove;
        }

        int foundBelow = 0;
        int slotBelow = selectedSlot;

        for (int checked = 0;
            checked < WEAPON_SLOT_COUNT - 1 && foundBelow < 2;
            ++checked) {

            slotBelow++;

            if (slotBelow >= WEAPON_SLOT_COUNT)
                slotBelow = 0;

            if (!IsWeaponSlotUsable(player, slotBelow))
                continue;

            int weaponId =
                player->m_aWeapons[slotBelow].m_eWeaponType;

            float offset = Res(100.0f + foundBelow * 58.0f);

            DrawWeaponSelectorIcon(
                weaponId,
                centerX,
                centerY + offset,
                ResW(54.0f),
                Res(53.0f),
                sideAlpha
            );

            ++foundBelow;
        }
    }

    // Konstruktor i inicjalizacja.
    LeedsHUDMain() {
        try {
            unsigned char* checkAddr = (unsigned char*)0x58F441;

            if (checkAddr && *checkAddr != 0x90)
                patch::Nop(0x58F441, 5);
        }
        catch (...) {}

        Events::drawHudEvent += [] {
            LeedsHUDMain::DrawBars();
        };

        Events::initRwEvent += [] {
            std::string asiPath = GetAsiFolder();
            std::string iniPath = asiPath + "LeedsHUD.ini";
            std::string modPath = asiPath + "LeedsHUD\\";

            bMoneyPadZeros =
                GetPrivateProfileIntA(
                    "Settings",
                    "MoneyPadZeros",
                    1,
                    iniPath.c_str()
                ) != 0;

            HudAlpha = GetPrivateProfileIntA(
                "Settings",
                "HUDAlpha",
                225,
                iniPath.c_str()
            );

            HudAlpha = std::clamp(HudAlpha, 0, 255);

            char modeBuffer[32];

            GetPrivateProfileStringA(
                "Settings",
                "Mode",
                "lcs",
                modeBuffer,
                sizeof(modeBuffer),
                iniPath.c_str()
            );

            sHudMode = modeBuffer;

            std::transform(
                sHudMode.begin(),
                sHudMode.end(),
                sHudMode.begin(),
                ::tolower
            );

            outlineR = GetPrivateProfileIntA("Colors", "OutlineR", 255, iniPath.c_str());
            outlineG = GetPrivateProfileIntA("Colors", "OutlineG", 255, iniPath.c_str());
            outlineB = GetPrivateProfileIntA("Colors", "OutlineB", 255, iniPath.c_str());

            healthR = GetPrivateProfileIntA("Colors", "HealthR", 255, iniPath.c_str());
            healthG = GetPrivateProfileIntA("Colors", "HealthG", 255, iniPath.c_str());
            healthB = GetPrivateProfileIntA("Colors", "HealthB", 255, iniPath.c_str());

            armorR = GetPrivateProfileIntA("Colors", "ArmorR", 255, iniPath.c_str());
            armorG = GetPrivateProfileIntA("Colors", "ArmorG", 255, iniPath.c_str());
            armorB = GetPrivateProfileIntA("Colors", "ArmorB", 255, iniPath.c_str());

            breathR = GetPrivateProfileIntA("Colors", "BreathR", 0, iniPath.c_str());
            breathG = GetPrivateProfileIntA("Colors", "BreathG", 100, iniPath.c_str());
            breathB = GetPrivateProfileIntA("Colors", "BreathB", 255, iniPath.c_str());

            breathBgR = GetPrivateProfileIntA("Colors", "BreathBgR", 0, iniPath.c_str());
            breathBgG = GetPrivateProfileIntA("Colors", "BreathBgG", 20, iniPath.c_str());
            breathBgB = GetPrivateProfileIntA("Colors", "BreathBgB", 60, iniPath.c_str());

            plusHealthR = GetPrivateProfileIntA("Colors", "PlusHealthR", 255, iniPath.c_str());
            plusHealthG = GetPrivateProfileIntA("Colors", "PlusHealthG", 255, iniPath.c_str());
            plusHealthB = GetPrivateProfileIntA("Colors", "PlusHealthB", 255, iniPath.c_str());

            plusArmorR = GetPrivateProfileIntA("Colors", "PlusArmorR", 255, iniPath.c_str());
            plusArmorG = GetPrivateProfileIntA("Colors", "PlusArmorG", 255, iniPath.c_str());
            plusArmorB = GetPrivateProfileIntA("Colors", "PlusArmorB", 255, iniPath.c_str());

            int txdSlot = CTxdStore::AddTxdSlot("leedshud");

            if (CTxdStore::LoadTxd(
                txdSlot,
                (modPath + "LeedsHUD.TXD").c_str())) {

                CTxdStore::AddRef(txdSlot);
                CTxdStore::PushCurrentTxd();
                CTxdStore::SetCurrentTxd(txdSlot);

                if (sHudMode == "lcs") {
                    outlineSprite.SetTexture((char*)"lcs_bar_outline");
                    armorBarSprite.SetTexture((char*)"lcs_bar_inside1");
                    healthBarSprite.SetTexture((char*)"lcs_bar_inside2");
                    ammoFontSprite.SetTexture((char*)"lcs_ammotext");
                }
                else {
                    outlineSprite.SetTexture((char*)"outline_frame");
                    armorBarSprite.SetTexture((char*)"bar_inside1");
                    healthBarSprite.SetTexture((char*)"bar_inside2");
                    ammoFontSprite.SetTexture((char*)"ammotext");
                }

                hudNumbersSprite.SetTexture((char*)"hudnumbers");
                plusSprite.SetTexture((char*)"plus");

                CTxdStore::PopCurrentTxd();
            }

            int wepSlot = CTxdStore::AddTxdSlot("leedshud_wep");

            if (CTxdStore::LoadTxd(
                wepSlot,
                (modPath + "LeedsHUDweapons.txd").c_str())) {

                CTxdStore::AddRef(wepSlot);
                CTxdStore::PushCurrentTxd();
                CTxdStore::SetCurrentTxd(wepSlot);

                for (int i = 0; i < 47; i++) {
                    if (strcmp(weaponTextureNames[i], "null") != 0)
                        weaponSprites[i].SetTexture((char*)weaponTextureNames[i]);
                }

                CTxdStore::PopCurrentTxd();
            }

            // INI znajduje się obok pliku ASI.
            // Tekstury ManhuntHUD znajdują się w podfolderze LeedsHUD.
            LeedsHUDManStatus::LoadConfig(iniPath.c_str());
            LeedsHUDManStatus::Load(modPath);

            bLoaded = true;
        };

        Events::shutdownRwEvent += [] {
            LeedsHUDManStatus::Shutdown();
        };

        Events::gameProcessEvent += [] {
            static bool bLastMenuActive = false;

            bool bMenuActive = FrontEndMenuManager.m_bMenuActive;

            if (bMenuActive && !bLastMenuActive)
                *(unsigned char*)0xBA6769 = bMenuSettingMemory ? 1 : 0;

            if (bMenuActive)
                bMenuSettingMemory = (*(unsigned char*)0xBA6769 != 0);
            else if (bMenuSettingMemory)
                *(unsigned char*)0xBA6769 = 0;

            bLastMenuActive = bMenuActive;

            g_bFinalHudStatus = bHudEnabledInIni && bMenuSettingMemory;
        };
    }

    // Rysowanie amunicji.
    static void DrawAmmoFontString(
        const std::string& text,
        float anchorX,
        float startY,
        float charWidth,
        float charHeight,
        float spacing = 0.85f)
    {
        if (!ammoFontSprite.m_pTexture)
            return;

        CPlayerPed* pPlayer = FindPlayerPed(-1);

        if (pPlayer && pPlayer->m_pPlayerData) {
            int activeSlot = pPlayer->m_nSelectedWepSlot;

            if (activeSlot >= 0 && activeSlot < 13) {
                int activeWeapon =
                    pPlayer->m_aWeapons[activeSlot].m_eWeaponType;

                if (activeWeapon == 40)
                    return;
            }
        }

        const float texW = 256.0f;
        const float texH = 64.0f;
        const float charW = 16.0f;
        const float charH = 21.0f;

        float totalWidth = 0.0f;

        for (char c : text)
            totalWidth += charWidth * spacing;

        float startX = anchorX - (totalWidth * 0.5f);

        float resScale =
            (float)RsGlobal.maximumHeight / 1080.0f;

        const float outlineRadius = 2.0f * resScale;

        CRGBA outlineColor(0, 0, 0, 255);
        CRGBA textColor(255, 255, 255, 255);

        const struct {
            float x, y;
        } radialOffsets[8] = {
            { 0.0f, -1.0f },
            { 0.707f, -0.707f },
            { 1.0f, 0.0f },
            { 0.707f, 0.707f },
            { 0.0f, 1.0f },
            { -0.707f, 0.707f },
            { -1.0f, 0.0f },
            { -0.707f, -0.707f }
        };

        float currentX = startX;

        for (char c : text) {
            int index = -1;

            if (c >= '0' && c <= '9')
                index = c - '0';
            else if (c == '-')
                index = 10;

            if (index != -1) {
                const float bleedPadding = 0.5f;

                float u1 = ((index * charW) + bleedPadding) / texW;
                float v1 = 0.0f;
                float u2 = (((index + 1) * charW) - bleedPadding) / texW;
                float v2 = charH / texH;

                for (int i = 0; i < 8; ++i) {
                    float offX = radialOffsets[i].x * outlineRadius;
                    float offY = radialOffsets[i].y * outlineRadius;

                    ammoFontSprite.Draw(
                        CRect(
                            currentX + offX,
                            startY + offY,
                            currentX + offX + charWidth,
                            startY + offY + charHeight
                        ),
                        outlineColor,
                        u1, v1, u2, v1, u1, v2, u2, v2
                    );
                }
            }

            currentX += charWidth * spacing;
        }

        currentX = startX;

        for (char c : text) {
            int index = -1;

            if (c >= '0' && c <= '9')
                index = c - '0';
            else if (c == '-')
                index = 10;

            if (index != -1) {
                const float bleedPadding = 0.5f;

                float u1 = ((index * charW) + bleedPadding) / texW;
                float v1 = 0.0f;
                float u2 = (((index + 1) * charW) - bleedPadding) / texW;
                float v2 = charH / texH;

                ammoFontSprite.Draw(
                    CRect(
                        currentX,
                        startY,
                        currentX + charWidth,
                        startY + charHeight
                    ),
                    textColor,
                    u1, v1, u2, v1, u1, v2, u2, v2
                );
            }

            currentX += charWidth * spacing;
        }
    }

    // ---------------------------------------------------------
    // Główne rysowanie HUD
    // ---------------------------------------------------------

    static void DrawBars() {
        g_LeedsRadarIndicatorsEnabled = false;
        g_LeedsRadarShowArmor = false;
        g_LeedsRadarShowBreath = false;

        g_LeedsRadarShowHealthPlus = false;
        g_LeedsRadarShowArmorPlus = false;

        if (!g_bFinalHudStatus || FrontEndMenuManager.m_bMenuActive)
            return;

        if (TheCamera.m_bWideScreenOn)
            return;

        if (!CTheScripts::bDisplayHud)
            return;

        if (CCutsceneMgr::ms_cutsceneProcessing)
            return;

        CPlayerPed* player = FindPlayerPed();

        if (!player || !player->m_pPlayerData)
            return;

        // -----------------------------------------------------
        // Tlen
        // -----------------------------------------------------

        float currentBreath = player->m_pPlayerData->m_fBreath;

        float maxBreath =
            1000.0f + (CStats::GetStatValue(22) * 1.5f);

        if (maxBreath <= 0.0f)
            maxBreath = 1000.0f;

        float breathLevelPerc = std::clamp(
            currentBreath / maxBreath,
            0.0f,
            1.0f
        );

        float breathUpgradeProgress = std::clamp(
            (maxBreath - 1000.0f) / 1500.0f,
            0.0f,
            1.0f
        );

        float breathFillScale =
            0.5f + (breathUpgradeProgress * 0.5f);

        float breathPerc = std::clamp(
            breathLevelPerc * breathFillScale,
            0.0f,
            1.0f
        );

        if (lastBreath < 0.0f)
            lastBreath = currentBreath;

        if (breathGracePeriod > 0.0f) {
            breathGracePeriod -= CTimer::ms_fTimeStep;
            lastBreath = currentBreath;
        }
        else if (
            currentBreath < (lastBreath - 0.1f) &&
            breathLevelPerc < 0.98f
        ) {
            breathVisibilityTimer = 150.0f;
        }

        if (breathVisibilityTimer > 0.0f)
            breathVisibilityTimer -= CTimer::ms_fTimeStep;

        lastBreath = currentBreath;

        float fadeSpeed = 0.04f * CTimer::ms_fTimeStep;

        if (breathVisibilityTimer > 0.0f &&
            breathLevelPerc < 0.99f) {
            colorFadeFactor =
                std::min(1.0f, colorFadeFactor + fadeSpeed);
        }
        else {
            colorFadeFactor =
                std::max(0.0f, colorFadeFactor - fadeSpeed);
        }

        LeedsHUDMain::DrawWeaponSelector(player);
        LeedsHUDManStatus::Draw(player);

        // -----------------------------------------------------
        // Zdrowie
        // Bazowe HP: 100
        // Maksymalne HP: 176
        // -----------------------------------------------------

        short playerFocus = CWorld::PlayerInFocus;

        float maxHealth =
            static_cast<float>(
                CWorld::Players[playerFocus].m_nMaxHealth
            );

        if (maxHealth <= 0.0f)
            maxHealth = 100.0f;

        float healthUpgradeProgress = std::clamp(
            (maxHealth - 100.0f) / 76.0f,
            0.0f,
            1.0f
        );

        float healthFillScale =
            0.5f + (healthUpgradeProgress * 0.5f);

        float healthPerc = std::clamp(
            (player->m_fHealth / maxHealth) * healthFillScale,
            0.0f,
            1.0f
        );

        // -----------------------------------------------------
        // Pancerz
        // Bazowy pancerz: 100
        // Maksymalny pancerz: 150
        // -----------------------------------------------------

        float maxArmor =
            static_cast<float>(
                CWorld::Players[playerFocus].m_nMaxArmour
            );

        if (maxArmor <= 0.0f)
            maxArmor = 100.0f;

        maxArmor = std::clamp(maxArmor, 100.0f, 150.0f);

        float armorUpgradeProgress = std::clamp(
            (maxArmor - 100.0f) / 50.0f,
            0.0f,
            1.0f
        );

        float armorFillScale =
            0.5f + (armorUpgradeProgress * 0.5f);

        float armorPerc = std::clamp(
            (player->m_fArmour / maxArmor) * armorFillScale,
            0.0f,
            1.0f
        );

        // -----------------------------------------------------
        // Przekazywanie aktualnych wartości do Radar.cpp
        // -----------------------------------------------------

        g_LeedsRadarHealthPercent = healthPerc;
        g_LeedsRadarArmorPercent = armorPerc;
        g_LeedsRadarBreathPercent = breathPerc;

        g_LeedsRadarIndicatorAlpha =
            static_cast<unsigned char>(HudAlpha);

        bool bDrawBreath = (colorFadeFactor > 0.01f);

        g_LeedsRadarShowBreath =
            bDrawBreath && breathLevelPerc < 0.99f;

        g_LeedsRadarShowArmor =
            !g_LeedsRadarShowBreath &&
            player->m_fArmour > 0.0f;

        // -----------------------------------------------------
        // Plus zdrowia
        // Ulepszone zdrowie i minimum 51% zapełnienia paska.
        // -----------------------------------------------------

        g_LeedsRadarShowHealthPlus =
            CWorld::Players[playerFocus].m_nMaxHealth >= 101 &&
            CWorld::Players[playerFocus].m_nMaxHealth <= 176 &&
            healthPerc >= 0.51f;

        // -----------------------------------------------------
        // Plus pancerza
        // Ulepszony pancerz i minimum 51% zapełnienia paska.
        // Nie pokazuj go podczas wyświetlania tlenu.
        // -----------------------------------------------------

        g_LeedsRadarShowArmorPlus =
            CWorld::Players[playerFocus].m_nMaxArmour >= 101 &&
            CWorld::Players[playerFocus].m_nMaxArmour <= 150 &&
            armorPerc >= 0.51f &&
            !g_LeedsRadarShowBreath;

        g_LeedsRadarIndicatorsEnabled = true;

        // -----------------------------------------------------
        // Amunicja — środek aktualnie wybranej ikony broni
        // -----------------------------------------------------

        int activeSlot = player->m_nSelectedWepSlot;

        if (activeSlot >= 0 && activeSlot < 13) {
            CWeapon& activeWep = player->m_aWeapons[activeSlot];

            if (activeSlot > 1 &&
                activeSlot != 10 &&
                activeWep.m_eWeaponType > 1 &&
                activeWep.m_eWeaponType < 44 &&
                activeWep.m_eWeaponType != 21 &&
                activeWep.m_eWeaponType != 14) {

                CWeaponInfo* info =
                    CWeaponInfo::GetWeaponInfo(
                        activeWep.m_eWeaponType,
                        player->GetWeaponSkill()
                    );

                if (activeWep.m_nAmmoTotal < 20000) {
                    char ammoStr[64];

                    if (activeWep.m_eWeaponType == 43) {
                        snprintf(
                            ammoStr,
                            sizeof(ammoStr),
                            "%d",
                            activeWep.m_nAmmoTotal
                        );
                    }
                    else if (info && info->m_nAmmoClip > 1) {
                        snprintf(
                            ammoStr,
                            sizeof(ammoStr),
                            "%d-%d",
                            activeWep.m_nAmmoTotal -
                                activeWep.m_nAmmoInClip,
                            activeWep.m_nAmmoInClip
                        );
                    }
                    else {
                        snprintf(
                            ammoStr,
                            sizeof(ammoStr),
                            "%d",
                            activeWep.m_nAmmoTotal
                        );
                    }

                    float weaponCenterX =
                        ResW(valWepX) +
                        ResW(valWepW) * 0.5f;

                    float weaponCenterY =
                        static_cast<float>(RsGlobal.maximumHeight) -
                        Res(valWepY) -
                        Res(valWepH) * 0.5f;

                    float ammoStartY =
                        weaponCenterY +
                        Res(-12.0f);

                    float ammoCenterX =
                        weaponCenterX -
                        ResW(19.0f);

                    LeedsHUDMain::DrawAmmoFontString(
                        ammoStr,
                        ammoCenterX,
                        ammoStartY,
                        ResW(16.0f * valAmW),
                        Res(21.0f * valAmH),
                        1.05f
                    );
                }
            }
        }
    }
};

// -------------------------------------------------------------
// Definicje statycznych elementów LeedsHUDMain
// -------------------------------------------------------------

CSprite2d LeedsHUDMain::outlineSprite;
CSprite2d LeedsHUDMain::ammoFontSprite;
CSprite2d LeedsHUDMain::weaponSprites[47];
CSprite2d LeedsHUDMain::hudNumbersSprite;
CSprite2d LeedsHUDMain::healthBarSprite;
CSprite2d LeedsHUDMain::armorBarSprite;
CSprite2d LeedsHUDMain::plusSprite;

bool LeedsHUDMain::bLoaded = false;

int LeedsHUDMain::s_LastWeaponSlot = -1;
int LeedsHUDMain::s_LastWeaponType = -1;

unsigned int LeedsHUDMain::s_WeaponSelectorChangeTime = 0;
bool LeedsHUDMain::s_WeaponSelectorInitialized = false;

// -------------------------------------------------------------
// Funkcja udostępniająca istniejący sprite plus dla Radar.cpp
// -------------------------------------------------------------

void LeedsHUDDrawPlus(
    const CRect& drawArea,
    int red,
    int green,
    int blue,
    unsigned char alpha)
{
    if (!LeedsHUDMain::plusSprite.m_pTexture || alpha == 0)
        return;

    LeedsHUDMain::plusSprite.Draw(
        drawArea,
        CRGBA(
            static_cast<unsigned char>(std::clamp(red, 0, 255)),
            static_cast<unsigned char>(std::clamp(green, 0, 255)),
            static_cast<unsigned char>(std::clamp(blue, 0, 255)),
            alpha
        )
    );
}

LeedsHUDMain leedsHudMainInstance;

// -------------------------------------------------------------
// Zegar
// -------------------------------------------------------------

class LeedsHUDClock {
public:
    static float Res(float value) {
        return value * ((float)RsGlobal.maximumHeight / 1080.0f);
    }

    static float ResW(float value) {
        return value * ((float)RsGlobal.maximumHeight / 1080.0f);
    }

    static void Draw() {
        if (CCutsceneMgr::ms_cutsceneProcessing ||
            TheCamera.m_bWideScreenOn ||
            !g_bFinalHudStatus)
            return;

        if (!CTheScripts::bDisplayHud)
            return;

        char timeStr[64];

        snprintf(
            timeStr,
            sizeof(timeStr),
            "%02d:%02d",
            CClock::ms_nGameClockHours,
            CClock::ms_nGameClockMinutes
        );

        float charWidth = ResW(16.0f * valClW);
        float charHeight = Res(17.0f * valClH);

        float textWidth = charWidth * 4.5f;

        float rightEdge =
            static_cast<float>(RsGlobal.maximumWidth) -
            ResW(valMnX);

        DrawCustomFontString(
            LeedsHUDMain::hudNumbersSprite,
            timeStr,
            rightEdge,
            Res(valClY),
            charWidth,
            charHeight,
            true,
            false,
            1.0f,
            HudAlpha
        );
    }
};

// -------------------------------------------------------------
// Pieniądze
// -------------------------------------------------------------

class LeedsHUDMoney {
public:
    static int m_nPreviousMoney;
    static int m_nDisplayMoney;
    static bool bInitialized;

    static long long m_nMoneyDifference;
    static unsigned int m_nMoneyChangeTime;
    static bool m_bMoneyChangeVisible;

    static constexpr unsigned int MONEY_VISIBLE_TIME = 1500;
    static constexpr unsigned int MONEY_FADE_TIME = 700;

    static float Res(float value) {
        return value * ((float)RsGlobal.maximumHeight / 1080.0f);
    }

    static float ResW(float value) {
        return value * ((float)RsGlobal.maximumHeight / 1080.0f);
    }

    static void Draw() {
        if (!g_bFinalHudStatus)
            return;

        if (!CTheScripts::bDisplayHud ||
            FrontEndMenuManager.m_bMenuActive ||
            TheCamera.m_bWideScreenOn)
            return;

        int currentMoney = *(int*)0xB7CE50;

        if (!bInitialized) {
            m_nPreviousMoney = currentMoney;
            m_nDisplayMoney = currentMoney;
            bInitialized = true;
            return;
        }

        if (currentMoney != m_nPreviousMoney) {
            m_nMoneyDifference =
                static_cast<long long>(currentMoney) -
                static_cast<long long>(m_nPreviousMoney);

            m_nPreviousMoney = currentMoney;
            m_nMoneyChangeTime = CTimer::m_snTimeInMilliseconds;
            m_bMoneyChangeVisible = true;
        }

        if (m_nDisplayMoney != currentMoney) {
            long long diff =
                static_cast<long long>(currentMoney) -
                static_cast<long long>(m_nDisplayMoney);

            long long step = std::llabs(diff) / 10 + 1;

            if (std::llabs(diff) <= step)
                m_nDisplayMoney = currentMoney;
            else if (diff > 0)
                m_nDisplayMoney += static_cast<int>(step);
            else
                m_nDisplayMoney -= static_cast<int>(step);
        }

        char str[64];

        bool bNegative = (m_nDisplayMoney < 0);

        long long absMoney = bNegative
            ? -static_cast<long long>(m_nDisplayMoney)
            : static_cast<long long>(m_nDisplayMoney);

        if (bMoneyPadZeros) {
            snprintf(
                str,
                sizeof(str),
                bNegative ? "-$%08lld" : "$%08lld",
                absMoney
            );
        }
        else {
            snprintf(
                str,
                sizeof(str),
                bNegative ? "-$%lld" : "$%lld",
                absMoney
            );
        }

        CRGBA tint = bNegative
            ? CRGBA(150, 0, 0, 255)
            : CRGBA(32, 110, 32, 255);

        DrawCustomFontString(
            LeedsHUDMain::hudNumbersSprite,
            str,
            (float)RsGlobal.maximumWidth - ResW(valMnX),
            Res(valMnY),
            ResW(16.0f * valMnW),
            Res(17.0f * valMnH),
            true,
            true,
            1.0f,
            HudAlpha,
            tint
        );

        if (!m_bMoneyChangeVisible || m_nMoneyDifference == 0)
            return;

        unsigned int elapsed =
            CTimer::m_snTimeInMilliseconds - m_nMoneyChangeTime;

        unsigned int totalDuration =
            MONEY_VISIBLE_TIME + MONEY_FADE_TIME;

        if (elapsed >= totalDuration) {
            m_bMoneyChangeVisible = false;
            return;
        }

        float fade = 1.0f;

        if (elapsed > MONEY_VISIBLE_TIME) {
            float fadeProgress =
                static_cast<float>(elapsed - MONEY_VISIBLE_TIME) /
                static_cast<float>(MONEY_FADE_TIME);

            fade = 1.0f - std::clamp(fadeProgress, 0.0f, 1.0f);
        }

        bool bDifferenceNegative = (m_nMoneyDifference < 0);

        long long absDifference = bDifferenceNegative
            ? -m_nMoneyDifference
            : m_nMoneyDifference;

        char differenceStr[64];

        snprintf(
            differenceStr,
            sizeof(differenceStr),
            bDifferenceNegative ? "-$%lld" : "$%lld",
            absDifference
        );

        CRGBA differenceTint = bDifferenceNegative
            ? CRGBA(220, 35, 35, 255)
            : CRGBA(40, 210, 75, 255);

        unsigned char differenceAlpha =
            static_cast<unsigned char>(
                std::clamp(
                    static_cast<float>(HudAlpha) * fade,
                    0.0f,
                    255.0f
                )
            );

        DrawCustomFontString(
            LeedsHUDMain::hudNumbersSprite,
            differenceStr,
            (float)RsGlobal.maximumWidth - ResW(valMnX),
            Res(valMnY + 37.0f),
            ResW(16.0f * valMnW),
            Res(17.0f * valMnH),
            true,
            true,
            1.0f,
            differenceAlpha,
            differenceTint
        );
    }
};

int LeedsHUDMoney::m_nPreviousMoney = 0;
int LeedsHUDMoney::m_nDisplayMoney = 0;
bool LeedsHUDMoney::bInitialized = false;

long long LeedsHUDMoney::m_nMoneyDifference = 0;
unsigned int LeedsHUDMoney::m_nMoneyChangeTime = 0;
bool LeedsHUDMoney::m_bMoneyChangeVisible = false;

// -------------------------------------------------------------
// Poziom poszukiwania
// -------------------------------------------------------------

class LeedsHUDWanted {
private:
    static inline int m_nLastWantedLevel = -1;
    static inline int m_nLastMinWantedLevel = -1;
    static inline unsigned int m_nFlashEndTime = 0;

    static constexpr unsigned int FLASH_CYCLE_DURATION = 400;
    static constexpr unsigned int TOTAL_FLASH_DURATION =
        FLASH_CYCLE_DURATION * 6;

    static constexpr unsigned int UNSUSPEND_FLASH_DURATION =
        FLASH_CYCLE_DURATION * 7;

    static constexpr unsigned int SUSPENDED_FLASH_CYCLE = 400;

public:
    static float Res(float value) {
        return value * ((float)RsGlobal.maximumHeight / 1080.0f);
    }

    static float ResW(float value) {
        return value * ((float)RsGlobal.maximumHeight / 1080.0f);
    }

    static void Draw() {
        if (!g_bFinalHudStatus)
            return;

        if (!CTheScripts::bDisplayHud ||
            FrontEndMenuManager.m_bMenuActive ||
            TheCamera.m_bWideScreenOn)
            return;

        CPlayerPed* player = FindPlayerPed();

        if (!player ||
            !player->m_pPlayerData ||
            !player->m_pPlayerData->m_pWanted)
            return;

        CWanted* pWanted = player->m_pPlayerData->m_pWanted;

        int wantedLevel = pWanted->m_nWantedLevel;
        int minWantedLevel = pWanted->m_nWantedLevelBeforeParole;

        bool isSuspended = minWantedLevel > wantedLevel;

        bool wasSuspendedMerge =
            (wantedLevel > m_nLastWantedLevel) &&
            (wantedLevel <= m_nLastMinWantedLevel);

        if (m_nLastWantedLevel != -1 &&
            wantedLevel != m_nLastWantedLevel &&
            !wasSuspendedMerge) {

            m_nFlashEndTime =
                CTimer::m_snTimeInMilliseconds +
                TOTAL_FLASH_DURATION;
        }

        if (wasSuspendedMerge) {
            m_nFlashEndTime =
                CTimer::m_snTimeInMilliseconds +
                UNSUSPEND_FLASH_DURATION;
        }

        m_nLastWantedLevel = wantedLevel;
        m_nLastMinWantedLevel = minWantedLevel;

        if (wantedLevel == 0 && minWantedLevel == 0)
            return;

        unsigned char renderAlpha = 225;

        unsigned int currentTime =
            CTimer::m_snTimeInMilliseconds;

        if (currentTime < m_nFlashEndTime) {
            unsigned int timePassed =
                TOTAL_FLASH_DURATION -
                (m_nFlashEndTime - currentTime);

            if ((timePassed / (FLASH_CYCLE_DURATION / 2)) % 2 == 1)
                renderAlpha = 0;
        }

        if (renderAlpha > 0 && wantedLevel > 0) {
            std::string starString(wantedLevel, '*');

            unsigned char combinedAlpha =
                static_cast<unsigned char>(
                    (HudAlpha / 255.0f) * renderAlpha
                );

            DrawCustomFontString(
                LeedsHUDMain::hudNumbersSprite,
                starString,
                (float)RsGlobal.maximumWidth - ResW(valStX),
                Res(valStY),
                ResW(valStW),
                Res(valStH * 5.0f),
                true,
                false,
                0.8f,
                combinedAlpha
            );
        }

        if (isSuspended) {
            bool visible =
                (currentTime / SUSPENDED_FLASH_CYCLE) % 2 == 0;

            if (visible) {
                std::string suspendedString(minWantedLevel, '*');

                unsigned char suspendedAlpha =
                    static_cast<unsigned char>(
                        (HudAlpha / 255.0f) * 225
                    );

                DrawCustomFontString(
                    LeedsHUDMain::hudNumbersSprite,
                    suspendedString,
                    (float)RsGlobal.maximumWidth - ResW(valStX),
                    Res(valStY),
                    ResW(valStW),
                    Res(valStH * 5.0f),
                    true,
                    false,
                    0.8f,
                    suspendedAlpha,
                    CRGBA(100, 200, 255, 255)
                );
            }
        }
    }
};

// -------------------------------------------------------------
// Rejestracja zdarzeń HUD
// -------------------------------------------------------------

class LeedsHUDClockPlugin {
public:
    LeedsHUDClockPlugin() {
        Events::drawHudEvent += [] {
            LeedsHUDClock::Draw();
        };
    }
} leedsHudClockPlugin;

class LeedsHUDWantedPlugin {
public:
    LeedsHUDWantedPlugin() {
        Events::drawHudEvent += [] {
            if (FrontEndMenuManager.m_bMenuActive ||
                TheCamera.m_bWideScreenOn)
                return;

            LeedsHUDWanted::Draw();
        };
    }
} leedsHUDWantedPlugin;

class LeedsHUDMoneyPlugin {
public:
    LeedsHUDMoneyPlugin() {
        Events::drawHudEvent += [] {
            if (FrontEndMenuManager.m_bMenuActive ||
                TheCamera.m_bWideScreenOn)
                return;

            LeedsHUDMoney::Draw();
        };
    }
} leedsHudMoneyPlugin;