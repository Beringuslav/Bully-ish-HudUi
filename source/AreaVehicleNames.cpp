
#include "plugin.h"
#include "CHud.h"
#include "CFont.h"
#include "CVehicle.h"
#include "CText.h"
#include "CModelInfo.h"
#include "CTimer.h"
#include "CCutsceneMgr.h"
#include "CMenuManager.h"
#include "CCamera.h"
#include "CSprite2d.h"

#include <windows.h>
#include <algorithm>
#include <string>
#include <cstdint>
#include <cstring>

using namespace plugin;

extern bool g_LeedsCutsceneNameVisible;

int nPeekKey = 0x54;

#define SCREEN_WIDTH ((float)RsGlobal.maximumWidth)
#define SCREEN_HEIGHT ((float)RsGlobal.maximumHeight)

static unsigned char UI_FONT = FONT_GOTHIC;
static float g_fontScaleModifier = 1.0f;

#define TEXT_SCALE_X (1.0f * ((float)RsGlobal.maximumHeight / 560.0f) * g_fontScaleModifier)
#define TEXT_SCALE_Y (2.0f * ((float)RsGlobal.maximumHeight / 560.0f) * g_fontScaleModifier)

static float Res(float value) {
    return value * ((float)RsGlobal.maximumHeight / 990.0f);
}

// Minimalna struktura danych aktualnej strefy z SilentPatch.
struct LeedsZoneInfo {
    unsigned char GangDensity[10];
    unsigned char DrugDealerCounter;
    CRGBA ZoneColour;
    bool unk1 : 1;
    bool unk2 : 1;
    bool unk3 : 1;
    bool unk4 : 1;
    bool unk5 : 1;
    bool bUseColour : 1;
    bool bInGangWar : 1;
    bool bNoCops : 1;
    unsigned char flags;
};

// Wskaźnik aktualnej strefy dla GTA SA 1.0 US.
// UWAGA: wymaga potwierdzenia sposobu dereferencji adresu.
static LeedsZoneInfo* GetCurrentZoneInfo() {
    static LeedsZoneInfo*** ppZoneInfo =
        reinterpret_cast<LeedsZoneInfo***>(0x58ADB1);

    if (!ppZoneInfo || !*ppZoneInfo || !**ppZoneInfo) {
        return nullptr;
    }

    return **ppZoneInfo;
}

// Kolor bazowy nazwy dzielnicy z uwzględnieniem koloru strefy.
static CRGBA GetAreaNameColour(int alpha) {
    LeedsZoneInfo* zone = GetCurrentZoneInfo();

    if (!zone) {
        return CRGBA(255, 255, 255, alpha);
    }

    const float intensity = std::min(
        static_cast<float>(zone->ZoneColour.a) / 120.0f,
        1.0f
    );

    if (intensity <= 0.0f) {
        return CRGBA(255, 255, 255, alpha);
    }

    const CRGBA gangColour = zone->ZoneColour;

    const auto blend = [intensity](unsigned char base, unsigned char gang) {
        return static_cast<unsigned char>(
            std::clamp(
                static_cast<int>(base + (gang - base) * intensity),
                0,
                255
            )
        );
    };

    return CRGBA(
        blend(255, gangColour.r),
        blend(255, gangColour.g),
        blend(255, gangColour.b),
        alpha
    );
}

class LeedsVehicleName {
public:
    static float nDisplayTimer;
    static CVehicle* pLastVehicle;

    static void Draw() {
        if (CCutsceneMgr::ms_cutsceneProcessing || TheCamera.m_bWideScreenOn) {
            nDisplayTimer = 0.0f;
            return;
        }

        CPlayerPed* player = FindPlayerPed();
        if (!player) return;

        CVehicle* pVeh = player->m_pVehicle;
        if (!pVeh) {
            nDisplayTimer = 0.0f;
            pLastVehicle = nullptr;
            return;
        }

        unsigned char state = *(unsigned char*)((uintptr_t)player + 0x530);
        bool bPeekPressed = (GetAsyncKeyState(nPeekKey) & 0x8000) != 0;

        if (state == 50) {
            if (pVeh != pLastVehicle || bPeekPressed) {
                nDisplayTimer = 300.0f;
                pLastVehicle = pVeh;
            }
        }
        else {
            pLastVehicle = nullptr;
        }

        if (nDisplayTimer > 0.0f) {
            if (g_LeedsCutsceneNameVisible) return;

            nDisplayTimer -= CTimer::ms_fTimeStep;

            int nAlpha = std::clamp(
                (nDisplayTimer < 20.0f) ? (int)(nDisplayTimer * 12.5f) : 255,
                0,
                255
            );

            CBaseModelInfo* pBaseInfo = CModelInfo::GetModelInfo(pVeh->m_nModelIndex);

            if (pBaseInfo) {
                char* gxtKey = (char*)((uintptr_t)pBaseInfo + 0x32);
                char* vehName = (char*)TheText.Get(gxtKey);

                if (vehName) {
                    CFont::SetOrientation(ALIGN_RIGHT);
                    CFont::SetFontStyle(UI_FONT);
                    CFont::SetScale(TEXT_SCALE_X, TEXT_SCALE_Y);
                    CFont::SetColor(CRGBA(255, 255, 255, nAlpha));
                    CFont::SetDropShadowPosition(1);
                    CFont::SetDropColor(CRGBA(0, 0, 0, nAlpha));
                    CFont::SetWrapx(SCREEN_WIDTH * 2.0f);
                    CFont::SetRightJustifyWrap(0.0f);

                    float x = SCREEN_WIDTH - Res(107.0f);
                    float y = SCREEN_HEIGHT - Res(200.0f);

                    CFont::PrintString(x, y, vehName);
                }
            }
        }
    }
};

float LeedsVehicleName::nDisplayTimer = 0.0f;
CVehicle* LeedsVehicleName::pLastVehicle = nullptr;

class LeedsAreaName {
public:
    static inline float m_visibilityTimer = 0.0f;
    static inline char m_savedZoneName[64] = { 0 };
    static inline bool m_isFadeIn = false;

    static void CaptureZoneName(float x, float y, char* str) {
        if (str && str[0] != '\0') {
            if (strcmp(m_savedZoneName, str) != 0) {
                strncpy(m_savedZoneName, str, 63);
                m_savedZoneName[63] = '\0';

                m_visibilityTimer = 1.0f;
                m_isFadeIn = true;
            }
        }
    }

    static void SilenceVehicleName(float x, float y, char* str) {}

    static void DrawAreaUI(char* str, float timer) {
        if (FrontEndMenuManager.m_bMenuActive ||
            CCutsceneMgr::ms_running ||
            !CHud::m_Wants_To_Draw_Hud) {
            return;
        }

        int alpha = 255;

        if (m_isFadeIn) {
            alpha = static_cast<int>((timer / 60.0f) * 255.0f);
        }
        else if (timer < 50.0f) {
            alpha = static_cast<int>((timer / 50.0f) * 255.0f);
        }

        alpha = std::clamp(alpha, 0, 255);

        if (alpha <= 0) return;

        CFont::SetProportional(true);
        CFont::SetFontStyle(UI_FONT);
        CFont::SetOrientation(ALIGN_RIGHT);
        CFont::SetScale(TEXT_SCALE_X, TEXT_SCALE_Y);
        CFont::SetDropShadowPosition(1);
        CFont::SetDropColor(CRGBA(0, 0, 0, alpha));

        // Kolor nazwy aktualnej dzielnicy.
        CFont::SetColor(GetAreaNameColour(alpha));

        CFont::SetWrapx(SCREEN_WIDTH * 2.0f);
        CFont::SetRightJustifyWrap(0.0f);

        float x = SCREEN_WIDTH - Res(107.0f);
        float y = SCREEN_HEIGHT - Res(140.0f);

        CFont::PrintString(x, y, str);
    }
};

class LeedsHUDPlugin {
private:
    static std::string GetIniPath() {
        char buffer[MAX_PATH];
        HMODULE hm = NULL;

        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&UI_FONT,
            &hm
        );

        GetModuleFileNameA(hm, buffer, MAX_PATH);

        std::string path(buffer);
        size_t lastSlash = path.find_last_of("\\/");

        return path.substr(0, lastSlash) + "\\LeedsHUD.ini";
    }

    static void LoadConfiguration() {
        std::string iniPath = GetIniPath();
        char fontSetting[32] = { 0 };

        GetPrivateProfileStringA(
            "Settings",
            "Mode",
            "vcs",
            fontSetting,
            sizeof(fontSetting),
            iniPath.c_str()
        );

        nPeekKey = GetPrivateProfileIntA(
            "Settings",
            "HighlightAreaVehicleNames",
            84,
            iniPath.c_str()
        );

        std::string selectedFont(fontSetting);

        std::transform(
            selectedFont.begin(),
            selectedFont.end(),
            selectedFont.begin(),
            ::tolower
        );

        if (selectedFont == "lcs") {
            UI_FONT = FONT_MENU;
            g_fontScaleModifier = 0.75f;
        }
        else {
            UI_FONT = FONT_GOTHIC;
            g_fontScaleModifier = 1.0f;
        }
    }

public:
    LeedsHUDPlugin() {
        LoadConfiguration();

        plugin::patch::RedirectCall(
            0x58AE5D,
            LeedsAreaName::CaptureZoneName
        );

        plugin::patch::RedirectCall(
            0x58B156,
            LeedsAreaName::SilenceVehicleName
        );

        Events::drawHudEvent += [] {
            LeedsVehicleName::Draw();
        };

        Events::drawingEvent += [] {
            if (LeedsAreaName::m_savedZoneName[0] == '\0' ||
                FrontEndMenuManager.m_bMenuActive) {
                return;
            }

            if (TheCamera.m_bWideScreenOn ||
                CHud::bScriptDontDisplayRadar) {
                return;
            }

            bool bIsPeeking =
                (GetAsyncKeyState(nPeekKey) & 0x8000) != 0;

            if (!g_LeedsCutsceneNameVisible) {
                if (LeedsAreaName::m_isFadeIn) {
                    LeedsAreaName::m_visibilityTimer +=
                        CTimer::ms_fTimeStep;

                    if (LeedsAreaName::m_visibilityTimer >= 300.0f) {
                        LeedsAreaName::m_visibilityTimer = 300.0f;
                        LeedsAreaName::m_isFadeIn = false;
                    }
                }
                else if (bIsPeeking) {
                    LeedsAreaName::m_visibilityTimer = 300.0f;
                }
                else if (LeedsAreaName::m_visibilityTimer > 0.0f) {
                    LeedsAreaName::m_visibilityTimer -=
                        CTimer::ms_fTimeStep;
                }

                if (LeedsAreaName::m_visibilityTimer > 0.0f) {
                    LeedsAreaName::DrawAreaUI(
                        LeedsAreaName::m_savedZoneName,
                        LeedsAreaName::m_visibilityTimer
                    );
                }
            }
        };
    }
} Leedscore;
