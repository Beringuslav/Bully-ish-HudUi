
#include "plugin.h"
#include "CCamera.h"
#include "CFont.h"
#include "CHud.h"
#include "CReplay.h"
#include "CTimer.h"
#include "CAERadioTrackManager.h"

#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace plugin;

static float Res(float value) {
    return value * ((float)RsGlobal.maximumHeight / 980.0f);
}

static float ResW(float value) {
    return value * ((float)RsGlobal.maximumHeight / 980.0f);
}

enum eSAFont {
    SA_FONT_GOTHIC = 0,
    SA_FONT_SUBTITLES = 1,
    SA_FONT_MENU = 2
};

struct RadioCfg {
    int fontStyle = SA_FONT_MENU;

    CRGBA colorNormal{ 30, 60, 140, 255 };
    CRGBA colorActive{ 100, 180, 255, 255 };

    float posX = 0.0f;
    float posY = 46.0f;

    float scaleX = 1.30f;
    float scaleY = 2.30f;
};

static RadioCfg cfg;

static int ClampColor(int value) {
    return std::clamp(value, 0, 255);
}

static float ClampScale(float value, float defaultValue) {
    if (!std::isfinite(value))
        return defaultValue;

    return std::clamp(value, 0.1f, 10.0f);
}

static void LoadConfig() {
    char iniPath[MAX_PATH]{};

    sprintf_s(
        iniPath,
        "%s\\LeedsHUD.ini",
        PLUGIN_PATH("")
    );

    char mode[16]{};

    GetPrivateProfileStringA(
        "Settings",
        "Mode",
        "vcs",
        mode,
        sizeof(mode),
        iniPath
    );

    cfg.fontStyle =
        (_stricmp(mode, "lcs") == 0)
        ? SA_FONT_MENU
        : SA_FONT_GOTHIC;

    auto readIntSafe = [&](const char* key, int defaultValue) -> int {
        char buffer[32]{};

        GetPrivateProfileStringA(
            "Colors",
            key,
            "",
            buffer,
            sizeof(buffer),
            iniPath
        );

        if (buffer[0] == '\0')
            return defaultValue;

        return atoi(buffer);
    };

    auto readFloat = [&](const char* key, float defaultValue) -> float {
        char buffer[32]{};

        GetPrivateProfileStringA(
            "Colors",
            key,
            "",
            buffer,
            sizeof(buffer),
            iniPath
        );

        if (buffer[0] == '\0')
            return defaultValue;

        char* end = nullptr;
        float value = strtof(buffer, &end);

        if (end == buffer || !std::isfinite(value))
            return defaultValue;

        return value;
    };

    cfg.colorNormal.r = ClampColor(readIntSafe("ColorNormal_R", cfg.colorNormal.r));
    cfg.colorNormal.g = ClampColor(readIntSafe("ColorNormal_G", cfg.colorNormal.g));
    cfg.colorNormal.b = ClampColor(readIntSafe("ColorNormal_B", cfg.colorNormal.b));
    cfg.colorNormal.a = ClampColor(readIntSafe("ColorNormal_A", cfg.colorNormal.a));

    cfg.colorActive.r = ClampColor(readIntSafe("ColorActive_R", cfg.colorActive.r));
    cfg.colorActive.g = ClampColor(readIntSafe("ColorActive_G", cfg.colorActive.g));
    cfg.colorActive.b = ClampColor(readIntSafe("ColorActive_B", cfg.colorActive.b));
    cfg.colorActive.a = ClampColor(readIntSafe("ColorActive_A", cfg.colorActive.a));

    cfg.posX = readFloat("PosX", cfg.posX);
    cfg.posY = readFloat("PosY", cfg.posY);

    cfg.scaleX = ClampScale(readFloat("ScaleX", cfg.scaleX), 1.30f);
    cfg.scaleY = ClampScale(readFloat("ScaleY", cfg.scaleY), 2.30f);
}

static void DrawRadioStation() {
    if (CTimer::m_UserPause ||
        CTimer::m_CodePause ||
        TheCamera.m_bWideScreenOn ||
        !FindPlayerVehicle(-1, 0) ||
        CReplay::Mode == 1) {
        return;
    }

    bool isScrollingNow =
        (AERadioTrackManager.m_nRetunesPending != 0 ||
         AERadioTrackManager.m_nPreviousRetunesPending != 0);

    if (isScrollingNow) {
        AERadioTrackManager.m_nStationNameDisplayEndTimeMs =
            CTimer::m_snTimeInMilliseconds + 2500;
    }

    if (AERadioTrackManager.m_bDisplayNewStationName &&
        AERadioTrackManager.IsVehicleRadioActive()) {

        AERadioTrackManager.m_nStationNameDisplayEndTimeMs =
            CTimer::m_snTimeInMilliseconds + 2500;

        AERadioTrackManager.m_bDisplayNewStationName = false;
    }

    if (CTimer::m_snTimeInMilliseconds >=
        AERadioTrackManager.m_nStationNameDisplayEndTimeMs) {
        return;
    }

    int stationIdx = isScrollingNow
        ? static_cast<int>(AERadioTrackManager.m_RequestedSettings.StationID)
        : static_cast<int>(AERadioTrackManager.m_ActiveSettings.StationID);

    if (stationIdx <= 0 || stationIdx >= 14)
        return;

    char* pName =
        AERadioTrackManager.GetRadioStationName(static_cast<signed char>(stationIdx));

    if (!pName || pName[0] == '\0')
        return;

    CFont::SetProportional(true);
    CFont::SetBackground(false, false);
    CFont::SetOrientation(ALIGN_CENTER);
    CFont::SetCentreSize(ResW(2240.0f));
    CFont::SetFontStyle(cfg.fontStyle);
    CFont::SetScale(ResW(cfg.scaleX), Res(cfg.scaleY));
    CFont::SetDropShadowPosition(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::SetWrapx(10000.0f);

    CFont::SetColor(isScrollingNow ? cfg.colorNormal : cfg.colorActive);

    CFont::PrintString(
        ((float)RsGlobal.maximumWidth / 2.0f) + ResW(cfg.posX),
        Res(cfg.posY),
        pName
    );

    CFont::DrawFonts();
}

class RadioTextOverride {
public:
    RadioTextOverride() {
        LoadConfig();

        patch::Set(0x53E4FA, 5);

        Events::drawHudEvent += [] {
            DrawRadioStation();
        };
    }
};

RadioTextOverride radioTextOverride;
