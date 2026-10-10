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

    m_backgroundScene = std::make_shared<SceneWindow>(api, true, m_uiRenderer, false);

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

    ImGui::Render();
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
