//
// Created by deamon on 20.12.19.
//

// Include ImGui internals before project macros (e.g. OFFSET).
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "FrontendUI.h"

#include "imguiLib/imguiImpl/imgui_impl_glfw.h"

#include <iostream>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <mathfu/glsl_mappings.h>
#include "../persistance/CascRequestProcessor.h"
#include "../database/CSqliteDB.h"
#include "../database/CEmptySqliteDB.h"
#include "renderer/uiScene/FrontendUIRendererFactory.h"
#include "../../wowViewerLib/src/gapi/interface/FrameContext.h"
#include "../../wowViewerLib/src/renderer/frame/FrameProfile.h"
#include "../../3rdparty/filesystem_impl/include/ghc/filesystem.hpp"

FrontendUI::FrontendUI(HApiContainer api, const std::string &databaseFile) {
    m_api = api;
    m_databaseFile = databaseFile;

    this->createDatabaseHandler();
    m_uiRenderer = FrontendUIRendererFactory::createForwardRenderer(m_api->hDevice);

    m_backgroundScene = std::make_shared<SceneWindow>(api, true, m_uiRenderer);

    // Load the saved CASC storages, with the callback that opens one
    m_cascStorageDialog = std::make_shared<CascStorageDialog>(
        m_api,
        [this](const std::string& cascPath, BuildDefinition& buildDef) -> bool {
            std::string path = cascPath;
            if (tryOpenCasc(path, buildDef)) {
                return true;
            } else {
                return false;
            }
        }
    );
}

void FrontendUI::composeFlyoverUI() {
    FrameContext::setCurrentProcessingFrameNumber(m_api->hDevice->getFrameNumber());

    // The scene is drawn under the ImGui layer, so an empty frame leaves only the scene.
    m_currentActiveScene = m_backgroundScene;
    m_lastActiveScene = m_currentActiveScene;

    drawOsdText();
    drawPopup();
    ImGui::Render();
}

/*
robots@Impl: The trivia popup.

The host names the text and a world point (`popup` on stdin, FlyoverMode) and this draws it as the game's tooltip, centred where
the point falls on screen in the last frame's camera, and nothing while the point is behind the camera. The art is the install's own: the
backdrop UI-Tooltip-Background, the edge UI-Tooltip-Border and the font Friz Quadrata, asked for the first time a popup is set; nothing
draws until all three have loaded. Sizes are in the game's UI units, the window height over 768, as the client scales its interface:
the text is GameTooltipText's 12, the edge 16 with the backdrop inset 5, and the text 10 in from the frame and wrapped at kWrap. The set
line sits a blank line under the text, wrapped at the same width, and never scrolls. The edge file is the classic backdrop
strip of eight square cells, left, right, top, bottom, then the corners top left, top right, bottom left and bottom right, the top and
bottom cells stored turned a quarter. The backdrop takes TOOLTIP_DEFAULT_BACKGROUND_COLOR, the text HIGHLIGHT_FONT_COLOR and the set line
NORMAL_FONT_COLOR, as the client colours a tooltip's lines and its item set name, each with the font's shadow one unit down and right.

Text taller than kMaxText scrolls up at kScroll units a second, holds kHold seconds at each end and starts again from the top. The fonts
are rasterised at the window's own size, so the atlas is built again when the window's height changes, before ImGui's frame, which
locks the atlas while it runs. Friz Quadrata is asked for the first time a popup or a zone text is set.

robots@Impl: The on-screen text.

The host decides which line shows and when (`osd_text` on stdin, FlyoverMode), the zone names or a countdown; this draws it as the classic client's ZoneText.xml and
ZoneText.lua do, read from the install. The zone line is ZoneTextFont, Friz Quadrata 32 with a thick outline, centred 192 units down
the screen (its 128-unit frame anchored 128 down); the subzone line is SubZoneTextFont, 26, its top at the zone line's bottom, which
keeps its place while the zone line is faded out. Both are 512 units wide, wrapped and centred, and take the colour the client gives a
zone with no PvP state. A held line stays at full opacity. Each other line fades in for kFadeIn seconds from the first frame it can be drawn, so the font's first load does not
eat it, holds kHoldZone and fades out for kFadeOut, the
client's ZoneFadeInDuration, ZoneHoldDuration and ZoneFadeOutDuration. The thick outline is the text drawn kOutlineSteps times in black
around a circle of kOutline units, then once in colour; the client's FreeType stroke has no ImGui counterpart.
*/
namespace {
    constexpr int kTooltipBackground = 137056;
    constexpr int kTooltipBorder = 137057;
    constexpr int kFriz = 615960;
    constexpr float kUiHeight = 768.0f;
    constexpr float kFontSize = 12.0f;
    constexpr float kEdge = 16.0f;
    constexpr float kInset = 5.0f;
    constexpr float kPad = 10.0f;
    constexpr float kWrap = 260.0f;
    constexpr float kMaxText = 300.0f;
    constexpr float kScroll = 20.0f;
    constexpr double kHold = 3.0;

    constexpr float kZoneSize = 32.0f;
    constexpr float kSubzoneSize = 26.0f;
    constexpr float kZoneCentre = 192.0f;
    constexpr float kZoneWidth = 512.0f;
    constexpr float kOutline = 2.0f;
    constexpr int kOutlineSteps = 16;
    constexpr double kFadeIn = 0.5;
    constexpr double kHoldZone = 1.0;
    constexpr double kFadeOut = 2.0;
    const ImVec4 kZoneColour(1.0f, 0.9294f, 0.7607f, 1.0f);

    // The opacity of a zone text line `t` seconds after it was set.
    float zoneFade(double t) {
        if (t < 0) return 0;
        if (t < kFadeIn) return (float) (t / kFadeIn);
        t -= kFadeIn;
        if (t < kHoldZone) return 1;
        t -= kHoldZone;
        return t < kFadeOut ? (float) (1 - t / kFadeOut) : 0;
    }

    // The text wrapped at `width` and each line centred on `x`, the first line's top at `top`; draws nothing without a draw list.
    // Returns the block's height.
    float centredLines(ImDrawList *draw, ImFont *font, float px, const std::string &text, float x, float top, float width,
                       float outline, float alpha) {
        const char *at = text.c_str();
        const char *end = at + text.size();
        float y = top;
        while (at < end) {
            const char *cut = font->CalcWordWrapPositionA(px / font->FontSize, at, end, width);
            if (cut == at) cut = at + 1;
            const char *lineEnd = cut;
            while (lineEnd > at && (lineEnd[-1] == ' ' || lineEnd[-1] == '\n')) lineEnd--;
            if (draw != nullptr && lineEnd > at) {
                const float w = font->CalcTextSizeA(px, FLT_MAX, 0, at, lineEnd).x;
                const ImVec2 pos(std::round(x - w / 2), std::round(y));
                const ImU32 black = ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, alpha));
                for (int i = 0; i < kOutlineSteps; i++) {
                    const float a = 2 * 3.14159265f * i / kOutlineSteps;
                    draw->AddText(font, px, ImVec2(pos.x + outline * std::cos(a), pos.y + outline * std::sin(a)), black, at, lineEnd);
                }
                ImVec4 colour = kZoneColour;
                colour.w = alpha;
                draw->AddText(font, px, pos, ImGui::ColorConvertFloat4ToU32(colour), at, lineEnd);
            }
            y += px;
            at = cut;
            while (at < end && (*at == ' ' || *at == '\n')) at++;
        }
        return y - top;
    }
}

void FrontendUI::setFlyoverPopup(const std::string &text, const std::string &set, const mathfu::vec3 &at) {
    if (text != m_popupText) m_popupShownAt = ImGui::GetTime();
    m_popupText = text;
    m_popupSet = set;
    m_popupAt = at;
}

void FrontendUI::setFlyoverOsdText(const std::string *text, const std::string *subtext, bool hold) {
    m_osdHold = hold;
    if (text != nullptr) {
        m_osdText = *text;
        m_osdShownAt = -1;
    }
    if (subtext != nullptr) {
        m_osdSubtext = *subtext;
        m_osdSubShownAt = -1;
    }
}

void FrontendUI::prepareFonts() {
    if ((m_popupText.empty() && m_osdText.empty() && m_osdSubtext.empty()) || m_frizMissing) return;
    auto storage = m_api->cacheStorage;
    if (storage == nullptr) return;

    if (m_frizFile == nullptr) m_frizFile = storage->getDb2Cache()->getFileId(kFriz);
    if (m_frizFile->getStatus() == FileStatus::FSRejected) {
        std::cerr << "flyover: the install has no Friz Quadrata (FileDataID " << kFriz << ")" << std::endl;
        m_frizMissing = true;
        return;
    }
    if (m_frizFile->getStatus() != FileStatus::FSLoaded) return;

    ImGuiIO &io = ImGui::GetIO();
    const float unit = io.DisplaySize.y / kUiHeight;
    const float popupPx = std::round(kFontSize * unit), zonePx = std::round(kZoneSize * unit), subzonePx = std::round(kSubzoneSize * unit);
    if (popupPx < 1 || (m_popupFont != nullptr && popupPx == m_popupFontPx && zonePx == m_zoneFontPx && subzonePx == m_subzoneFontPx)) return;

    if (m_frizData.empty()) {
        auto content = m_frizFile->getContent();
        m_frizData.assign(content->begin(), content->end());
    }
    io.Fonts->Clear();
    io.Fonts->AddFontDefault();
    auto add = [&](float px) {
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        return io.Fonts->AddFontFromMemoryTTF(m_frizData.data(), (int) m_frizData.size(), px, &config);
    };
    m_popupFontPx = popupPx;
    m_popupFont = add(popupPx);
    m_zoneFontPx = zonePx;
    m_zoneFont = add(zonePx);
    m_subzoneFontPx = subzonePx;
    m_subzoneFont = add(subzonePx);
    createFontTexture();
}

void FrontendUI::drawOsdText() {
    if (m_zoneFont == nullptr || m_subzoneFont == nullptr) return;
    const double now = ImGui::GetTime();
    if (m_osdShownAt < 0) m_osdShownAt = now;
    if (m_osdSubShownAt < 0) m_osdSubShownAt = now;
    auto alpha = [&](const std::string &line, double shownAt) { return line.empty() ? 0.0f : m_osdHold ? 1.0f : zoneFade(now - shownAt); };
    const float zoneAlpha = alpha(m_osdText, m_osdShownAt);
    const float subzoneAlpha = alpha(m_osdSubtext, m_osdSubShownAt);
    if (zoneAlpha <= 0 && subzoneAlpha <= 0) return;

    const ImGuiIO &io = ImGui::GetIO();
    const float unit = io.DisplaySize.y / kUiHeight;
    const float x = io.DisplaySize.x / 2, width = kZoneWidth * unit, outline = kOutline * unit;
    ImDrawList *draw = ImGui::GetForegroundDrawList();

    const float zoneHeight = centredLines(nullptr, m_zoneFont, m_zoneFontPx, m_osdText, x, 0, width, 0, 0);
    const float zoneTop = kZoneCentre * unit - zoneHeight / 2;
    if (zoneAlpha > 0) centredLines(draw, m_zoneFont, m_zoneFontPx, m_osdText, x, zoneTop, width, outline, zoneAlpha);
    if (subzoneAlpha > 0) centredLines(draw, m_subzoneFont, m_subzoneFontPx, m_osdSubtext, x, zoneTop + zoneHeight, width, outline, subzoneAlpha);
}

bool FrontendUI::popupArtReady() {
    auto storage = m_api->cacheStorage;
    if (storage == nullptr || m_popupFont == nullptr) return false;

    auto prepare = [&](int fileDataId, HBlpTexture &blp, HGSamplableTexture &texture, std::shared_ptr<IUIMaterial> &material) {
        if (material != nullptr) return true;
        if (blp == nullptr) blp = storage->getTextureCache()->getFileId(fileDataId);
        if (texture == nullptr) texture = m_api->hDevice->createBlpTexture(blp, false, false);
        if (!texture->getTexture()->getIsLoaded()) return false;
        material = m_uiRenderer->createUIMaterial(texture);
        return true;
    };
    const bool background = prepare(kTooltipBackground, m_tooltipBackgroundBlp, m_tooltipBackground, m_tooltipBackgroundMat);
    const bool border = prepare(kTooltipBorder, m_tooltipBorderBlp, m_tooltipBorder, m_tooltipBorderMat);
    return background && border;
}

void FrontendUI::drawPopup() {
    if (m_popupText.empty() || !popupArtReady()) return;

    auto plan = m_backgroundScene ? m_backgroundScene->getLastPlan() : nullptr;
    if (plan == nullptr || plan->renderingMatrices == nullptr) return;
    const mathfu::vec4 clip = plan->renderingMatrices->perspectiveMat * plan->renderingMatrices->lookAtMat * mathfu::vec4(m_popupAt, 1.0f);
    if (clip.w <= 0) return;

    const ImGuiIO &io = ImGui::GetIO();
    const ImVec2 anchor((clip.x / clip.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - clip.y / clip.w * 0.5f) * io.DisplaySize.y);
    const float unit = io.DisplaySize.y / kUiHeight;
    const float pad = kPad * unit, edge = kEdge * unit, inset = kInset * unit;

    const ImVec2 text = m_popupFont->CalcTextSizeA(m_popupFontPx, FLT_MAX, kWrap * unit, m_popupText.c_str());
    const float shown = (std::min)(text.y, kMaxText * unit);
    const ImVec2 set = m_popupSet.empty() ? ImVec2(0, 0) : m_popupFont->CalcTextSizeA(m_popupFontPx, FLT_MAX, kWrap * unit, m_popupSet.c_str());
    const float setGap = m_popupSet.empty() ? 0 : m_popupFontPx;
    const float width = (std::max)(text.x, set.x);
    const ImVec2 size(width + 2 * pad, shown + setGap + set.y + 2 * pad);
    const ImVec2 lo(anchor.x - size.x / 2, anchor.y - size.y / 2);
    const ImVec2 hi(anchor.x + size.x / 2, anchor.y + size.y / 2);

    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const ImTextureID background = m_tooltipBackgroundMat->uniqueId;
    const ImTextureID border = m_tooltipBorderMat->uniqueId;

    draw->AddImage(background, ImVec2(lo.x + inset, lo.y + inset), ImVec2(hi.x - inset, hi.y - inset),
                   ImVec2(0, 0), ImVec2(1, 1), IM_COL32(23, 23, 48, 255));

    auto cell = [](int i) { return i / 8.0f; };
    draw->AddImage(border, ImVec2(lo.x, lo.y + edge), ImVec2(lo.x + edge, hi.y - edge), ImVec2(cell(0), 0), ImVec2(cell(1), 1));
    draw->AddImage(border, ImVec2(hi.x - edge, lo.y + edge), ImVec2(hi.x, hi.y - edge), ImVec2(cell(1), 0), ImVec2(cell(2), 1));
    draw->AddImageQuad(border,
                       ImVec2(lo.x + edge, lo.y), ImVec2(hi.x - edge, lo.y), ImVec2(hi.x - edge, lo.y + edge), ImVec2(lo.x + edge, lo.y + edge),
                       ImVec2(cell(2), 1), ImVec2(cell(2), 0), ImVec2(cell(3), 0), ImVec2(cell(3), 1));
    draw->AddImageQuad(border,
                       ImVec2(lo.x + edge, hi.y - edge), ImVec2(hi.x - edge, hi.y - edge), ImVec2(hi.x - edge, hi.y), ImVec2(lo.x + edge, hi.y),
                       ImVec2(cell(3), 1), ImVec2(cell(3), 0), ImVec2(cell(4), 0), ImVec2(cell(4), 1));
    draw->AddImage(border, lo, ImVec2(lo.x + edge, lo.y + edge), ImVec2(cell(4), 0), ImVec2(cell(5), 1));
    draw->AddImage(border, ImVec2(hi.x - edge, lo.y), ImVec2(hi.x, lo.y + edge), ImVec2(cell(5), 0), ImVec2(cell(6), 1));
    draw->AddImage(border, ImVec2(lo.x, hi.y - edge), ImVec2(lo.x + edge, hi.y), ImVec2(cell(6), 0), ImVec2(cell(7), 1));
    draw->AddImage(border, ImVec2(hi.x - edge, hi.y - edge), hi, ImVec2(cell(7), 0), ImVec2(cell(8), 1));

    float scrolled = 0;
    const float overflow = text.y - shown;
    if (overflow > 0) {
        const double run = overflow / (kScroll * unit);
        const double t = std::fmod(ImGui::GetTime() - m_popupShownAt, kHold + run + kHold);
        scrolled = (float) std::clamp((t - kHold) * kScroll * unit, 0.0, (double) overflow);
    }

    const ImVec2 origin(lo.x + pad, lo.y + pad);
    draw->PushClipRect(origin, ImVec2(origin.x + text.x + unit, origin.y + shown), true);
    const ImVec2 at(origin.x, origin.y - scrolled);
    draw->AddText(m_popupFont, m_popupFontPx, ImVec2(at.x + unit, at.y + unit), IM_COL32(0, 0, 0, 255), m_popupText.c_str(), nullptr, kWrap * unit);
    draw->AddText(m_popupFont, m_popupFontPx, at, IM_COL32(255, 255, 255, 255), m_popupText.c_str(), nullptr, kWrap * unit);
    draw->PopClipRect();

    if (!m_popupSet.empty()) {
        const ImVec2 setAt(origin.x, origin.y + shown + setGap);
        draw->AddText(m_popupFont, m_popupFontPx, ImVec2(setAt.x + unit, setAt.y + unit), IM_COL32(0, 0, 0, 255), m_popupSet.c_str(), nullptr, kWrap * unit);
        draw->AddText(m_popupFont, m_popupFontPx, setAt, IM_COL32(255, 209, 0, 255), m_popupSet.c_str(), nullptr, kWrap * unit);
    }
}

bool FrontendUI::openFirstStorage() {
    return m_cascStorageDialog && m_cascStorageDialog->openFirstStorage();
}

bool FrontendUI::openBuild(const std::string &cascPath, const BuildDefinition &buildDef) {
    return m_cascStorageDialog && m_cascStorageDialog->openBuild(cascPath, buildDef);
}


void FrontendUI::initImgui(GLFWwindow *window) {

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    this->imguiContext = ImGui::CreateContext();

    // No imgui.ini: the flyover draws no chrome.
    ImGui::GetIO().IniFilename = nullptr;

    ImGuiIO &io = ImGui::GetIO();
    (void) io;
//    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForVulkan(window, true);

    this->createFontTexture();
}

void FrontendUI::newFrame() {
    prepareFonts();

//    ImGui_ImplOpenGL3_NewFrame();

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    io.uiScale = this->uiScale;
    io.DisplaySize = ImVec2((float)io.DisplaySize.x / io.uiScale, (float)io.DisplaySize.y / io.uiScale);
}

void FrontendUI::shutDown() {
    ImGui_ImplGlfw_Shutdown();
}

bool FrontendUI::getStopMouse() {
    return ImGui::GetIO().WantCaptureMouse;
}

bool FrontendUI::getStopKeyboard() {
    ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}

HFrameScenario FrontendUI::createFrameScenario(int canvWidth, int canvHeight, double deltaTime) {
    ZoneScoped ;
    auto l_device = m_api->hDevice;
    auto processingFrame = l_device->getFrameNumber();
    std::function<uint32_t()> updateFrameNumberLambda = [l_device, frame = processingFrame]() -> uint32_t {
        FrameContext::setCurrentProcessingFrameNumber(frame);
        return frame;
    };

    HFrameScenario scenario = std::make_shared<HFrameScenario::element_type>();
    {
        ViewPortDimensions dimension = {
            {0,     0},
            {static_cast<unsigned int>(canvWidth), static_cast<unsigned int>(canvHeight)}
        };
        // The scene renders at renderScale of the window, and the final pass scales it to the swapchain.
        // The ImGui layer keeps the full size.
        // With FSR on and the scale at most 1 it is the FSR render to display ratio; above 1 the scene is shrunk instead.
        float renderScale = std::clamp(m_api->getConfig()->renderScale, 0.05f, 1.5f);
        ViewPortDimensions sceneDimension = {
            {0, 0},
            {std::max(1u, static_cast<unsigned int>(canvWidth * renderScale)),
             std::max(1u, static_cast<unsigned int>(canvHeight * renderScale))}
        };
        m_backgroundScene->setViewPortDimensions(sceneDimension);

        m_backgroundScene->render(
            deltaTime, m_api->getConfig()->fov, scenario,
            /*
            m_debugRenderView,
            debugWidth,
            debugHeight
             */ nullptr,
            updateFrameNumberLambda
        );

        auto uiFrameInput = std::make_shared<FrameInputParams<ImGuiFramePlan::ImGUIParam>>();
        uiFrameInput->delta = deltaTime * (1000.0f);
        uiFrameInput->frameParameters = std::make_shared<ImGuiFramePlan::ImGUIParam>( ImGui::GetDrawData(), dimension);

        scenario->cullFunctions.push_back(m_uiRenderer->createCullUpdateRenderChain(uiFrameInput, updateFrameNumberLambda));
    }

    return scenario;
}

bool FrontendUI::tryOpenCasc(std::string &cascPath, BuildDefinition &buildDef) {
    HRequestProcessor newProcessor = nullptr;
    std::shared_ptr<WoWFilesCacheStorage> newStorage = nullptr;

    try {
        auto processor = std::make_shared<CascRequestProcessor>(cascPath, buildDef,
            [notifications = m_storageNotifications](const StorageError& error) {
                notifications->push(error);
            });
        if (!processor->isOpen()) return false;
        newProcessor = std::move(processor);
        newStorage = std::make_shared<WoWFilesCacheStorage>(newProcessor.get());
    } catch (const std::exception& error) {
        m_storageNotifications->push({StorageErrorCategory::StorageOpen,
                                  std::string("Opening storage: ") + error.what()});
        return false;
    } catch (...) {
        m_storageNotifications->push({StorageErrorCategory::StorageOpen, "Opening storage: unknown error"});
        return false;
    }

    m_api->cacheStorage = newStorage;
    m_api->requestProcessor = newProcessor;

    m_currentProduct = buildDef.productName;
    m_currentIsClassic = buildDef.isClassic;

    return true;
}

inline bool fileExistsNotNull (const std::string& name) {
#ifdef ANDROID
    return false;
#endif

    ghc::filesystem::path p{name};
    std::error_code errorCode;

    bool fileExists = exists(p,errorCode) && ghc::filesystem::file_size(p) > 10;
    if (errorCode) {
        std::cout << "errorCode = " << errorCode.message() << std::endl;
    }

    return fileExists;
}

void FrontendUI::createDatabaseHandler() {
    bool useEmptyDatabase = false;
    if (fileExistsNotNull(m_databaseFile)) {
        try{
            m_api->databaseHandler = std::make_shared<CSqliteDB>(m_databaseFile);
        } catch(std::exception const& e) {
            std::cout << "Failed to open database: " << e.what() << std::endl;
            useEmptyDatabase = true;
        } catch(...) {
            std::cout << "Exception occurred" << std::endl;
        }
    }

    if (useEmptyDatabase) {
        m_api->databaseHandler = std::make_shared<CEmptySqliteDB>();
    }
    if (m_api->databaseHandler != nullptr)
        m_api->getConfig()->classicFog = m_api->databaseHandler->usesClassicFog();
}

void FrontendUI::createFontTexture() {
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bit (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.
    // Upload texture to graphics system

    // Store our identifier
    fontMat = this->m_uiRenderer->createUIMaterial({this->m_uiRenderer->uploadFontTexture(pixels, width, height)});
    io.Fonts->TexID = fontMat->uniqueId;
}
