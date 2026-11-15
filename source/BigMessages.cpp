
#include "plugin.h"
#include "CHud.h"
#include "CFont.h"
#include "CTimer.h"
#include "CText.h"
#include "CSprite2d.h"
#include "CCutsceneMgr.h"
#include "CCamera.h"

#include <windows.h>
#include <algorithm>
#include <string>
#include <cstring>
#include <cstdint>

using namespace plugin;

// Zmienna współdzielona z AreaVehicleNames.cpp
bool g_LeedsCutsceneNameVisible = false;

#define SCREEN_WIDTH   ((float)RsGlobal.maximumWidth)
#define SCREEN_HEIGHT ((float)RsGlobal.maximumHeight)

#define SCALE_Y(val)  ((val) * (SCREEN_HEIGHT / 1080.0f))
#define SCALE_X(val)  ((val) * (SCREEN_WIDTH  / 1920.0f))

#define REWARD_GAP_PERCENT 0.02f

typedef void(__cdecl* AddBigMessage_t)(
    const char*,
    unsigned int,
    unsigned short
);

// -------------------------------------------------------------
// Bezpieczne kopiowanie tekstu
// -------------------------------------------------------------

static void CopyText(char* destination, size_t destinationSize, const char* source) {
    if (!destination || destinationSize == 0)
        return;

    if (!source) {
        destination[0] = '\0';
        return;
    }

    strncpy(destination, source, destinationSize - 1);
    destination[destinationSize - 1] = '\0';
}

// -------------------------------------------------------------
// Przechwytywanie komunikatów GTA
// -------------------------------------------------------------

void __cdecl MyAddBigMessage(
    const char* text,
    unsigned int time,
    unsigned short style)
{
    if (text && (style == 1 || style == 2))
        return;

    ((AddBigMessage_t)0x58C6A5)(text, time, style);
}

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
// Tryb HUD
// -------------------------------------------------------------

static int GetHudMode() {
    static bool bChecked = false;
    static int cachedMode = 0;

    if (!bChecked) {
        std::string iniPath = GetAsiDir() + "LeedsHUD.ini";

        char result[32] = {};

        GetPrivateProfileStringA(
            "Settings",
            "Mode",
            "default",
            result,
            sizeof(result),
            iniPath.c_str()
        );

        std::string mode(result);

        std::transform(
            mode.begin(),
            mode.end(),
            mode.begin(),
            [](unsigned char c) {
                return static_cast<char>(tolower(c));
            }
        );

        if (mode == "vcs")
            cachedMode = 2;
        else if (mode == "lcs")
            cachedMode = 1;

        bChecked = true;
    }

    return cachedMode;
}

static bool IsVcsMode() {
    return GetHudMode() == 2;
}

// -------------------------------------------------------------
// Usuwanie kodów kolorów GTA
// -------------------------------------------------------------

static std::string StripColorTokens(const std::string& input) {
    std::string output = input;

    size_t pos = 0;

    while ((pos = output.find('~')) != std::string::npos) {
        size_t end = output.find('~', pos + 1);

        if (end != std::string::npos)
            output.erase(pos, (end - pos) + 1);
        else
            break;
    }

    return output;
}

// -------------------------------------------------------------
// Główna obsługa komunikatów
// -------------------------------------------------------------

class LeedsHUDBigMessage {
public:

    static inline bool bDeathArrestShowing = false;
    static inline bool bDeathArrestFadingOut = false;
    static inline float alphaDeathArrest = 0.0f;

    static inline char capturedDeathArrestTitle[128] = "";

    static inline bool bMissionShowing = false;
    static inline bool bMissionFadingOut = false;
    static inline bool bIsTemporary = false;

    static inline float alphaMission = 0.0f;

    static inline unsigned int missionDisplayTimer = 0;

    static inline char capturedMissionTitle[128] = "";
    static inline char capturedFailReason[256] = "";
    static inline char capturedReward[256] = "";

    static inline bool bCutsceneNameShowing = false;
    static inline bool bCutsceneNameFadingOut = false;

    static inline float alphaCutsceneName = 0.0f;

    static inline unsigned int cutsceneNameTimer = 0;

    static inline char capturedCutsceneName[128] = "";

    static inline bool bProcessingDeathArrest = false;
    static inline bool bProcessingMission = false;

    static inline char lastProcessedMissionString[128] = "";

    static inline unsigned int missionStringResetTimer = 0;

    static inline unsigned int deathArrestResetTimer = 0;

    static inline char lastProcessedDeathArrestString[128] = "";

    static inline unsigned int lastClosedTime = 0;

    static inline bool bIsFailedShowing = false;

    // ---------------------------------------------------------
    // Rozpoznawanie ważnych komunikatów
    // ---------------------------------------------------------

    static bool IsMajorEvent(const char* text) {
        if (!text)
            return false;

        return
            strstr(text, "PASSED") ||
            strstr(text, "FAILED") ||
            strstr(text, "WASTED") ||
            strstr(text, "BUSTED") ||
            strstr(text, "WINNER");
    }

    static bool IsAnyResult(const char* text) {
        if (!text || !text[0])
            return false;

        return
            IsMajorEvent(text) ||
            strstr(text, "Round") ||
            strstr(text, "LEVEL") ||
            strstr(text, "Passed") ||
            strstr(text, "Winner");
    }

    // ---------------------------------------------------------
    // Kontrola dostępności pamięci
    // ---------------------------------------------------------

    static bool IsBadReadPtr(void* ptr) {
        if (!ptr)
            return true;

        MEMORY_BASIC_INFORMATION mbi{};

        if (VirtualQuery(ptr, &mbi, sizeof(mbi))) {
            DWORD mask =
                PAGE_READONLY |
                PAGE_READWRITE |
                PAGE_WRITECOPY |
                PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE |
                PAGE_EXECUTE_WRITECOPY;

            return
                !(mbi.Protect & mask) ||
                (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        }

        return true;
    }

    // ---------------------------------------------------------
    // Aktualizacja komunikatów
    // ---------------------------------------------------------

    static void Update() {
        unsigned int now = CTimer::m_snTimeInMilliseconds;

        if (now > missionStringResetTimer)
            lastProcessedMissionString[0] = '\0';

        if (now > deathArrestResetTimer)
            lastProcessedDeathArrestString[0] = '\0';

        // -----------------------------------------------------
        // Nazwa sceny
        // -----------------------------------------------------

        if (bCutsceneNameShowing) {
            if (!bCutsceneNameFadingOut) {
                alphaCutsceneName += 0.05f;

                if (alphaCutsceneName > 1.0f)
                    alphaCutsceneName = 1.0f;

                if (now > cutsceneNameTimer)
                    bCutsceneNameFadingOut = true;
            }
            else {
                alphaCutsceneName -= 0.03f;

                if (alphaCutsceneName <= 0.0f) {
                    alphaCutsceneName = 0.0f;

                    bCutsceneNameShowing = false;
                    bCutsceneNameFadingOut = false;

                    memset(
                        capturedCutsceneName,
                        0,
                        sizeof(capturedCutsceneName)
                    );
                }
            }
        }

        if (CHud::m_BigMessage[1][0] != '\0') {
            std::string raw =
                StripColorTokens(CHud::m_BigMessage[1]);

            if (!raw.empty() &&
                strcmp(capturedCutsceneName, raw.c_str()) != 0) {

                CopyText(
                    capturedCutsceneName,
                    sizeof(capturedCutsceneName),
                    raw.c_str()
                );

                alphaCutsceneName = 0.0f;
                bCutsceneNameShowing = true;
                bCutsceneNameFadingOut = false;

                cutsceneNameTimer = now + 4000;
            }
        }

        // -----------------------------------------------------
        // Obsługa cutscen
        // -----------------------------------------------------

        if (CCutsceneMgr::ms_running) {
            CHud::m_BigMessage[1][0] = '\0';
            return;
        }

        // -----------------------------------------------------
        // Powód niepowodzenia misji
        // -----------------------------------------------------

        if (!bMissionShowing && !bDeathArrestShowing) {
            if (CHud::m_Message[0] &&
                (strstr(CHud::m_Message, "~r~") ||
                 strstr(CHud::m_Message, "~R~"))) {

                CopyText(
                    capturedFailReason,
                    sizeof(capturedFailReason),
                    CHud::m_Message
                );
            }
        }

        // -----------------------------------------------------
        // WASTED / BUSTED
        // -----------------------------------------------------

        CPed* player = FindPlayerPed();

        if (player &&
            reinterpret_cast<uintptr_t>(player) > 0x1000 &&
            !IsBadReadPtr(player) &&
            !bProcessingDeathArrest) {

            float fHealth = patch::Get<float>(
                reinterpret_cast<uintptr_t>(player) + 0x540
            );

            bool isDead = fHealth < 1.0f;

            unsigned int playerState = 0;

            if (!isDead) {
                playerState = patch::Get<unsigned int>(
                    reinterpret_cast<uintptr_t>(player) + 0x530
                );
            }

            bool isBusted = (playerState == 63);

            if (isDead || isBusted) {
                const char* eventName =
                    isDead ? "WASTED!" : "BUSTED!";

                if (strcmp(
                    lastProcessedDeathArrestString,
                    eventName
                ) != 0) {

                    bProcessingDeathArrest = true;

                    CopyText(
                        capturedDeathArrestTitle,
                        sizeof(capturedDeathArrestTitle),
                        eventName
                    );

                    CopyText(
                        lastProcessedDeathArrestString,
                        sizeof(lastProcessedDeathArrestString),
                        eventName
                    );

                    deathArrestResetTimer = now + 5000;

                    bDeathArrestShowing = true;
                    bDeathArrestFadingOut = false;

                    memset(
                        capturedFailReason,
                        0,
                        sizeof(capturedFailReason)
                    );
                }
            }
        }

        // -----------------------------------------------------
        // Mission Passed / Mission Failed
        // -----------------------------------------------------

        if (!bProcessingMission &&
            CHud::m_BigMessage[0][0]) {

            const char* hudMsg = CHud::m_BigMessage[0];

            if (IsAnyResult(hudMsg) &&
                strcmp(lastProcessedMissionString, hudMsg) != 0) {

                bool isResult =
                    strstr(hudMsg, "PASSED") ||
                    strstr(hudMsg, "FAILED") ||
                    strstr(hudMsg, "Passed") ||
                    strstr(hudMsg, "WINNER") ||
                    strstr(hudMsg, "Winner");

                if (isResult && (now - lastClosedTime < 4000)) {
                    CHud::m_BigMessage[0][0] = '\0';
                    return;
                }

                bProcessingMission = true;

                CopyText(
                    lastProcessedMissionString,
                    sizeof(lastProcessedMissionString),
                    hudMsg
                );

                missionStringResetTimer = now + 10000;

                // Bezpieczne rozdzielenie tytułu i nagrody.
                const char* newline = strstr(hudMsg, "~n~");

                if (newline) {
                    size_t titleLength =
                        static_cast<size_t>(newline - hudMsg);

                    size_t safeLength = std::min(
                        titleLength,
                        sizeof(capturedMissionTitle) - 1
                    );

                    memcpy(
                        capturedMissionTitle,
                        hudMsg,
                        safeLength
                    );

                    capturedMissionTitle[safeLength] = '\0';

                    CopyText(
                        capturedReward,
                        sizeof(capturedReward),
                        newline + 3
                    );
                }
                else {
                    CopyText(
                        capturedMissionTitle,
                        sizeof(capturedMissionTitle),
                        hudMsg
                    );

                    capturedReward[0] = '\0';
                }

                bMissionShowing = true;
                bMissionFadingOut = false;

                bIsTemporary = !IsMajorEvent(hudMsg);

                missionDisplayTimer =
                    now + (bIsTemporary ? 2500 : 6000);
            }
        }

        CHud::m_BigMessage[0][0] = '\0';
        CHud::m_BigMessage[1][0] = '\0';

        // -----------------------------------------------------
        // Stan niepowodzenia misji
        // -----------------------------------------------------

        bIsFailedShowing =
            bMissionShowing &&
            strstr(capturedMissionTitle, "FAILED") &&
            !bIsTemporary;

        // -----------------------------------------------------
        // Animacja WASTED / BUSTED
        // -----------------------------------------------------

        if (bDeathArrestShowing) {
            if (!bDeathArrestFadingOut) {
                alphaDeathArrest += 0.05f;

                if (alphaDeathArrest > 1.0f)
                    alphaDeathArrest = 1.0f;

                if (TheCamera.m_fFadeAlpha > 240.0f)
                    bDeathArrestFadingOut = true;
            }
            else {
                alphaDeathArrest -= 0.05f;

                if (alphaDeathArrest <= 0.0f) {
                    alphaDeathArrest = 0.0f;

                    bDeathArrestShowing = false;
                    bDeathArrestFadingOut = false;
                    bProcessingDeathArrest = false;

                    memset(
                        capturedDeathArrestTitle,
                        0,
                        sizeof(capturedDeathArrestTitle)
                    );
                }
            }
        }

        // -----------------------------------------------------
        // Animacja komunikatów misji
        // -----------------------------------------------------

        if (bMissionShowing) {
            if (!bMissionFadingOut) {
                alphaMission += 0.05f;

                if (alphaMission > 1.0f)
                    alphaMission = 1.0f;

                if ((bDeathArrestShowing &&
                    TheCamera.m_fFadeAlpha > 240.0f) ||
                    now > missionDisplayTimer) {

                    bMissionFadingOut = true;
                }
            }
            else {
                alphaMission -= 0.05f;

                if (alphaMission <= 0.0f) {
                    alphaMission = 0.0f;

                    lastClosedTime = now;

                    bMissionShowing = false;
                    bMissionFadingOut = false;
                    bIsTemporary = false;
                    bProcessingMission = false;

                    memset(
                        capturedMissionTitle,
                        0,
                        sizeof(capturedMissionTitle)
                    );

                    memset(
                        capturedReward,
                        0,
                        sizeof(capturedReward)
                    );

                    memset(
                        capturedFailReason,
                        0,
                        sizeof(capturedFailReason)
                    );
                }
            }
        }

        // Synchronizacja z AreaVehicleNames.cpp
        g_LeedsCutsceneNameVisible =
            bCutsceneNameShowing &&
            alphaCutsceneName > 0.0f;
    }

    // ---------------------------------------------------------
    // Ustawienia tekstu
    // ---------------------------------------------------------

    static void ApplySafeNoWrap() {
        CFont::SetWrapx(10000.0f);
        CFont::SetCentreSize(10000.0f);
        CFont::SetRightJustifyWrap(10000.0f);
    }

    // ---------------------------------------------------------
    // Rysowanie komunikatów
    // ---------------------------------------------------------

    static void Draw() {
        float resScale = SCREEN_HEIGHT / 1080.0f;

        float centerX = SCREEN_WIDTH / 2.0f;
        float centerY = SCREEN_HEIGHT / 2.0f;

        int hudMode = GetHudMode();

        bool vcs = (hudMode == 2);

        if (!CCutsceneMgr::ms_running) {

            // -------------------------------------------------
            // WASTED / BUSTED
            // -------------------------------------------------

            if (alphaDeathArrest > 0.0f) {
                unsigned char a =
                    static_cast<unsigned char>(
                        210.0f * alphaDeathArrest
                    );

                CRGBA col =
                    vcs
                    ? CRGBA(9, 211, 114, a)
                    : (
                        strstr(capturedDeathArrestTitle, "BUSTED")
                        ? CRGBA(100, 180, 255, a)
                        : CRGBA(255, 60, 60, a)
                    );

                CFont::SetProportional(true);
                CFont::SetOrientation(ALIGN_CENTER);
                CFont::SetDropColor(CRGBA(0, 0, 0, a));
                CFont::SetFontStyle(FONT_PRICEDOWN);

                CFont::SetScale(
                    3.2f * resScale,
                    4.8f * resScale
                );

                CFont::SetColor(col);
                CFont::SetEdge(1);
                CFont::SetDropShadowPosition(2);

                ApplySafeNoWrap();

                CFont::PrintString(
                    centerX,
                    centerY - SCALE_Y(220.0f),
                    capturedDeathArrestTitle
                );
            }

            // -------------------------------------------------
            // Komunikaty misji
            // -------------------------------------------------

            if (alphaMission > 0.0f) {
                unsigned char a =
                    static_cast<unsigned char>(
                        210.0f * alphaMission
                    );

                CRGBA col =
                    vcs
                    ? CRGBA(200, 120, 151, a)
                    : (
                        (
                            strstr(capturedMissionTitle, "PASSED") ||
                            strstr(capturedMissionTitle, "WINNER") ||
                            bIsTemporary
                        )
                        ? CRGBA(255, 220, 0, a)
                        : CRGBA(255, 60, 60, a)
                    );

                CFont::SetProportional(true);
                CFont::SetOrientation(ALIGN_CENTER);
                CFont::SetDropColor(CRGBA(0, 0, 0, a));
                CFont::SetFontStyle(FONT_PRICEDOWN);

                CFont::SetScale(
                    (bIsTemporary ? 2.8f : 3.2f) * resScale,
                    (bIsTemporary ? 4.2f : 4.8f) * resScale
                );

                CFont::SetColor(col);
                CFont::SetEdge(1);
                CFont::SetDropShadowPosition(2);

                std::string title = capturedMissionTitle;

                if (vcs)
                    title = StripColorTokens(title);

                ApplySafeNoWrap();

                CFont::PrintString(
                    centerX,
                    centerY - SCALE_Y(40.0f),
                    const_cast<char*>(title.c_str())
                );

                CFont::SetEdge(1);
                CFont::SetDropShadowPosition(2);

                float y = centerY + SCALE_Y(70.0f);

                // -------------------------------------------------
                // Powód niepowodzenia
                // -------------------------------------------------

                if (capturedFailReason[0] &&
                    strstr(capturedMissionTitle, "FAILED") &&
                    !bIsTemporary) {

                    std::string failReason = capturedFailReason;

                    if (vcs)
                        failReason = StripColorTokens(failReason);

                    CFont::SetFontStyle(FONT_SUBTITLES);
                    CFont::SetDropShadowPosition(1);

                    CFont::SetScale(
                        0.85f * resScale,
                        1.70f * resScale
                    );

                    CFont::SetColor(
                        vcs ? col : CRGBA(255, 60, 60, a)
                    );

                    ApplySafeNoWrap();

                    CFont::PrintString(
                        centerX,
                        y,
                        const_cast<char*>(failReason.c_str())
                    );

                    y += SCALE_Y(45.0f);
                }

                // -------------------------------------------------
                // Nagroda
                // -------------------------------------------------

                if (capturedReward[0]) {
                    std::string reward = capturedReward;

                    reward = StripColorTokens(reward);

                    size_t tokenPos = 0;

                    while ((tokenPos = reward.find("~n~")) != std::string::npos)
                        reward.erase(tokenPos, 3);

                    while ((tokenPos = reward.find('\n')) != std::string::npos)
                        reward.erase(tokenPos, 1);

                    while ((tokenPos = reward.find('\r')) != std::string::npos)
                        reward.erase(tokenPos, 1);

                    while (!reward.empty() &&
                        (reward.back() == ' ' || reward.back() == '\t')) {

                        reward.pop_back();
                    }

                    while (!reward.empty() &&
                        (reward.front() == ' ' || reward.front() == '\t')) {

                        reward.erase(0, 1);
                    }

                    CFont::SetProportional(true);
                    CFont::SetFontStyle(FONT_PRICEDOWN);

                    CFont::SetScale(
                        1.5f * resScale,
                        2.8f * resScale
                    );

                    CFont::SetColor(
                        vcs ? col : CRGBA(225, 225, 225, a)
                    );

                    CFont::SetOrientation(ALIGN_CENTER);

                    size_t respectPos = reward.find("RESPECT");
                    size_t moneyPos = reward.find("$");

                    std::string finalizedRewardString;

                    if (respectPos != std::string::npos &&
                        moneyPos != std::string::npos) {

                        std::string moneyPart =
                            reward.substr(0, respectPos);

                        std::string respectPart =
                            reward.substr(respectPos);

                        while (!moneyPart.empty() &&
                            moneyPart.back() == ' ') {

                            moneyPart.pop_back();
                        }

                        while (!respectPart.empty() &&
                            respectPart.front() == ' ') {

                            respectPart.erase(0, 1);
                        }

                        if (respectPart.find('+') == std::string::npos)
                            respectPart += " +";

                        finalizedRewardString =
                            moneyPart + " - " + respectPart;
                    }
                    else {
                        if (respectPos != std::string::npos &&
                            reward.find('+') == std::string::npos) {

                            reward += " +";
                        }

                        finalizedRewardString = reward;
                    }

                    ApplySafeNoWrap();

                    CFont::PrintString(
                        centerX,
                        centerY + SCALE_Y(70.0f),
                        const_cast<char*>(finalizedRewardString.c_str())
                    );
                }
            }
        }

        // -----------------------------------------------------
        // Nazwa sceny
        // -----------------------------------------------------

        if (alphaCutsceneName > 0.0f) {
            unsigned char a =
                static_cast<unsigned char>(
                    210.0f * alphaCutsceneName
                );

            // Domyślny styl czcionki zapobiega użyciu
            // niezainicjalizowanej zmiennej.
            eFontStyle font = FONT_GOTHIC;

            if (hudMode == 1)
                font = FONT_MENU;
            else if (hudMode == 2)
                font = FONT_GOTHIC;

            std::string name = capturedCutsceneName;

            float targetX = SCREEN_WIDTH - SCALE_X(100.0f);
            float targetY = SCREEN_HEIGHT - SCALE_Y(120.0f);

            CFont::SetProportional(true);
            CFont::SetOrientation(ALIGN_RIGHT);
            CFont::SetFontStyle(font);
            CFont::SetWrapx(-1000.0f);

            CFont::SetColor(CRGBA(255, 220, 0, a));
            CFont::SetDropColor(CRGBA(0, 0, 0, a));

            CFont::SetEdge(1);
            CFont::SetDropShadowPosition(1);

            if (hudMode == 2) {
                CFont::SetScale(
                    2.4f * resScale,
                    3.7f * resScale
                );

                CFont::PrintString(
                    targetX,
                    targetY,
                    const_cast<char*>(name.c_str())
                );
            }
            else if (hudMode == 1) {
                CFont::SetScale(
                    1.4f * resScale,
                    2.2f * resScale
                );

                CFont::PrintString(
                    targetX,
                    targetY,
                    const_cast<char*>(name.c_str())
                );
            }
        }

        // Przywrócenie podstawowych ustawień czcionki.
        CFont::SetOrientation(ALIGN_LEFT);
        CFont::SetJustify(false);
        CFont::SetEdge(0);
        CFont::SetDropShadowPosition(0);
    }

    // ---------------------------------------------------------
    // Konstruktor pluginu
    // ---------------------------------------------------------

    LeedsHUDBigMessage() {
        patch::RedirectJump(
            0x58C6A0,
            (void*)MyAddBigMessage
        );

        Events::processScriptsEvent += Update;
        Events::drawHudEvent += Draw;
    }

} leedshudbigmessage;
