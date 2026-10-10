//
// Created by deamon on 20.12.19.
//

#ifndef AWEBWOWVIEWERCPP_FRONTENDUI_H
#define AWEBWOWVIEWERCPP_FRONTENDUI_H

#include <GLFW/glfw3.h>

#include "imguiLib/imgui.h"
#include "StorageNotifications.h"
#include "../../wowViewerLib/src/engine/objects/iScene.h"
#include "../persistance/CascRequestProcessor.h"
#include "../database/buildInfoParser/buildDefinition.h"
#include "renderer/uiScene/IFrontendUIBufferCreate.h"
#include "renderer/uiScene/FrontendUIRenderer.h"
#include "../../wowViewerLib/src/renderer/mapScene/MapSceneRenderer.h"
#include "childWindow/sceneWindow/SceneWindow.h"
#include "childWindow/cascStorageDialog/CascStorageDialog.h"


class FrontendUI : public IScene, public std::enable_shared_from_this<FrontendUI> {
public:
    void createDatabaseHandler();

    // The client database is `databaseFile`, `./export.db3` unless the caller names another.
    FrontendUI(HApiContainer api, const std::string &databaseFile = "./export.db3");
    ~FrontendUI() override {
        ImGui::DestroyContext(this->imguiContext);
    };

    HFrameScenario createFrameScenario(int canvWidth, int canvHeight, double deltaTime);

    void setUIScale(float scale) {
        uiScale = scale;
    }
    int getWindowWidth() const {
        return windowWidth;
    }
    int getWindowHeight() const {
        return windowHeight;
    }
    void setWindowSize(int width, int height) {
        windowWidth = width;
        windowHeight = height;
    }

    std::shared_ptr<SceneWindow> getCurrentActiveScene() {
        return m_currentActiveScene;
    };

    // Flyover mode: no ImGui chrome, the background scene is the whole window. Set before initImgui.
    void setFlyoverMode(bool value) { m_flyoverMode = value; }
    std::shared_ptr<SceneWindow> getBackgroundScene() { return m_backgroundScene; }
    HApiContainer getApi() { return m_api; }
    bool openFirstStorage();
    // Opens the build in the folder, as the storage dialog does for a folder the user picked.
    bool openBuild(const std::string &cascPath, const BuildDefinition &buildDef);
    void composeFlyoverUI();
private:
    bool m_flyoverMode = false;
    std::string m_databaseFile;
    std::shared_ptr<StorageNotifications> m_storageNotifications = std::make_shared<StorageNotifications>();
    ImGuiContext* imguiContext = nullptr;
    std::shared_ptr<FrontendUIRenderer> m_uiRenderer;

    std::shared_ptr<IUIMaterial> fontMat;

    std::shared_ptr<SceneWindow> m_backgroundScene = nullptr;
    std::shared_ptr<SceneWindow> m_currentActiveScene = nullptr;
    std::weak_ptr<SceneWindow> m_lastActiveScene;

    float uiScale = 1;

    bool tryOpenCasc(std::string &cascPath, BuildDefinition &buildDef);

    int windowWidth = 640;
    int windowHeight = 480;

    std::string m_currentProduct = "";
    bool m_currentIsClassic = false;

    HApiContainer m_api;

    std::shared_ptr<CascStorageDialog> m_cascStorageDialog = nullptr;

public:
    void initImgui(GLFWwindow *window);
    void shutDown();

    void newFrame();

    bool getStopMouse();
    bool getStopKeyboard();

    void createFontTexture();
};


#endif //AWEBWOWVIEWERCPP_FRONTENDUI_H
