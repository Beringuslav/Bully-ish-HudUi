
#include "plugin.h"
#include "common.h"
#include "CMenuManager.h"
#include "CTxdStore.h"
#include "CRadar.h"
#include "CSprite2d.h"
#include "CPed.h"
#include "CVehicle.h"
#include <string>
#include <windows.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include "injector.hpp"

using namespace plugin;

extern float g_LeedsRadarHealthPercent;
extern float g_LeedsRadarArmorPercent;
extern float g_LeedsRadarBreathPercent;

extern bool g_LeedsRadarIndicatorsEnabled;
extern bool g_LeedsRadarShowArmor;
extern bool g_LeedsRadarShowBreath;

// Widoczność plusów ulepszeń.
extern bool g_LeedsRadarShowHealthPlus;
extern bool g_LeedsRadarShowArmorPlus;

// Kolory plusów z LeedsHUD.ini.
extern int plusHealthR;
extern int plusHealthG;
extern int plusHealthB;

extern int plusArmorR;
extern int plusArmorG;
extern int plusArmorB;

// Funkcja rysująca istniejący plusSprite z Main.cpp.
extern void LeedsHUDDrawPlus(
    const CRect& drawArea,
    int red,
    int green,
    int blue,
    unsigned char alpha
);

extern unsigned char g_LeedsRadarIndicatorAlpha;

static RwTexture* (__cdecl* _RwTextureRead)(const char*, const char*) =
    (RwTexture * (__cdecl*)(const char*, const char*))0x7F3AC0;

static bool isLoaded = false;
static bool bLoaded = false;

static CSprite2d mLeedsHudSprite;
static CSprite2d mBarFrame;

static RwTexture* pMapDirTex = nullptr;
static RwTexture* pNorthTex = nullptr;
static RwTexture* pDestinationTex = nullptr;

// Zmieniona pozycja Y: 148.0f -> 128.0f.
const CVector2D mLeedsHudPos(169.5f, 128.0f);

const float mLeedsHudBaseSize = 85.0f;
const float mLeedsHudRingSize = 95.0f;

static float g_BlipSize = 11.0f;
static float g_SpriteBlipSize = 25.0f;
static float g_EntityBlipSize = 11.0f;

static float gHudScale = 1.0f;

static const float kScaleDefault = 1.0f;
static const float kScaleLarge = 1.55f;

static float gZoomFoot = 180.0f;
static float gZoomCar = 283.3333f;

static const float kZoomDefault = 180.0f;
static const float kZoomCarDefault = 283.3333f;

static const float kZoomLarge = 350.0f;
static const float kZoomCarLarge = 420.0f;

static bool bBigRadarActive = false;
static bool bBigFadingOut = false;

static DWORD bigFadeStartTime = 0;

static bool bTZoomActive = false;
static bool bTFadingOut = false;

static DWORD tZoomStartTime = 0;
static DWORD tFadeStartTime = 0;

static int gKeyZoomOut = 0x54;
static int gKeyBigRadar = 0x58;

// Parametry animowanej powierzchni wody.
static const int kWaveSegments = 48;
static const float kWaveAmplitude = 0.012f;
static const float kWaveSpeed = 2.4f;
static const float kWaveFrequency = 2.2f;

#define screenHeight (static_cast<float>(RsGlobal.maximumHeight))
#define screenRes(val) (static_cast<float>(val) * (screenHeight / 900.0f))

#define LIVE_RING (mLeedsHudRingSize * gHudScale)
#define LIVE_BASE (mLeedsHudBaseSize * gHudScale)

#define LIVE_POS_X (mLeedsHudPos.x + (mLeedsHudRingSize * (gHudScale - 1.0f)))
#define LIVE_POS_Y (mLeedsHudPos.y + (mLeedsHudRingSize * (gHudScale - 1.0f)))

class LeedsHUD {
public:
    static int txdIndex;

    static void GetRadarIndicatorData(
        float& health,
        float& armor,
        float& breath,
        bool& showArmor,
        bool& showBreath,
        unsigned char& alpha)
    {
        health = 1.0f;
        armor = 0.0f;
        breath = 1.0f;

        showArmor = false;
        showBreath = false;
        alpha = 0;

        if (!g_LeedsRadarIndicatorsEnabled)
            return;

        health = std::clamp(g_LeedsRadarHealthPercent, 0.0f, 1.0f);
        armor = std::clamp(g_LeedsRadarArmorPercent, 0.0f, 1.0f);
        breath = std::clamp(g_LeedsRadarBreathPercent, 0.0f, 1.0f);

        showArmor = g_LeedsRadarShowArmor;
        showBreath = g_LeedsRadarShowBreath;
        alpha = g_LeedsRadarIndicatorAlpha;
    }

    // Miesza kolor wskaźnika z bielą na podstawie HUDAlpha.
    static CRGBA ApplyIndicatorAlpha(
        int r,
        int g,
        int b,
        unsigned char alpha)
    {
        float strength = static_cast<float>(alpha) / 255.0f;

        r = static_cast<int>(255.0f * (1.0f - strength) + r * strength);
        g = static_cast<int>(255.0f * (1.0f - strength) + g * strength);
        b = static_cast<int>(255.0f * (1.0f - strength) + b * strength);

        return CRGBA(
            static_cast<unsigned char>(std::clamp(r, 0, 255)),
            static_cast<unsigned char>(std::clamp(g, 0, 255)),
            static_cast<unsigned char>(std::clamp(b, 0, 255)),
            255
        );
    }

    // Kolor HP: ustalona zieleń przy pełnym zdrowiu, czerwony przy niskim.
    static CRGBA GetHealthDiscColor(
        float health,
        unsigned char alpha)
    {
        health = std::clamp(health, 0.0f, 1.0f);

        int r = static_cast<int>((1.0f - health) * 255.0f + health * 160.0f);
        int g = static_cast<int>(health * 233.0f);
        int b = static_cast<int>(health * 102.0f);

        return ApplyIndicatorAlpha(r, g, b, alpha);
    }

    // Kolor pancerza: jasny szary.
    static CRGBA GetArmorDiscColor(
        float armor,
        unsigned char alpha)
    {
        armor = std::clamp(armor, 0.0f, 1.0f);

        return ApplyIndicatorAlpha(200, 200, 200, alpha);
    }

    // Kolor tlenu: turkusowy.
    static CRGBA GetBreathDiscColor(
        float breath,
        unsigned char alpha)
    {
        breath = std::clamp(breath, 0.0f, 1.0f);

        int r = 0;
        int g = static_cast<int>(breath * 255.0f);
        int b = static_cast<int>(breath * 255.0f);

        return ApplyIndicatorAlpha(r, g, b, alpha);
    }

    // Rysuje animowane wypełnienie wskaźnika.
    // Fala porusza się stale, niezależnie od zmian poziomu wskaźnika.
    // Jej amplituda jest ograniczona przez aktualną ilość wypełnienia.
    static void DrawWaveIndicator(
        const CRect& drawArea,
        float percent,
        const CRGBA& color,
        float uStart,
        float uEnd,
        float phaseOffset)
    {
        if (!mLeedsHudSprite.m_pTexture)
            return;

        percent = std::clamp(percent, 0.0f, 1.0f);

        if (percent <= 0.001f)
            return;

        const float fullWidth = drawArea.right - drawArea.left;
        const float fullHeight = drawArea.bottom - drawArea.top;

        if (fullWidth <= 0.0f || fullHeight <= 0.0f)
            return;

        const float filledHeight = fullHeight * percent;
        const float filledTop = drawArea.bottom - filledHeight;

        // Czas animacji niezależny od zmian wartości wskaźnika.
        const float time =
            static_cast<float>(GetTickCount()) * 0.001f;

        // Przy niższym poziomie wskaźnika fala ma mniejszą amplitudę.
        const float amplitude = std::min(
            fullHeight * kWaveAmplitude,
            filledHeight * 0.18f
        );

        if (amplitude <= 0.01f)
            return;

        // Dodatkowa składowa tworzy bardziej organiczny kształt fali.
        const float secondaryAmplitude = amplitude * 0.28f;

        for (int i = 0; i < kWaveSegments; i++) {
            const float segmentStart =
                static_cast<float>(i) /
                static_cast<float>(kWaveSegments);

            const float segmentEnd =
                static_cast<float>(i + 1) /
                static_cast<float>(kWaveSegments);

            const float x0 =
                drawArea.left + fullWidth * segmentStart;

            const float x1 =
                drawArea.left + fullWidth * segmentEnd;

            const float middle =
                (segmentStart + segmentEnd) * 0.5f;

            const float wavePhase =
                middle * 6.28318530718f * kWaveFrequency
                - time * kWaveSpeed
                + phaseOffset;

            const float secondaryPhase =
                middle * 6.28318530718f * 3.7f
                + time * 1.65f
                + phaseOffset * 0.7f;

            const float wave =
                std::sinf(wavePhase) +
                std::sinf(secondaryPhase) * 0.28f;

            float waveTop =
                filledTop + wave * amplitude;

            // Nie pozwalamy fali wyjść poza obszar tarczy
            // ani poza dolną granicę wypełnienia.
            waveTop = std::clamp(
                waveTop,
                drawArea.top,
                drawArea.bottom - 0.5f
            );

            if (waveTop >= drawArea.bottom)
                continue;

            const float vTop =
                std::clamp(
                    (waveTop - drawArea.top) / fullHeight,
                    0.0f,
                    1.0f
                );

            const float vBottom = 1.0f;

            const float u0 =
                uStart + (uEnd - uStart) * segmentStart;

            const float u1 =
                uStart + (uEnd - uStart) * segmentEnd;

            CRect segmentArea(
                x0,
                waveTop,
                x1,
                drawArea.bottom
            );

            // Zachowujemy oryginalne współrzędne tekstury radardisc.
            // Jej przezroczystość nadal ogranicza wypełnienie do kształtu tarczy.
            mLeedsHudSprite.Draw(
                segmentArea,
                color,
                u0, vTop,
                u1, vTop,
                u0, vBottom,
                u1, vBottom
            );
        }
    }

    // Lewa połowa — zdrowie.
    static void DrawLeftIndicator(
        const CRect& drawArea,
        float percent,
        const CRGBA& color)
    {
        float middleX =
            (drawArea.left + drawArea.right) * 0.5f;

        CRect leftArea(
            drawArea.left,
            drawArea.top,
            middleX,
            drawArea.bottom
        );

        DrawWaveIndicator(
            leftArea,
            percent,
            color,
            0.0f,
            0.5f,
            0.0f
        );
    }

    // Prawa połowa — pancerz albo tlen.
    static void DrawRightIndicator(
        const CRect& drawArea,
        float percent,
        const CRGBA& color,
        float phaseOffset)
    {
        float middleX =
            (drawArea.left + drawArea.right) * 0.5f;

        CRect rightArea(
            middleX,
            drawArea.top,
            drawArea.right,
            drawArea.bottom
        );

        DrawWaveIndicator(
            rightArea,
            percent,
            color,
            0.5f,
            1.0f,
            phaseOffset
        );
    }

    // Rysuje plusy ulepszeń poza górną częścią tarczy.
    // Ich rozmiar i pozycja są zależne od rozmiaru drawArea.
    static void DrawRadarPlusIcons(
        const CRect& drawArea,
        unsigned char alpha,
        bool showBreath)
    {
        if (alpha == 0)
            return;

        if (!g_LeedsRadarShowHealthPlus &&
            !g_LeedsRadarShowArmorPlus)
            return;

        const float fullWidth =
            drawArea.right - drawArea.left;

        const float fullHeight =
            drawArea.bottom - drawArea.top;

        if (fullWidth <= 0.0f || fullHeight <= 0.0f)
            return;

        // Rozmiar plusa skalowany względem szerokości tarczy.
        const float plusSize = fullWidth * 0.16f;

        // Oba plusy znajdują się w górnej części tarczy.
        // Ich pionowe położenie jest identyczne.
        const float plusTop =
            drawArea.top + fullHeight * 0.12f;

        // Niezależne przesunięcia poziome ikon.
        // Health przesuwamy w prawo, a Armor w lewo —
        // obie ikony bliżej krawędzi radardisc.
        const float healthPlusOffsetX = plusSize * 0.50f;
        const float armorPlusOffsetX = plusSize * 0.50f;

        // Większość ikony znajduje się poza obrysem tarczy.
        // Niewielkie zachodzenie na obszar tarczy wizualnie
        // wiąże ikonę z odpowiednią połową wskaźnika.
        const float leftPlusRight =
            drawArea.left + plusSize * 0.15f;

        const float rightPlusLeft =
            drawArea.right - plusSize * 0.15f;

        // Plus zdrowia — lewa strona, przesunięty w prawo.
        if (g_LeedsRadarShowHealthPlus) {
            CRect healthPlusArea(
                leftPlusRight - plusSize + healthPlusOffsetX,
                plusTop,
                leftPlusRight + healthPlusOffsetX,
                plusTop + plusSize
            );

            LeedsHUDDrawPlus(
                healthPlusArea,
                plusHealthR,
                plusHealthG,
                plusHealthB,
                alpha
            );
        }

        // Plus pancerza — prawa strona, przesunięty w lewo.
        // Nie pokazujemy go, gdy prawa połowa tarczy
        // przedstawia tlen zamiast pancerza.
        if (g_LeedsRadarShowArmorPlus && !showBreath) {
            CRect armorPlusArea(
                rightPlusLeft - armorPlusOffsetX,
                plusTop,
                rightPlusLeft + plusSize - armorPlusOffsetX,
                plusTop + plusSize
            );

            LeedsHUDDrawPlus(
                armorPlusArea,
                plusArmorR,
                plusArmorG,
                plusArmorB,
                alpha
            );
        }
    }

    // Rysuje pełną tarczę radaru oraz animowane wskaźniki.
    //
    // Cała tarcza pozostaje widoczna.
    // Lewa połowa: zdrowie.
    // Prawa połowa: pancerz albo tlen.
    static void DrawRadarDisc(const CRect& drawArea)
    {
        if (!mLeedsHudSprite.m_pTexture)
            return;

        float health = 1.0f;
        float armor = 0.0f;
        float breath = 1.0f;

        bool showArmor = false;
        bool showBreath = false;

        unsigned char alpha = 0;

        GetRadarIndicatorData(
            health,
            armor,
            breath,
            showArmor,
            showBreath,
            alpha
        );

        RwRenderStateSet(
            rwRENDERSTATEVERTEXALPHAENABLE,
            (void*)TRUE
        );

        // Półprzezroczyste, niebiesko-granatowe tło całej tarczy.
        // Pozostaje widoczne również wtedy, gdy wskaźniki są wyłączone
        // albo ich wartość wynosi zero.
        mLeedsHudSprite.Draw(
            drawArea,
            CRGBA(41, 70, 84, 180)
        );

        // Bez aktywnych wskaźników pozostawiamy tylko kolorową tarczę.
        if (!g_LeedsRadarIndicatorsEnabled || alpha == 0)
            return;

        // HP — lewa połowa radaru.
        CRGBA healthColor = GetHealthDiscColor(health, alpha);

        DrawLeftIndicator(
            drawArea,
            health,
            healthColor
        );

        // Prawa połowa — tlen ma pierwszeństwo przed pancerzem.
        if (showBreath) {
            CRGBA breathColor = GetBreathDiscColor(breath, alpha);

            DrawRightIndicator(
                drawArea,
                breath,
                breathColor,
                1.4f
            );
        }
        else if (showArmor) {
            CRGBA armorColor = GetArmorDiscColor(armor, alpha);

            DrawRightIndicator(
                drawArea,
                armor,
                armorColor,
                1.4f
            );
        }

        // Plusy rysujemy na końcu, aby pozostały widoczne
        // ponad wypełnieniem tarczy.
        DrawRadarPlusIcons(
            drawArea,
            alpha,
            showBreath
        );
    }

    static std::string GetAsiFolder() {
        char buffer[MAX_PATH]{};
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

    static std::string fetchPath() {
        char pathBuf[MAX_PATH]{};
        HMODULE module = NULL;

        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&isLoaded,
            &module
        );

        GetModuleFileNameA(module, pathBuf, MAX_PATH);

        std::string fullPath(pathBuf);
        size_t pos = fullPath.find_last_of("\\/");

        return fullPath.substr(0, pos) + "\\LeedsHUD\\";
    }

    static bool IsPlayerInVehicle() {
        CVehicle* veh = FindPlayerVehicle(-1, false);
        return veh != nullptr;
    }

    static void ApplyBlipSizePatches() {
        injector::WriteMemory<float*>(0x586047, &g_BlipSize, true);
        injector::WriteMemory<float*>(0x586060, &g_BlipSize, true);

        injector::WriteMemory<float*>(0x584A02, &g_SpriteBlipSize, true);
        injector::WriteMemory<float*>(0x584A1A, &g_SpriteBlipSize, true);
        injector::WriteMemory<float*>(0x584A32, &g_SpriteBlipSize, true);
        injector::WriteMemory<float*>(0x584A42, &g_SpriteBlipSize, true);

        injector::WriteMemory<float*>(0x5886DC, &g_EntityBlipSize, true);
    }

    static void UpdateRadarZoom() {
        static int s_ver = -1;

        if (s_ver == -1) {
            float probe = 0.0f;
            memcpy(&probe, (void*)0x586C9B, sizeof(float));
            s_ver = (probe == 180.0f) ? 10 : 11;
        }

        bool bigPressed =
            (GetAsyncKeyState(gKeyBigRadar) & 0x8000) != 0;

        bool tHeld =
            (GetAsyncKeyState(gKeyZoomOut) & 0x8000) != 0;

        bool inCar = IsPlayerInVehicle();

        static bool s_bigWasDown = false;

        if (bigPressed && !s_bigWasDown) {
            bBigRadarActive = !bBigRadarActive;

            if (bBigRadarActive) {
                bBigFadingOut = false;
                gHudScale = kScaleLarge;
            }
            else {
                bBigFadingOut = true;
                bigFadeStartTime = GetTickCount();
            }
        }

        s_bigWasDown = bigPressed;

        if (bBigFadingOut) {
            DWORD elapsed = GetTickCount() - bigFadeStartTime;

            float progress = std::min(
                1.0f,
                static_cast<float>(elapsed) / 280.0f
            );

            float t = 1.0f - progress;

            gHudScale =
                kScaleDefault +
                t * (kScaleLarge - kScaleDefault);

            if (progress >= 1.0f) {
                gHudScale = kScaleDefault;
                bBigFadingOut = false;
            }
        }

        if (bBigRadarActive) {
            if (inCar)
                gZoomCar = kZoomCarLarge;
            else
                gZoomFoot = kZoomLarge;
        }
        else if (bBigFadingOut) {
            float t =
                (gHudScale - kScaleDefault) /
                (kScaleLarge - kScaleDefault);

            if (inCar) {
                gZoomCar =
                    kZoomCarDefault +
                    t * (kZoomCarLarge - kZoomCarDefault);
            }
            else {
                gZoomFoot =
                    kZoomDefault +
                    t * (kZoomLarge - kZoomDefault);
            }
        }

        if (tHeld && !bBigRadarActive) {
            bTZoomActive = true;
            bTFadingOut = false;

            tZoomStartTime = GetTickCount();

            if (inCar)
                gZoomCar = kZoomCarLarge;
            else
                gZoomFoot = kZoomLarge;
        }

        if (!tHeld && bTZoomActive && !bTFadingOut && !bBigRadarActive) {
            if (GetTickCount() - tZoomStartTime > 5000) {
                bTFadingOut = true;
                tFadeStartTime = GetTickCount();
            }
        }

        if (bTFadingOut && !bBigRadarActive) {
            DWORD elapsed = GetTickCount() - tFadeStartTime;

            float progress = std::min(
                1.0f,
                static_cast<float>(elapsed) / 350.0f
            );

            float t = 1.0f - progress;

            if (inCar) {
                gZoomCar =
                    kZoomCarDefault +
                    t * (kZoomCarLarge - kZoomCarDefault);
            }
            else {
                gZoomFoot =
                    kZoomDefault +
                    t * (kZoomLarge - kZoomDefault);
            }

            if (progress >= 1.0f) {
                if (inCar)
                    gZoomCar = kZoomCarDefault;
                else
                    gZoomFoot = kZoomDefault;

                bTFadingOut = false;
                bTZoomActive = false;
            }
        }

        float currentZoom = inCar ? gZoomCar : gZoomFoot;

        if (s_ver == 10) {
            patch::SetFloat(0x586C9B, currentZoom);
            patch::SetFloat(0x586C7F, currentZoom);
        }
        else {
            patch::SetFloat(0x58746B, currentZoom);
            patch::SetFloat(0x58744F, currentZoom);
        }
    }

    static void initiate() {
        if (isLoaded)
            return;

        txdIndex = CTxdStore::AddTxdSlot("leedshud_internal");

        std::string dir = fetchPath();
        std::string asiPath = GetAsiFolder();

        std::string txdPath = dir + "LeedsHUD.TXD";
        std::string iniPath = asiPath + "LeedsHUD.ini";

        int radarMapAlpha = GetPrivateProfileIntA(
            "Settings",
            "RadarMapAlpha",
            150,
            iniPath.c_str()
        );

        int blipsAlpha = GetPrivateProfileIntA(
            "Settings",
            "BlipsAlpha",
            210,
            iniPath.c_str()
        );

        gKeyZoomOut = GetPrivateProfileIntA(
            "Settings",
            "ZoomOut",
            0x54,
            iniPath.c_str()
        );

        gKeyBigRadar = GetPrivateProfileIntA(
            "Settings",
            "ExpandedRadar",
            0x58,
            iniPath.c_str()
        );

        if (CTxdStore::LoadTxd(txdIndex, txdPath.c_str())) {
            CTxdStore::AddRef(txdIndex);
            CTxdStore::PushCurrentTxd();
            CTxdStore::SetCurrentTxd(txdIndex);

            mLeedsHudSprite.SetTexture((char*)"radardisc");
            mBarFrame.SetTexture((char*)"heightframe");

            pMapDirTex = _RwTextureRead("mapdir", NULL);
            pNorthTex = _RwTextureRead("radar_north", NULL);
            pDestinationTex = _RwTextureRead("destination", NULL);

            CTxdStore::PopCurrentTxd();
            isLoaded = true;
        }

        patch::RedirectCall(0x58AA25, renderHudElement);
        patch::RedirectJump(0x583480, applyHudTranslation);
        patch::RedirectCall(0x58A551, DrawRadarPlane);
        patch::RedirectCall(0x58A649, DrawPlaneHeight);
        patch::RedirectCall(0x58A77A, DrawPlaneHeightBorder);

        patch::Nop(0x58A818, 16);
        patch::Nop(0x58A8C2, 16);
        patch::Nop(0x58A96C, 16);

        patch::SetUChar(0x58694E + 1, TRUE);

        patch::SetUInt(0x586432 + 1, radarMapAlpha);
        patch::SetUInt(0x58647B + 1, radarMapAlpha);
        patch::SetUInt(0x5864BC + 1, radarMapAlpha);

        patch::SetUInt(0x586F34 + 1, blipsAlpha);
        patch::SetUInt(0x587BD1 + 1, blipsAlpha);

        ApplyBlipSizePatches();

        {
            float probe = 0.0f;
            memcpy(&probe, (void*)0x586C9B, sizeof(float));

            bool is10 = (probe == 180.0f);

            DWORD zoomCarAddr = reinterpret_cast<DWORD>(&gZoomCar);
            DWORD zoomAddr = reinterpret_cast<DWORD>(&gZoomFoot);

            if (is10) {
                patch::SetUInt(0x586C66, zoomCarAddr);
                patch::SetUInt(0x586C60, zoomAddr);
            }
            else {
                patch::SetUInt(0x587436, zoomCarAddr);
                patch::SetUInt(0x587430, zoomAddr);
            }
        }

        Events::gameProcessEvent += [] {
            UpdateRadarZoom();
        };
    }

    static void RadarTextures() {
        if (!isLoaded)
            return;

        if (pMapDirTex &&
            CRadar::RadarBlipSprites[2].m_pTexture != pMapDirTex) {
            CRadar::RadarBlipSprites[2].m_pTexture = pMapDirTex;
        }

        if (pNorthTex &&
            CRadar::RadarBlipSprites[4].m_pTexture != pNorthTex) {
            CRadar::RadarBlipSprites[4].m_pTexture = pNorthTex;
        }

        if (pDestinationTex) {
            for (int i = 0; i < 64; i++) {
                RwTexture* tex =
                    CRadar::RadarBlipSprites[i].m_pTexture;

                if (tex && strncmp(tex->name, "radar_waypoint", 14) == 0) {
                    CRadar::RadarBlipSprites[i].m_pTexture =
                        pDestinationTex;
                    break;
                }
            }
        }
    }

    static void __fastcall renderHudElement(
        CSprite2d* self,
        int,
        CRect const& rect,
        CRGBA const& color)
    {
        if (!isLoaded || !mLeedsHudSprite.m_pTexture)
            return;

        RadarTextures();

        const float posX = screenRes(LIVE_POS_X);
        const float posY = screenRes(LIVE_POS_Y);
        const float ring = screenRes(LIVE_RING);

        CRect drawArea(
            posX - ring,
            posY - ring,
            posX + ring,
            posY + ring
        );

        RwRenderStateSet(
            rwRENDERSTATEVERTEXALPHAENABLE,
            (void*)TRUE
        );

        DrawRadarDisc(drawArea);
    }

    static void applyHudTranslation(
        CVector2D* screenOut,
        CVector2D* worldIn)
    {
        if (FrontEndMenuManager.m_bDrawRadarOrMap) {
            screenOut->x =
                FrontEndMenuManager.m_fMapZoom * worldIn->x +
                FrontEndMenuManager.m_fMapBaseX;

            screenOut->y =
                FrontEndMenuManager.m_fMapBaseY -
                FrontEndMenuManager.m_fMapZoom * worldIn->y;
        }
        else {
            screenOut->x =
                screenRes(LIVE_POS_X) +
                (worldIn->x * screenRes(LIVE_BASE));

            screenOut->y =
                screenRes(LIVE_POS_Y) -
                (worldIn->y * screenRes(LIVE_BASE));
        }
    }

    static void __fastcall DrawRadarPlane(
        CSprite2d* sprite,
        int,
        float x1,
        float y1,
        float x2,
        float y2,
        float x3,
        float y3,
        float x4,
        float y4,
        CRGBA const& color)
    {
        if (!sprite || !sprite->m_pTexture)
            return;

        float rollAngle = 0.0f;

        CVehicle* veh = FindPlayerVehicle(-1, false);

        if (veh && veh->m_matrix) {
            float rightZ = std::max(
                -1.0f,
                std::min(1.0f, veh->m_matrix->right.z)
            );

            rollAngle = asinf(rightZ);
        }

        float arrowHalf = screenRes(LIVE_RING * 0.99f);

        float cx = screenRes(LIVE_POS_X);
        float cy = screenRes(LIVE_POS_Y);

        CVector2D posn[4] = {
            { cx - arrowHalf, cy - arrowHalf },
            { cx + arrowHalf, cy - arrowHalf },
            { cx - arrowHalf, cy + arrowHalf },
            { cx + arrowHalf, cy + arrowHalf }
        };

        if (rollAngle != 0.0f) {
            float c = cosf(rollAngle);
            float s = sinf(rollAngle);

            for (int i = 0; i < 4; i++) {
                float dx = posn[i].x - cx;
                float dy = posn[i].y - cy;

                posn[i].x = cx + dx * c - dy * s;
                posn[i].y = cy + dx * s + dy * c;
            }
        }

        RwRenderStateSet(
            rwRENDERSTATEVERTEXALPHAENABLE,
            (void*)TRUE
        );

        sprite->Draw(
            posn[2].x, posn[2].y,
            posn[3].x, posn[3].y,
            posn[0].x, posn[0].y,
            posn[1].x, posn[1].y,
            CRGBA(255, 255, 255, 255)
        );
    }

    static float GetNormalizedPlayerHeight() {
        CPed* playa = FindPlayerPed();
        CVehicle* playaVeh = FindPlayerVehicle(-1, false);

        float pZ =
            playaVeh ? playaVeh->GetPosition().z :
            (playa ? playa->GetPosition().z : 0.0f);

        return std::max(0.0f, std::min(1.0f, pZ / 900.0f));
    }

    static void DrawPlaneHeightBorder(
        CRect const& rect,
        CRGBA const& color)
    {
        if (!mBarFrame.m_pTexture)
            return;

        float x = screenRes(LIVE_POS_X + LIVE_RING + 15.0f);
        float y = screenRes(LIVE_POS_Y);

        float wHalf = screenRes(17.0f * 0.5f * 0.6f);
        float hHalf = screenRes(265.0f * 0.5f * 0.6f);

        mBarFrame.Draw(
            CRect(x - wHalf, y - hHalf, x + wHalf, y + hHalf),
            CRGBA(255, 255, 255, 255)
        );

        float currentY =
            (y + screenRes(258.0f * 0.5f * 0.6f)) -
            (screenRes(258.0f * 0.6f) * GetNormalizedPlayerHeight());

        float barW = screenRes(11.0f * 0.5f * 0.6f);

        CSprite2d::DrawRect(
            CRect(
                x - barW - screenRes(1.5f),
                currentY - screenRes(0.5f),
                x + barW + screenRes(1.5f),
                currentY + screenRes(0.5f)
            ),
            CRGBA(255, 255, 255, 255)
        );
    }

    static void DrawPlaneHeight(
        CRect const& rect,
        CRGBA const& color)
    {
        float x = screenRes(LIVE_POS_X + LIVE_RING + 15.0f);
        float yCenter = screenRes(LIVE_POS_Y);

        float barW = screenRes(11.0f * 0.5f * 0.6f);
        float barHMax = screenRes(258.0f * 0.5f * 0.6f);

        float barBottom = yCenter + barHMax;

        float currentY =
            barBottom -
            ((barHMax * 2.0f) * GetNormalizedPlayerHeight());

        CSprite2d::DrawRect(
            CRect(x - barW, currentY, x + barW, barBottom),
            CRGBA(140, 211, 239, 255)
        );
    }

    static void release() {
        if (txdIndex != -1) {
            mLeedsHudSprite.Delete();
            mBarFrame.Delete();

            CTxdStore::RemoveTxdSlot(txdIndex);

            txdIndex = -1;

            pMapDirTex = nullptr;
            pNorthTex = nullptr;
            pDestinationTex = nullptr;

            isLoaded = false;
        }
    }
};

int LeedsHUD::txdIndex = -1;

class LeedsHUDLoad {
public:
    LeedsHUDLoad() {
        Events::initRwEvent += [] {
            LeedsHUD::initiate();
        };

        Events::shutdownRwEvent += [] {
            LeedsHUD::release();
        };
    }
} leedsradar;
