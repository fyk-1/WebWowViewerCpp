#define _AMD64_ 1

//#define NOWINBASEINTERLOCK

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _WIN32
 #include <errhandlingapi.h>
 #include "flyover/CrashStack.h"
#endif

 
//#define __EMSCRIPTEN__

#ifdef LINK_VULKAN
#define GLFW_INCLUDE_VULKAN
#define VK_NO_PROTOTYPES
#endif


#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <csignal>
#include <exception>
#include <algorithm>
#include <filesystem>
#include <GLFW/glfw3.h>
#include <sqlite3.h>

#include "../wowViewerLib/src/gapi/interface/IDevice.h"
#include "../wowViewerLib/src/gapi/IDeviceFactory.h"
#include "ui/FrontendUI.h"
#include "../wowViewerLib/src/renderer/frame/SceneComposer.h"
#include "../wowViewerLib/src/engine/camera/firstPersonCamera.h"
#include "../wowViewerLib/src/engine/objects/scenes/map.h"
#include "database/CEmptySqliteDB.h"
#include "flyover/FlyoverMode.h"
#ifndef SKIP_VULKAN
#include "../wowViewerLib/src/gapi/vulkan/GDeviceVulkan.h"
#endif
#include <exception>
#ifdef WIN32
#include <timeapi.h>
#endif
#ifdef LINK_TRACY
#include "Tracy.hpp"
#endif

int mleft_pressed = 0;
int mright_pressed = 0;
double m_x = 0.0;
double m_y = 0.0;
double mright_press_x = 0.0;
double mright_press_y = 0.0;

bool stopMouse = false;
bool stopKeyboard = false;

std::shared_ptr<FrontendUI> frontendUI = nullptr;
// Set in the --flyover mode, where F toggles between the path camera and the free camera and F12 asks the host for a debug dump.
FlyoverMode *flyoverMode = nullptr;

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos){
    if (stopMouse) {
        mleft_pressed = 0;
        mright_pressed = 0;
        return;
    };
    HApiContainer apiContainer = *(HApiContainer *)glfwGetWindowUserPointer(window);
    auto currentActiveScene = frontendUI->getCurrentActiveScene();
    auto controllable = currentActiveScene ? currentActiveScene->getCamera() : nullptr;

    if (!controllable) {
        mleft_pressed = 0;
        mright_pressed = 0;
        return;
    };

//    if (!pointerIsLocked) {
        if (mleft_pressed == 1) {
            controllable->addHorizontalViewDir((xpos - m_x) / 4.0f);
            controllable->addVerticalViewDir((ypos - m_y) / 4.0f);

            m_x = xpos;
            m_y = ypos;
        } else if (mright_pressed == 1) {
            controllable->addCameraViewOffset((xpos - m_x) / 8.0f, -(ypos - m_y) / 8.0f);

            m_x = xpos;
            m_y = ypos;
        }

    // Continuous selection: while Ctrl is held, request a pick at the cursor on every move,
    // instead of requiring a right-click.
    bool ctrlHeld = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    if (ctrlHeld && apiContainer->getConfig()->enableObjectPicking && currentActiveScene) {
        currentActiveScene->requestPick((int)xpos, (int)ypos);
    }
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
//    addCameraViewOffset
    if (stopMouse) return;

    HApiContainer apiContainer = *(HApiContainer *)glfwGetWindowUserPointer(window);
    auto currentActiveScene = frontendUI->getCurrentActiveScene();
    auto controllable = currentActiveScene ? currentActiveScene->getCamera() : nullptr;

    if (!controllable) {
        mleft_pressed = 0;
        mright_pressed = 0;
        return;
    };

    if (action == GLFW_PRESS) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            mleft_pressed = 1;
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            mright_pressed = 1;
        }

        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        m_x = xpos;
        m_y = ypos;

        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            mright_press_x = xpos;
            mright_press_y = ypos;
        }
//        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
    if (action == GLFW_RELEASE) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            mleft_pressed = 0;
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            mright_pressed = 0;

            // Treat a right-click with negligible movement (not a camera-pan drag) as an object-pick request.
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            const double clickMoveThreshold = 3.0;
            if (std::abs(xpos - mright_press_x) < clickMoveThreshold && std::abs(ypos - mright_press_y) < clickMoveThreshold) {
                if (apiContainer->getConfig()->enableObjectPicking && currentActiveScene) {
                    currentActiveScene->requestPick((int)xpos, (int)ypos);
                }
            }
        }
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (flyoverMode && key == GLFW_KEY_F && action == GLFW_PRESS) {
        flyoverMode->requestToggleMode();
        return;
    }
    if (flyoverMode && key == GLFW_KEY_F12 && action == GLFW_PRESS) {
        flyoverMode->requestDump();
        return;
    }

    HApiContainer apiContainer = *(HApiContainer *)glfwGetWindowUserPointer(window);
    auto currentActiveScene = frontendUI->getCurrentActiveScene();
    auto controllable = currentActiveScene ? currentActiveScene->getCamera() : nullptr;

    if (!controllable) return;

    if ( action == GLFW_PRESS) {
        if (stopKeyboard) return;
        switch (key) {
            case 'W' :
                controllable->startMovingForward();
                break;
            case 'S' :
                controllable->startMovingBackwards();
                break;
            case 'A' :
                controllable->startStrafingLeft();
                break;
            case 'D':
                controllable->startStrafingRight();
                break;
            case 'Q':
                controllable->startMovingUp();
                break;
            case 'E':
                controllable->startMovingDown();
                break;

            default:
                break;
        }
    } else if ( action == GLFW_RELEASE) {
        switch (key) {
            case 'W' :
                controllable->stopMovingForward();
                break;
            case 'S' :
                controllable->stopMovingBackwards();
                break;
            case 'A' :
                controllable->stopStrafingLeft();
                break;
            case 'D':
                controllable->stopStrafingRight();
                break;
            case 'Q':
                controllable->stopMovingUp();
                break;
            case 'E':
                controllable->stopMovingDown();
                break;
            default:
                break;
        }
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    auto currentActiveScene = frontendUI->getCurrentActiveScene();
    auto controllable = currentActiveScene ? currentActiveScene->getCamera() : nullptr;

    if (!controllable) return;

    HApiContainer apiContainer = *(HApiContainer *)glfwGetWindowUserPointer(window);

    controllable->zoomInFromMouseScroll(-yoffset/2.0f);
}

int canvWidth = 640;
int canvHeight = 480;

int windowWidth = canvWidth;
int windowHeight = canvHeight;
bool windowSizeChanged = false;

void window_size_callback(GLFWwindow* window, int width, int height)
{
    windowWidth = width;
    windowHeight = height;

    glfwGetFramebufferSize(window, &canvWidth, &canvHeight);

    if (frontendUI != nullptr) {
        frontendUI->setWindowSize(windowWidth, windowHeight);
    }

    windowSizeChanged = true;
}

void beforeCrash(void);




extern "C" void my_function_to_handle_aborts(int signal_number)
{
    /*Your code goes here. You can output debugging info.
      If you return from this function, and it was called
      because abort() was called, your program will exit or crash anyway
      (with a dialog box on Windows).
     */

#ifdef _WIN32
    // A fault on the main thread reaches here through the CRT's SEH filter, which keeps the original exception for the
    // signal handler: walk that, since the breakpoint below only shows this handler.
    fprintf(stderr, "crash: signal %d\n", signal_number);
    if (auto info = (EXCEPTION_POINTERS *) *__pxcptinfoptrs()) printCrashStack(info);
#endif
    std::cout << "HELLO" << std::endl;
    __debugbreak();
}

/*Do this early in your program's initialization */


#ifdef _WIN32
void beforeCrash() {
    // An uncaught exception lands here through std::terminate: name it before the breakpoint the handler below walks.
    if (auto pending = std::current_exception()) {
        try { std::rethrow_exception(pending); }
        catch (const std::exception &e) { fprintf(stderr, "crash: uncaught exception: %s\n", e.what()); }
        catch (...) { fputs("crash: uncaught exception of an unknown type\n", stderr); }
    }
    std::cout << "HELLO" << std::endl;
    __debugbreak();
}

static LONG WINAPI windows_exception_handler(EXCEPTION_POINTERS * ExceptionInfo)
{
    switch(ExceptionInfo->ExceptionRecord->ExceptionCode)
    {
        case EXCEPTION_ACCESS_VIOLATION:
            fputs("Error: EXCEPTION_ACCESS_VIOLATION\n", stderr);
            printCrashStack(ExceptionInfo);
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            fputs("Error: EXCEPTION_ARRAY_BOUNDS_EXCEEDED\n", stderr);
            break;
        case EXCEPTION_BREAKPOINT:
            fputs("Error: EXCEPTION_BREAKPOINT\n", stderr);
            printCrashStack(ExceptionInfo);
            break;
    }
	return 0;
}
#endif


double currentFrame;
double lastFrame;

int main(int argc, char *argv[]) {
    // Lists the Vulkan devices and exits, before any window or device exists.
#ifndef SKIP_VULKAN
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--list-gpus") return GDeviceVLK::listPhysicalDevices(std::cout);
    }
#endif

    std::string flyoverRouteFile;
    const bool flyoverRequested = FlyoverMode::findArgument(argc, argv, flyoverRouteFile);
    FlyoverInstall flyoverInstall;
    std::string flyoverDatabase;
    if (flyoverRequested) {
        // Before any output, so that stdout carries protocol lines only.
        FlyoverMode::claimStdout();
        std::string usageError;
        if (!FlyoverMode::findInstall(argc, argv, flyoverInstall, usageError)) {
            std::cerr << "flyover: " << usageError << std::endl;
            return 2;
        }
        if (!FlyoverMode::findDatabase(argc, argv, flyoverDatabase, usageError)) {
            std::cerr << "flyover: " << usageError << std::endl;
            return 2;
        }
        if (!flyoverDatabase.empty() && !FlyoverMode::canOpenDatabase(flyoverDatabase)) {
            std::cerr << "flyover: cannot open database " << flyoverDatabase << std::endl;
            return 2;
        }
    }
    int exitCode = 0;
    if (!flyoverRequested) {
        std::cerr << "usage: Wowely-Planet-Farseer --flyover <route.json> | --list-gpus" << std::endl;
        return 2;
    }

//    std::ofstream out("log.txt");
//    std::streambuf *coutbuf = std::cout.rdbuf(); //save old buf
//    std::cout.rdbuf(out.rdbuf()); //redirect std::cout to out.txt!
//    std::cerr.rdbuf(out.rdbuf()); //redirect std::cout to out.txt!

std::ios_base::Init a;

#ifdef _WIN32
    SetUnhandledExceptionFilter(windows_exception_handler);
    const bool SET_TERMINATE = std::set_terminate(beforeCrash);
#if !defined(_MSC_VER) && !defined(_LIBCPP_VERSION)
    const bool SET_TERMINATE_UNEXP = std::set_unexpected(beforeCrash);
#endif
#endif
    signal(SIGABRT, &my_function_to_handle_aborts);
    signal(SIGSEGV, &my_function_to_handle_aborts);


    glfwInit();

    std::string rendererName = "vulkan";

//    if (argc > 1 && std::string(argv[1]) == "-vulkan") {
//        rendererName = "vulkan";
//    }

    //For Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    auto window = glfwCreateWindow(windowWidth, windowHeight, "Wowely-Planet-Farseer", nullptr, nullptr);

#ifdef LINK_VULKAN
    vkCallInitCallback callback;
    callback.createSurface = [&](VkInstance vkInstance) {
        VkSurfaceKHR surface;

        if (glfwCreateWindowSurface(vkInstance, window, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface!");
        }

        return surface;
    };
    callback.getRequiredExtensions = [](char** &extensionNames, int &extensionCnt) {
        uint32_t count;
        extensionNames = const_cast<char **>(glfwGetRequiredInstanceExtensions(&count));
        extensionCnt = count;
    };
#else
    void *callback = nullptr;
#endif

    //Open Sql storage
    HApiContainer apiContainer = std::make_shared<ApiContainer>();

    //The pipeline cache lives beside the viewer database the host passes, in the data folder, and not at all without one
#ifndef SKIP_VULKAN
    if (!flyoverDatabase.empty()) {
        GDeviceVLK::setPipelineCacheFile(
            (std::filesystem::path(flyoverDatabase).parent_path() / "pipeline-cache.bin").string());
    }
#endif

    //Create device
    auto hdevice = IDeviceFactory::createDevice(rendererName, &callback);
    apiContainer->databaseHandler = std::make_shared<CEmptySqliteDB>() ;
    apiContainer->hDevice = hdevice;

    SceneComposer sceneComposer = SceneComposer(apiContainer);

    frontendUI = flyoverDatabase.empty()
        ? std::make_shared<FrontendUI>(apiContainer)
        : std::make_shared<FrontendUI>(apiContainer, flyoverDatabase);

    glfwSetWindowUserPointer(window, &apiContainer);
    glfwSetKeyCallback(window, onKey);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetCursorPosCallback( window, cursor_position_callback);
    glfwSetWindowSizeCallback( window, window_size_callback);
    // The flyover window is resized from outside to any size.
    glfwSetWindowSizeLimits( window,
                             flyoverRequested ? GLFW_DONT_CARE : canvWidth,
                             flyoverRequested ? GLFW_DONT_CARE : canvHeight,
                             GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetMouseButtonCallback( window, mouse_button_callback);

    GLFWmonitor* windowMonitor = glfwGetPrimaryMonitor();
    float xscale, yscale;
    glfwGetMonitorContentScale(windowMonitor, &xscale, &yscale);

    float uiScale = std::max<float>(xscale, yscale);
    std::cout << "uiScale = " << uiScale << std::endl;

    int width_mm, height_mm;
    glfwGetMonitorPhysicalSize(windowMonitor, &width_mm, &height_mm);

    if (width_mm > 0 && height_mm > 0) {
        std::cout << "monitor width = " << width_mm << "mm, monitor height = " << height_mm << "mm" << std::endl;
        float monitorDiag = sqrt((width_mm * width_mm) + (height_mm * height_mm)) * 3.0f / 64.0f;
        std::cout << "monitor diag " << monitorDiag << "''" << std::endl;

        if (monitorDiag > 20) {
            uiScale = std::min<float>(uiScale, 2.0f);
        }
        if (monitorDiag > 25) {
            uiScale = std::min<float>(uiScale, 1.5f);
        }

        std::cout << "corrected uiScale for monitor dimensions = " << uiScale << std::endl;
    }

    uiScale = 1.0;
    frontendUI->setUIScale(uiScale);

//    auto native_me = std::this_thread::get_id().native_handle();
#ifdef WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    timeBeginPeriod(3);
#endif
    //This has to be called after setting all callbacks specific to this app.
    //ImGUI takes care of previous callbacks and calls them before applying it's own logic over data
    //Otherwise keys like backspace, delete etc wont work
    frontendUI->setFlyoverMode(flyoverRequested);
    frontendUI->initImgui(window);
    {
        int width = frontendUI->getWindowWidth();
        int height = frontendUI->getWindowHeight();

        glfwSetWindowSize(window, width, height);
        glfwGetFramebufferSize(window, &canvWidth, &canvHeight);
    }
    glfwSwapInterval(0);
    tbb::global_control global_limit(oneapi::tbb::global_control::max_allowed_parallelism,
                                     2*apiContainer->getConfig()->hardwareThreadCount());

    std::unique_ptr<FlyoverMode> flyover;
    if (flyoverRequested) {
        flyover = std::make_unique<FlyoverMode>(*frontendUI, window);
        if (flyover->start(flyoverRouteFile, flyoverInstall)) {
            flyoverMode = flyover.get();
        } else {
            exitCode = flyover->failureExitCode();
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

//try {
    // The shortest a frame takes: a frame-rate cap sleeps out the rest of its frame time; uncapped keeps a 5 ms floor.
    auto minFrameMs = [&]() {
        double frameRateCap = apiContainer->getConfig()->frameRateCap;
        return frameRateCap > 0 ? std::max(5.0, 1000.0 / frameRateCap) : 5.0;
    };
    bool resumedFromIdle = false;
    while (!glfwWindowShouldClose(window)) {
        /*
        robots@Impl: In flyover mode a frameRateCap of 0 or less draws, presents and sleeps on nothing, unless
        FlyoverMode::wantsFrames or a window resize asks for a frame: the loop waits on the window's events, which the stdin
        thread also ends (FlyoverMode::post), and runs the mode's update for its commands and lines. The wait is capped at
        IDLE_WAIT_SECONDS so the `world` line keeps its pace and a close request is seen. The first frame drawn after the gap
        takes one frame interval as its delta, not the time spent idle, so animation and the free camera carry on from where
        they stood, and sets weatherClockResetRequested, since weather falls on the wall clock. The UI mode has no such branch:
        there 0 is uncapped.
        */
        if (flyoverMode && apiContainer->getConfig()->frameRateCap <= 0 && !flyoverMode->wantsFrames() && !windowSizeChanged) {
            constexpr double IDLE_WAIT_SECONDS = 0.1;
            glfwWaitEventsTimeout(IDLE_WAIT_SECONDS);
            flyoverMode->update();
            lastFrame = glfwGetTime();
            resumedFromIdle = true;
            continue;
        }

        frontendUI->newFrame();
        stopMouse = frontendUI->getStopMouse();
        stopKeyboard = frontendUI->getStopKeyboard();

        glfwPollEvents();

        // Render scene
        currentFrame = glfwGetTime(); // seconds
        double deltaTime = currentFrame - lastFrame;
        if (resumedFromIdle) {
            deltaTime = minFrameMs() / 1000.0;
            apiContainer->getConfig()->weatherClockResetRequested = true;
            resumedFromIdle = false;
        }

        // The scene is the whole window, so the UI pass is an empty frame.
        if (flyoverMode) flyoverMode->update();
        frontendUI->composeFlyoverUI();
        auto sceneScenario = frontendUI->createFrameScenario(canvWidth, canvHeight, deltaTime);

        sceneComposer.draw(sceneScenario, windowSizeChanged);
        windowSizeChanged = false;

        double currentDeltaAfterDraw = (glfwGetTime() - currentFrame)*(1000.0f);
        if (flyoverMode) flyoverMode->noteFrame(currentDeltaAfterDraw / 1000.0);
        lastFrame = currentFrame;
        const double frameMs = minFrameMs();
        if (currentDeltaAfterDraw < frameMs) {
            using namespace std::chrono_literals;
#ifdef WIN32
            sqlite3_sleep((int)(frameMs-currentDeltaAfterDraw));
#else
            std::this_thread::sleep_for(std::chrono::milliseconds((int)(frameMs-currentDeltaAfterDraw)));
#endif
        }

#ifdef LINK_TRACY
        FrameMark;
#endif
    }
//} catch(const std::exception &e){
//    std::cerr << e.what() << std::endl;
//    throw;
//} catch(...) {
//    std::cout << "something happened" << std::endl;
//}

    hdevice->waitForAllWorkToComplete();
    frontendUI->shutDown();

    // std::cout << "program ended" << std::endl;
#ifdef WIN32
    timeEndPeriod(3);
#endif
    //        while (1) {
    //            mainLoop(&myapp);
    //        }

    flyoverMode = nullptr;
    return exitCode;
}


