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
#include "flyover/FlyoverConfig.h"
#ifndef SKIP_VULKAN
#include "../wowViewerLib/src/gapi/vulkan/GDeviceVulkan.h"
#endif
#include <exception>
#ifdef WIN32
#include <timeapi.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi.h>
#include <wrl/client.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <chrono>
#include <set>
#endif
#ifdef LINK_TRACY
#include "Tracy.hpp"
#endif

#ifdef WIN32
/*
robots@Impl: The vblank wait.

With Config::vsyncDivisor = N (1 to 4), a frame starts on every Nth vertical blank of the monitor the window is on, and
frameRateCap is ignored. wait() runs before the frame's work: it calls IDXGIOutput::WaitForVBlank, stamping each return,
until the time since the vblank that started the previous frame is at least (N - 0.5) periods. The half period
absorbs the jitter of a return, so the Nth vblank counts however early or late it is stamped. A frame that overran N
refreshes finds that time already past, so the first vblank after it starts the next frame instead of the Nth after
that, and a frame never starts between two vblanks.

The period is measured, never read from the display mode, which reports 59 or 60 for a 59.94 Hz screen. A resolve
times 4 consecutive vblanks, and afterwards every pair of consecutive returns one period apart (within 25%) pulls a
smoothed value toward their gap by 1/16.

The output is the one whose DXGI_OUTPUT_DESC.Monitor is the window's MonitorFromWindow, searched on every adapter, and
it is resolved again when that handle changes. When no output matches, or WaitForVBlank fails or returns at no refresh
rate, wait() returns false and the caller paces by frameRateCap; a new resolve is tried a second later. Each cause
is reported once on stderr.
*/
class VBlankPacer {
public:
    // Blocks until a frame may start, and returns true. Returns false at once when there is no usable vblank source.
    bool wait(GLFWwindow *window, int divisor) {
        HMONITOR monitor = MonitorFromWindow(glfwGetWin32Window(window), MONITOR_DEFAULTTONEAREST);
        if (monitor != m_monitor) {
            m_monitor = monitor;
            drop(Clock::time_point{});
        }
        if (!m_output) {
            if (Clock::now() < m_retryAt) return false;
            if (!resolve()) return false;
        }

        for (;;) {
            HRESULT result = m_output->WaitForVBlank();
            auto now = Clock::now();
            if (FAILED(result)) {
                fail("wait", "WaitForVBlank failed", result);
                return false;
            }

            double gap = millisecondsBetween(m_lastReturn, now);
            if (std::abs(gap - m_periodMs) < 0.25 * m_periodMs) m_periodMs += (gap - m_periodMs) / 16.0;
            m_lastReturn = now;

            if (!m_hasFrameStart || millisecondsBetween(m_frameStart, now) >= (divisor - 0.5) * m_periodMs) {
                m_frameStart = now;
                m_hasFrameStart = true;
                return true;
            }
        }
    }

private:
    using Clock = std::chrono::high_resolution_clock;

    Microsoft::WRL::ComPtr<IDXGIOutput> m_output;
    HMONITOR m_monitor = nullptr;
    Clock::time_point m_retryAt{};
    Clock::time_point m_lastReturn{};
    Clock::time_point m_frameStart{};
    bool m_hasFrameStart = false;
    double m_periodMs = 0;
    std::set<std::string> m_reported;

    static double millisecondsBetween(Clock::time_point from, Clock::time_point to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    }

    void drop(Clock::time_point retryAt) {
        m_output.Reset();
        m_hasFrameStart = false;
        m_retryAt = retryAt;
    }

    bool fail(const char *cause, const char *reason, HRESULT result) {
        drop(Clock::now() + std::chrono::seconds(1));
        if (m_reported.insert(cause).second) {
            fprintf(stderr, "vsync: %s (0x%08lx); using frameRateCap\n", reason, (unsigned long) result);
        }
        return false;
    }

    bool resolve() {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(result)) return fail("factory", "no DXGI factory", result);

        Microsoft::WRL::ComPtr<IDXGIOutput> found;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        for (UINT a = 0; !found && SUCCEEDED(factory->EnumAdapters1(a, &adapter)); a++) {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            for (UINT o = 0; !found && SUCCEEDED(adapter->EnumOutputs(o, &output)); o++) {
                DXGI_OUTPUT_DESC desc;
                if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == m_monitor) found = output;
            }
        }
        if (!found) return fail("output", "no DXGI output for the window's monitor", S_OK);

        constexpr int k_Samples = 4;
        Clock::time_point first{};
        Clock::time_point last{};
        for (int i = 0; i <= k_Samples; i++) {
            result = found->WaitForVBlank();
            last = Clock::now();
            if (FAILED(result)) return fail("wait", "WaitForVBlank failed", result);
            if (i == 0) first = last;
        }
        const double period = millisecondsBetween(first, last) / k_Samples;
        if (period < 2.0 || period > 100.0) return fail("period", "vblanks do not arrive at a refresh rate", S_OK);

        m_output = found;
        m_periodMs = period;
        m_lastReturn = last;
        m_hasFrameStart = false;
        return true;
    }
};
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
// Set in the --flyover mode, where the window's own mouse and keyboard do nothing: the host reads the input and steers by stdin.
FlyoverMode *flyoverMode = nullptr;

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos){
    if (flyoverMode) return;
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
    if (flyoverMode) return;
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
    if (flyoverMode) return;

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
    if (flyoverMode) return;
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

    // Prints the renderer fields of a fresh Config, the library's defaults, as one JSON line and exits, with no window or device.
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--config-defaults") {
            Config config;
            std::cout << FlyoverConfig::dump(config) << std::endl;
            return 0;
        }
    }

    std::string flyoverRouteFile;
    const bool flyoverRequested = FlyoverMode::findArgument(argc, argv, flyoverRouteFile);
    FlyoverInstall flyoverInstall;
    std::string flyoverDatabase;
    std::string flyoverHeights;
    std::string flyoverBake;
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
        if (!FlyoverMode::findHeights(argc, argv, flyoverHeights, flyoverBake, usageError)) {
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
        std::cerr << "usage: Wowely-Planet-Farseer --flyover <route.json> | --list-gpus | --config-defaults"<< std::endl;
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

    bool windowFromEnv = false;
    // A height bake renders 256 x 256 images, so its window is that size, and hidden: nobody looks at it.
    if (!flyoverBake.empty()) {
        windowWidth = 256;
        windowHeight = 256;
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    } else if (const char *size = getenv("WWV_WINDOW"); size != nullptr && *size != '\0') {
        // WWV_WINDOW=<w>x<h> sizes the window in pixels, for a measurement at the monitor's size.
        char *end = nullptr;
        const long w = isdigit((unsigned char) size[0]) ? strtol(size, &end, 10) : 0;
        const bool wellFormed = w > 0 && *end == 'x' && isdigit((unsigned char) end[1]);
        const long h = wellFormed ? strtol(end + 1, &end, 10) : 0;
        if (wellFormed && h > 0 && *end == '\0' && w <= 16384 && h <= 16384) {
            windowWidth = (int) w;
            windowHeight = (int) h;
            windowFromEnv = true;
            // 🤖 Borderless, so a title bar does not take its height from the client area.
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        } else {
            fprintf(stderr, "window: WWV_WINDOW=%s ignored\n", size);
        }
    }
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
        // A bake keeps the 256 x 256 window it was made with, and so does a WWV_WINDOW size.
        const bool keepSize = !flyoverBake.empty() || windowFromEnv;
        int width = keepSize ? windowWidth : frontendUI->getWindowWidth();
        int height = keepSize ? windowHeight : frontendUI->getWindowHeight();

        glfwSetWindowSize(window, width, height);
        glfwGetFramebufferSize(window, &canvWidth, &canvHeight);
    }
    glfwSwapInterval(0);
    tbb::global_control global_limit(oneapi::tbb::global_control::max_allowed_parallelism,
                                     2*apiContainer->getConfig()->hardwareThreadCount());

    std::unique_ptr<FlyoverMode> flyover;
    if (flyoverRequested) {
        flyover = std::make_unique<FlyoverMode>(*frontendUI, window);
        flyover->setHeights(flyoverHeights, flyoverBake);
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
#ifdef WIN32
    VBlankPacer vblankPacer;
#endif
    while (!glfwWindowShouldClose(window)) {
        // On a vsyncDivisor, the frame starts on a vblank and the sleep below does not run.
        bool onVblank = false;
#ifdef WIN32
        const int vsyncDivisor = std::clamp(apiContainer->getConfig()->vsyncDivisor, 0, 4);
        if (vsyncDivisor > 0) {
#ifdef LINK_TRACY
            ZoneScopedN("vblank wait");
#endif
            onVblank = vblankPacer.wait(window, vsyncDivisor);
        }
#endif
        frontendUI->newFrame();
        stopMouse = frontendUI->getStopMouse();
        stopKeyboard = frontendUI->getStopKeyboard();

        glfwPollEvents();

        // Render scene
        currentFrame = glfwGetTime(); // seconds
        double deltaTime = currentFrame - lastFrame;

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
        if (!onVblank && currentDeltaAfterDraw < frameMs) {
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


