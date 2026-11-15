
#include "plugin.h"
#include "RenderWare.h"
#include "CTxdStore.h"
#include "Patch.h"
#include "CCamera.h"
#include "CHud.h"
#include "CSprite2d.h"
#include "CSprite.h"
#include "CWeaponInfo.h"
#include "CTheScripts.h"
#include "CCutsceneMgr.h"
#include "CMenuManager.h"
#include "CWeapon.h"
#include "CPools.h"

#include <windows.h>
#include <string>
#include <cstdint>

using namespace plugin;

// -------------------------------------------------------------
// Zmienne globalne
// -------------------------------------------------------------

static bool g_bSwapDone = false;
static bool g_bEnableRedLockOn = true;

static int g_CrosshairTxdSlot = -1;

// -------------------------------------------------------------
// Ścieżka do LeedsHUD.ini
// -------------------------------------------------------------

static std::string GetAsiDir() {
    char buf[MAX_PATH] = {};
    HMODULE hMod = nullptr;

    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetAsiDir),
        &hMod
    );

    GetModuleFileNameA(hMod, buf, MAX_PATH);

    std::string path(buf);

    size_t lastSlash = path.find_last_of("\\/");

    if (lastSlash == std::string::npos)
        return "";

    return path.substr(0, lastSlash + 1);
}

// -------------------------------------------------------------
// Konfiguracja
// -------------------------------------------------------------

static void LoadConfig() {
    std::string iniPath = GetAsiDir() + "LeedsHUD.ini";

    g_bEnableRedLockOn =
        GetPrivateProfileIntA(
            "Settings",
            "CrosshairRedLockOn",
            1,
            iniPath.c_str()
        ) != 0;
}

// -------------------------------------------------------------
// Rodzaje celowników
// -------------------------------------------------------------

#define CROSSHAIRS_TOTALSPRITES 4

enum eCrosshairSprites {
    CROSSHAIR_M16 = 0,
    CROSSHAIR_ROCKET = 1,
    CROSSHAIR_SNIPER = 2,
    CROSSHAIR_VIEWFINDER = 3
};

static CSprite2d CrosshairSprites[CROSSHAIRS_TOTALSPRITES];

static bool ms_bSpritesLoaded = false;

static char* CrosshairsNames[CROSSHAIRS_TOTALSPRITES] = {
    (char*)"sitem16",
    (char*)"siterocket",
    (char*)"scope",
    (char*)"viewfinder"
};

// -------------------------------------------------------------
// Ładowanie i zwalnianie tekstur
// -------------------------------------------------------------

class CHudNew {
public:

    static void Initialise() {
        if (ms_bSpritesLoaded)
            return;

        if (g_CrosshairTxdSlot != -1)
            return;

        g_CrosshairTxdSlot =
            CTxdStore::AddTxdSlot("crosshair");

        if (g_CrosshairTxdSlot == -1)
            return;

        bool loaded = CTxdStore::LoadTxd(
            g_CrosshairTxdSlot,
            PLUGIN_PATH((char*)"LeedsHUD\\LeedsHUD.txd")
        );

        if (!loaded) {
            CTxdStore::RemoveTxdSlot(g_CrosshairTxdSlot);
            g_CrosshairTxdSlot = -1;
            return;
        }

        CTxdStore::AddRef(g_CrosshairTxdSlot);

        CTxdStore::PushCurrentTxd();

        CTxdStore::SetCurrentTxd(g_CrosshairTxdSlot);

        bool allTexturesLoaded = true;

        for (int i = 0; i < CROSSHAIRS_TOTALSPRITES; i++) {
            CrosshairSprites[i].SetTexture(
                CrosshairsNames[i]
            );

            if (!CrosshairSprites[i].m_pTexture)
                allTexturesLoaded = false;
        }

        CTxdStore::PopCurrentTxd();

        ms_bSpritesLoaded = allTexturesLoaded;

        if (!ms_bSpritesLoaded) {
            for (int i = 0; i < CROSSHAIRS_TOTALSPRITES; i++) {
                CrosshairSprites[i].Delete();
            }

            CTxdStore::RemoveTxdSlot(g_CrosshairTxdSlot);
            g_CrosshairTxdSlot = -1;
        }
    }

    static void Shutdown() {
        if (ms_bSpritesLoaded || g_CrosshairTxdSlot != -1) {
            for (int i = 0; i < CROSSHAIRS_TOTALSPRITES; i++) {
                CrosshairSprites[i].Delete();
            }

            if (g_CrosshairTxdSlot != -1) {
                CTxdStore::RemoveTxdSlot(g_CrosshairTxdSlot);
                g_CrosshairTxdSlot = -1;
            }

            ms_bSpritesLoaded = false;
        }
    }
};

// -------------------------------------------------------------
// Sprawdzanie poprawności celu
// -------------------------------------------------------------

static bool IsValidPedEntity(CEntity* ent) {
    if (!ent)
        return false;

    uintptr_t addr = reinterpret_cast<uintptr_t>(ent);

    if (addr < 0x10000 || addr >= 0x7FFFFFFF)
        return false;

    if (!CPools::ms_pPedPool)
        return false;

    CPed* ped = reinterpret_cast<CPed*>(ent);

    if (!CPools::ms_pPedPool->IsObjectValid(ped))
        return false;

    if (ent->m_nType != 1)
        return false;

    return true;
}

// -------------------------------------------------------------
// Sprawdzanie, czy gracz celuje w peda
// -------------------------------------------------------------

static bool IsAimingAtPed(CPlayerPed* player) {
    if (!player || !g_bEnableRedLockOn)
        return false;

    if (player->m_pPlayerTargettedPed)
        return true;

    if (player->m_pPlayerData &&
        player->m_pPlayerData->m_bHaveTargetSelected) {

        return true;
    }

    CEntity* crosshairEnt =
        *reinterpret_cast<CEntity**>(0xB79358);

    return IsValidPedEntity(crosshairEnt);
}

// -------------------------------------------------------------
// Rysowanie celownika
// -------------------------------------------------------------

void leedsCrosshair() {
    if (!ms_bSpritesLoaded)
        return;

    CPlayerPed* player = FindPlayerPed(0);

    if (!player || !player->m_pPlayerData)
        return;

    if (FrontEndMenuManager.m_bMenuActive)
        return;

    if (!CHud::m_Wants_To_Draw_Hud)
        return;

    if (TheCamera.m_bWideScreenOn)
        return;

    if (CCutsceneMgr::ms_cutsceneProcessing)
        return;

    if (!CTheScripts::bDisplayHud)
        return;

    eCamMode Mode =
        TheCamera.m_aCams[
            TheCamera.m_nActiveCam
        ].m_nMode;

    // ---------------------------------------------------------
    // Aparat fotograficzny
    // ---------------------------------------------------------

    if (Mode == MODE_CAMERA) {
        float size = SCREEN_COORD(512.0f);

        CRect rect(
            (SCREEN_WIDTH / 2.0f) - size,
            (SCREEN_HEIGHT / 2.0f) - size,
            (SCREEN_WIDTH / 2.0f) + size,
            (SCREEN_HEIGHT / 2.0f) + size
        );

        CrosshairSprites[CROSSHAIR_VIEWFINDER].Draw(
            rect,
            CRGBA(255, 255, 255, 255)
        );

        return;
    }

    // ---------------------------------------------------------
    // Celownik M16 - tryb specjalny
    // ---------------------------------------------------------

    if ((int)Mode == 45) {
        float x = SCREEN_WIDTH * 0.5f;
        float y = SCREEN_HEIGHT * 0.5f;

        float fixedSize = SCREEN_COORD(40.0f);

        CRect rect(
            x - fixedSize,
            y - fixedSize,
            x + fixedSize,
            y + fixedSize
        );

        CrosshairSprites[CROSSHAIR_M16].Draw(
            rect,
            CRGBA(255, 255, 255, 255)
        );

        return;
    }

    // ---------------------------------------------------------
    // Celownik rakietowy w pojeździe
    // ---------------------------------------------------------

    if (Mode == MODE_1STPERSON) {
        CVehicle* playerVehicle =
            FindPlayerVehicle(0, false);

        if (playerVehicle) {
            unsigned int model =
                playerVehicle->m_nModelIndex;

            if (model == 476 ||
                model == 520 ||
                model == 425) {

                float x = SCREEN_WIDTH * 0.5f;
                float y = SCREEN_HEIGHT * 0.5f;

                float rocketSize = SCREEN_COORD(80.0f);

                CRect rect(
                    x - rocketSize,
                    y - rocketSize,
                    x + rocketSize,
                    y + rocketSize
                );

                CrosshairSprites[CROSSHAIR_ROCKET].Draw(
                    rect,
                    CRGBA(255, 255, 255, 255)
                );

                return;
            }
        }
    }

    // ---------------------------------------------------------
    // Aktualna broń gracza
    // ---------------------------------------------------------

    int slot = player->m_nSelectedWepSlot;

    if (slot < 0 || slot >= 13)
        return;

    CWeaponInfo* info =
        CWeaponInfo::GetWeaponInfo(
            player->m_aWeapons[slot].m_eWeaponType,
            player->GetWeaponSkill()
        );

    if (!info)
        return;

    // ---------------------------------------------------------
    // Tryby celowania
    // ---------------------------------------------------------

    if (
        Mode == MODE_AIMWEAPON ||
        Mode == MODE_AIMWEAPON_FROMCAR ||
        Mode == MODE_ROCKETLAUNCHER ||
        Mode == MODE_ROCKETLAUNCHER_HS ||
        Mode == MODE_SNIPER ||
        Mode == MODE_AIMWEAPON_ATTACHED
    ) {
        CRect rect;

        // -----------------------------------------------------
        // Karabin snajperski
        // -----------------------------------------------------

        if (info->m_nModelId == 358) {
            float sniperSize = 700.0f;

            rect.left =
                (SCREEN_WIDTH / 2.0f) -
                SCREEN_COORD(sniperSize);

            rect.top =
                (SCREEN_HEIGHT / 2.0f) -
                SCREEN_COORD(sniperSize);

            rect.right =
                (SCREEN_WIDTH / 2.0f) +
                SCREEN_COORD(sniperSize);

            rect.bottom =
                (SCREEN_HEIGHT / 2.0f) +
                SCREEN_COORD(sniperSize);

            CrosshairSprites[CROSSHAIR_SNIPER].Draw(
                rect,
                CRGBA(255, 255, 255, 255)
            );
        }

        // -----------------------------------------------------
        // Wyrzutnie rakiet
        // -----------------------------------------------------

        else if (
            info->m_nModelId == 359 ||
            info->m_nModelId == 360
        ) {
            float rocketSize = 80.0f;

            rect.left =
                (SCREEN_WIDTH / 2.0f) -
                SCREEN_COORD(rocketSize);

            rect.top =
                (SCREEN_HEIGHT / 2.0f) -
                SCREEN_COORD(rocketSize);

            rect.right =
                (SCREEN_WIDTH / 2.0f) +
                SCREEN_COORD(rocketSize);

            rect.bottom =
                (SCREEN_HEIGHT / 2.0f) +
                SCREEN_COORD(rocketSize);

            CrosshairSprites[CROSSHAIR_ROCKET].Draw(
                rect,
                CRGBA(255, 255, 255, 255)
            );
        }

        // -----------------------------------------------------
        // Pozostałe bronie
        // -----------------------------------------------------

        else {
            float x =
                (float)SCREEN_WIDTH *
                CCamera::m_f3rdPersonCHairMultX;

            float y =
                (float)SCREEN_HEIGHT *
                CCamera::m_f3rdPersonCHairMultY;

            bool bIsAimingAtPed =
                IsAimingAtPed(player);

            float fixedSize = SCREEN_COORD(40.0f);

            rect.left = x - fixedSize;
            rect.top = y - fixedSize;
            rect.right = x + fixedSize;
            rect.bottom = y + fixedSize;

            if (bIsAimingAtPed) {
                CrosshairSprites[CROSSHAIR_M16].Draw(
                    rect,
                    CRGBA(191, 0, 0, 255)
                );
            }
            else {
                CrosshairSprites[CROSSHAIR_M16].Draw(
                    rect,
                    CRGBA(255, 255, 255, 255)
                );
            }
        }
    }
}

// -------------------------------------------------------------
// Plugin LeedsHUD Crosshair
// -------------------------------------------------------------

class leedsCrosshairEmpty {
public:

    leedsCrosshairEmpty() {

        // -----------------------------------------------------
        // Inicjalizacja gry
        // -----------------------------------------------------

        Events::initGameEvent += [] {
            patch::PutRetn(0x58E020);

            g_bSwapDone = false;

            LoadConfig();

            CHudNew::Initialise();
        };

        // -----------------------------------------------------
        // Ponowna inicjalizacja gry
        // -----------------------------------------------------

        Events::reInitGameEvent += [] {
            g_bSwapDone = false;

            LoadConfig();

            if (!ms_bSpritesLoaded)
                CHudNew::Initialise();
        };

        // -----------------------------------------------------
        // Rysowanie celownika
        // -----------------------------------------------------

        Events::drawingEvent += [] {
            if (!g_bSwapDone)
                leedsCrosshair();
        };

        // -----------------------------------------------------
        // Zwalnianie tekstur
        // -----------------------------------------------------

        Events::shutdownRwEvent += [] {
            CHudNew::Shutdown();
        };
    }

} LeedsCrosshair;
