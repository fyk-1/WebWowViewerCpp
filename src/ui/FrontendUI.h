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
    // The trivia popup: the text drawn as the game's tooltip centred on a world point, from the install's own art and font. An empty
    // text hides it.
    void setFlyoverPopup(const std::string &text, const mathfu::vec3 &at);
    // The zone text: the zone's name, and the subzone's under it, drawn as the game's banner at the top of the screen. A line that is
    // set fades in, holds and fades out; a null one is left as it is, and an empty one is hidden at once.
    void setFlyoverZoneText(const std::string *zone, const std::string *subzone);
private:
    void prepareFonts();
    bool popupArtReady();
    void drawPopup();
    void drawZoneText();

    std::string m_popupText;
    double m_popupShownAt = 0;
    mathfu::vec3 m_popupAt = {0, 0, 0};
    std::shared_ptr<Db2File> m_frizFile;
    std::vector<unsigned char> m_frizData;
    bool m_frizMissing = false;
    ImFont *m_popupFont = nullptr;
    float m_popupFontPx = 0;
    std::string m_zoneText, m_subzoneText;
    double m_zoneShownAt = 0, m_subzoneShownAt = 0;   // -1 until the line's first drawable frame
    ImFont *m_zoneFont = nullptr, *m_subzoneFont = nullptr;
    float m_zoneFontPx = 0, m_subzoneFontPx = 0;
    HBlpTexture m_tooltipBackgroundBlp, m_tooltipBorderBlp;
    HGSamplableTexture m_tooltipBackground, m_tooltipBorder;
    std::shared_ptr<IUIMaterial> m_tooltipBackgroundMat, m_tooltipBorderMat;

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
