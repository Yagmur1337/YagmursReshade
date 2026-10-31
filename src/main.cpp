// Unishade redraws the target game window in its own D3D11 swapchain, so ReShade can be
// installed on this exe. The game is only observed from outside, through window
// enumeration, executable metadata and Windows.Graphics.Capture. No game memory is read or code injected.

#include "addon.h"
#include "capture.h"
#include "config.h"
#include "depth/depth.h"
#include "discord.h"
#include "frame_limit.h"
#include "launcher.h"
#include "log.h"
#include "menu.h"
#include "names.h"
#include "overlay.h"
#include "reshade_config.h"
#include "game_integration.h"
#include "setup_check.h"
#include "state.h"
#include "update.h"

#include <algorithm>
#include <psapi.h>
#include <timeapi.h>
#include <map>
#include <optional>
#include <vector>

State g;

namespace
{
// While nothing is attached, every window is looked at this often. While a game is attached, only the window in front.
constexpr ULONGLONG kSearchInterval = 1000;
// A capture that fails to start is tried again after this, doubling each time up to kMaxCaptureRetry.
constexpr ULONGLONG kFirstCaptureRetry = 2000;
constexpr ULONGLONG kMaxCaptureRetry = 30000;
// The same frame is shown again this often, so toasts and hints still change smoothly at 60 FPS over a paused game.
constexpr ULONGLONG kRepeatInterval = 16;
// After a shortcut, frames are shown on every wake-up for this long, so what it changed shows right away.
constexpr ULONGLONG kShortcutResponse = 1000;
// Recovery from a lost graphics device gives up after this many losses within kLossWindow, well before Windows stops
// resetting a GPU that keeps hanging, or after failing to make a new device for kDeviceRetryLimit. A new device is
// made kDeviceRetryInterval after a loss or a failed attempt, since a GPU reset can take a moment.
constexpr size_t kMaxLosses = 3;
constexpr ULONGLONG kLossWindow = 60000;
constexpr ULONGLONG kDeviceRetryLimit = 30000;
constexpr ULONGLONG kDeviceRetryInterval = 1000;

struct CaptureRetry
{
    ULONGLONG at = 0;
    ULONGLONG delay = 0;
    unsigned failures = 0;
};

struct Loop
{
    ULONGLONG nextSearch = 0;
    HWND lastForeground = nullptr;
    HWND lastTarget = nullptr;
    std::map<HWND, CaptureRetry> captureRetries;

    ULONGLONG nextRepeat = 0;
    ULONGLONG fastUntil = 0;
    FrameLimit frameLimit;
    bool presentPending = false;
    bool wasInteractive = false;
    bool wasVisible = false;

    // The game to attach to again once a new device is made.
    std::optional<GameWindow> reattach;
    std::vector<ULONGLONG> losses;
    ULONGLONG deviceRetryAt = 0;
    ULONGLONG deviceFailingSince = 0;
};
Loop loop;

void LogState()
{
    LogReShadeDiagnostics();
    PROCESS_MEMORY_COUNTERS memory{ sizeof(memory) };
    GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
    const HWND foreground = GetForegroundWindow();
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(foreground, &foregroundPid);
    const auto& fps = g.frameStatistics;
    Log(LogLevel::Info, L"State: target=%p, selected=%p, foreground=%p, foreground_pid=%lu, capture=%d, visible=%d, menu=%d, frame=%d, pool=%dx%d, captured=%llu, "
                       L"capture_fps=%.1f, present_fps=%.1f, fresh_fps=%.1f, processing_ms=%.2f, peak_ms=%.2f, "
                       L"effects_loading=%d, effects_compiling=%d, depth=%d, fps_limit=%d, effect_resolution=%d%%, "
                       L"working_set=%zu MB, peak_working_set=%zu MB.",
        g.target, g.selectedGame ? g.selectedGame->window : nullptr, foreground, foregroundPid,
        g.captureEnabled, g.overlayVisible, g.editMode, static_cast<bool>(g.latestFrame), g.poolSize.Width, g.poolSize.Height,
        g.capturedFrames.load(std::memory_order_relaxed), fps.captureFps, fps.programFps, fps.freshFps, fps.processingMs, fps.peakProcessingMs,
        ReShadeLoadingEffects(), ReShadeCompilingEffects(), DepthEnabled(), FrameRateLimit(), EffectResolution(),
        memory.WorkingSetSize / (1024 * 1024), memory.PeakWorkingSetSize / (1024 * 1024));
}

void ShowError(const std::wstring& message)
{
    Log(LogLevel::Error, L"ShowError: %ls", message.c_str());
    MessageBoxW(nullptr, (message + L"\n\nMore details are in " + LogPath() + L".").c_str(), L"Unishade", MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND);
}

// A damaged list is kept beside it, since the next change to the list replaces the file.
void LoadGames()
{
    const std::wstring path = ExeDirectory() + L"games.ini";
    try
    {
        g.autoGames = LoadAutoGames(path);
    }
    catch (const std::exception& e)
    {
        g.autoGames = DefaultAutoGames();
        if (CopyFileW(path.c_str(), (path + L".damaged").c_str(), FALSE))
            Report(LogLevel::Warning, L"Your saved games could not be loaded (%hs), so Unishade is using the default list. The old list "
                                      L"was kept as games.ini.damaged.",
                   e.what());
        else
            Report(LogLevel::Warning, L"Your saved games could not be loaded (%hs), so Unishade is using the default list. Changing "
                                      L"the list replaces games.ini.",
                   e.what());
    }
    for (const AutoGame& game : g.autoGames)
        Log(LogLevel::Info, L"Saved game: %ls, executable=%ls, enabled=%d.", game.name.c_str(), game.executable.c_str(), game.enabled);
}

void GiveUpOnDevice(const wchar_t* reason)
{
    g.captureEnabled = false;
    loop.reattach.reset();
    loop.losses.clear();
    loop.deviceRetryAt = 0;
    loop.deviceFailingSince = 0;
    const std::wstring retry = g.overlayHotkey.empty() ? L"restart Unishade" : L"press " + g.overlayHotkey + L" or restart Unishade";
    Report(LogLevel::Error, L"%ls, so Unishade turned the overlay off. Update the graphics driver, then %ls.", reason, retry.c_str());
}

// After a driver update, a GPU reset or a switch to another GPU, everything made on the device is released and capture
// starts again on a new device with the same game.
void OnDeviceLost(HRESULT error)
{
    const HRESULT reason = g.device ? g.device->GetDeviceRemovedReason() : S_OK;
    Log(LogLevel::Warning, L"Lost the graphics device (0x%08X, reason 0x%08X). Starting again on a new one.", static_cast<unsigned>(error),
        static_cast<unsigned>(reason));
    LogState();
    const HWND game = g.target;
    const bool editing = g.editMode;
    loop.reattach = g.activeGame;
    ReleaseDevice();
    // The menu closed with the swapchain, so input goes back to the game.
    if (editing && game)
        SetForegroundWindow(game);

    const ULONGLONG now = GetTickCount64();
    std::erase_if(loop.losses, [now](ULONGLONG at) { return now - at >= kLossWindow; });
    loop.losses.push_back(now);
    loop.deviceRetryAt = now + kDeviceRetryInterval;
    if (loop.losses.size() >= kMaxLosses)
        GiveUpOnDevice(L"The graphics device was lost several times in a minute");
}

// Makes a new device once the previous one was lost. Returns whether there is a device.
bool EnsureDevice()
{
    if (g.device)
        return true;
    const ULONGLONG now = GetTickCount64();
    if (now < loop.deviceRetryAt)
        return false;
    try
    {
        CreateDevice();
        loop.deviceFailingSince = 0;
        Log(LogLevel::Info, L"Made a new graphics device.");
        return true;
    }
    catch (const winrt::hresult_error& e)
    {
        Log(LogLevel::Info, L"Device recovery failed: %ls (0x%08X), failing_for=%llu ms.", e.message().c_str(),
            static_cast<unsigned>(e.code()), loop.deviceFailingSince ? now - loop.deviceFailingSince : 0);
        if (!loop.deviceFailingSince)
        {
            loop.deviceFailingSince = now;
            Log(LogLevel::Warning, L"Could not make a new graphics device: %ls (0x%08X). Trying again.", e.message().c_str(),
                static_cast<unsigned>(e.code()));
        }
        else if (now - loop.deviceFailingSince >= kDeviceRetryLimit)
        {
            GiveUpOnDevice(L"The graphics device could not be made again after it was lost");
            return false;
        }
        loop.deviceRetryAt = now + kDeviceRetryInterval;
        return false;
    }
}

// The saved game a window belongs to, by its presets folder's name, or empty for a window picked for this session only.
std::wstring SavedGame(const GameWindow& game)
{
    try
    {
        const std::filesystem::path executable = ProcessExecutable(game.processId);
        for (const AutoGame& saved : g.autoGames)
            if (MatchesExecutable(saved, executable))
                return FolderName(saved.name);
    }
    catch (const std::system_error&)
    {
    }
    return {};
}

// A window whose capture fails to start is tried again less and less often, and the failure is logged once.
void Attach(const GameWindow& game)
{
    const ULONGLONG now = GetTickCount64();
    if (const auto retry = loop.captureRetries.find(game.window); retry != loop.captureRetries.end() && now < retry->second.at)
        return;
    if (g.target)
        StopCapture();
    g.activeGame = game;
    Log(LogLevel::Info, L"Attaching: %ls, hwnd=%p, pid=%lu, manual=%d.", game.name.c_str(), game.window, game.processId, g.selectedGame.has_value());
    try
    {
        Log(LogLevel::Info, L"Target executable: %ls.", ProcessExecutable(game.processId).c_str());
    }
    catch (const std::system_error& e)
    {
        Log(LogLevel::Info, L"Could not query target executable: %hs.", e.what());
    }
    try
    {
        StartCapture(game.window);
        SetPerformanceGame(SavedGame(game));
        loop.captureRetries.erase(game.window);
    }
    catch (const winrt::hresult_error& e)
    {
        if (DeviceLost(e.code()))
        {
            OnDeviceLost(e.code());
            return;
        }
        StopCapture();
        CaptureRetry& retry = loop.captureRetries[game.window];
        retry.delay = retry.delay ? std::min(retry.delay * 2, kMaxCaptureRetry) : kFirstCaptureRetry;
        retry.at = now + retry.delay;
        Log(LogLevel::Info, L"Capture attempt failed: hwnd=%p, attempt=%u, retry_in=%llu ms, error=0x%08X, %ls.", game.window,
            retry.failures + 1, retry.delay, static_cast<unsigned>(e.code()), e.message().c_str());
        if (++retry.failures == 1)
            Log(LogLevel::Error, L"Could not capture %ls: %ls (0x%08X)", game.name.c_str(), e.message().c_str(), static_cast<unsigned>(e.code()));
        else if (retry.failures == 2)
            Log(LogLevel::Info, L"Still could not capture %ls. Trying again less often, up to every %llu seconds.", game.name.c_str(),
                kMaxCaptureRetry / 1000);
    }
}

// Looks at every window only while nothing is attached. An attached game in front needs no search, and another saved
// game coming to the front takes over, so only the window in front is checked then.
void SearchForGame()
{
    // A game that was let go of may be followed by another right away.
    if (!g.target && loop.lastTarget)
        loop.nextSearch = 0;
    loop.lastTarget = g.target;
    const HWND foreground = GetForegroundWindow();
    const bool foregroundChanged = foreground != loop.lastForeground;
    loop.lastForeground = foreground;
    if (g.target && (g.selectedGame || g.editMode))
        return;

    const ULONGLONG now = GetTickCount64();
    std::optional<GameWindow> game;
    if (!g.target && g.selectedGame)
        game = FindGameTarget(g.selectedGame, g.autoGames);
    else if (!g.target)
    {
        if (foregroundChanged)
            game = MatchGameWindow(foreground, g.autoGames);
        if (!game && now >= loop.nextSearch)
        {
            loop.nextSearch = now + kSearchInterval;
            std::erase_if(loop.captureRetries, [](const auto& retry) { return !IsWindow(retry.first); });
            game = FindGameTarget(std::nullopt, g.autoGames, foreground);
        }
    }
    else if (foreground != g.target && (foregroundChanged || now >= loop.nextSearch))
    {
        loop.nextSearch = now + kSearchInterval;
        game = MatchGameWindow(foreground, g.autoGames);
    }
    if (game && game->window != g.target)
        Attach(*game);
}

int DisplayRefreshRate()
{
    HWND hwnd = g.overlay ? g.overlay : g.target;
    if (!hwnd)
        return 60;
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
        return 60;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(monitor, &mi))
    {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 0)
            return static_cast<int>(dm.dmDisplayFrequency);
    }
    return 60;
}

// Effects run on every present, so the same picture is not shown over and over: a new frame is shown when it arrives,
// and frames are shown on every wake-up only while the menu takes input, ReShade makes effects or a shortcut was just
// used. Otherwise the last frame is shown again every kRepeatInterval.
void ShowFrames()
{
    // Only the newest frame matters. The capture worker keeps it, and the one shown before goes back to the pool.
    bool fresh = false;
    {
        const std::lock_guard lock(g.frameMutex);
        if (g.arrivedFrame)
        {
            g.latestFrame = std::move(g.arrivedFrame);
            fresh = true;
        }
    }

    if (g.latestFrame)
    {
        SizeInt32 size = g.latestFrame.ContentSize();
        if ((size.Width != g.poolSize.Width || size.Height != g.poolSize.Height) && size.Width > 0 && size.Height > 0)
        {
            g.latestFrame = nullptr;
            {
                const std::lock_guard lock(g.frameMutex);
                g.arrivedFrame = nullptr;
            }
            g.poolSize = size;
            g.pool.Recreate(g.captureDevice, g.hdrWhiteLevel ? kHdrPixelFormat : kPixelFormat, kFrameBuffers, size);
            Log(LogLevel::Info, L"%ls resized to %dx%d", g.activeGame->name.c_str(), size.Width, size.Height);
        }
    }

    // Opening or closing a menu and showing the overlay change the picture without a new frame.
    const bool interactive = g.editMode || ReShadeMenuOpen();
    if (interactive != loop.wasInteractive || (g.overlayVisible && !loop.wasVisible))
        loop.presentPending = true;
    loop.wasInteractive = interactive;
    loop.wasVisible = g.overlayVisible;
    if (!g.overlayVisible || !g.latestFrame)
        return;

    const ULONGLONG now = GetTickCount64();
    const bool compiling = ReShadeLoadingEffects() || ReShadeCompilingEffects();
    if (fresh || interactive || loop.presentPending || compiling || now < loop.fastUntil || now >= loop.nextRepeat)
    {
        if (compiling)
        {
            // During effect compilation, pace presentation to 60 FPS to avoid overloading the GPU while keeping UI smooth.
            if (now < loop.nextRepeat)
                return;
            loop.nextRepeat = now + 16;
        }
        else
        {
            // A frame rate limit holds back whatever comes too soon, the menu included. A frame held back is shown with
            // the next one, or on the next repeat when the game stops drawing.
            int limit = FrameRateLimit();
            if (limit <= 0)
            {
                // Follow game up to display refresh rate. If the game is running at 120, 400, or 4000 FPS,
                // pacing presentation to the display refresh rate prevents GPU overload and queue starvation,
                // while delivering the freshest frame smoothly at full monitor speed without any stutter.
                limit = std::max(DisplayRefreshRate(), 60);
            }
            if (limit > 0)
            {
                using namespace std::chrono;
                const nanoseconds time = FrameStatistics::Clock::now().time_since_epoch();
                if (!loop.frameLimit.Allow(time.count(), nanoseconds(seconds(1)).count() / limit))
                    return;
            }
            loop.nextRepeat = now + kRepeatInterval;
        }
        PresentLatestFrame();
        loop.presentPending = false;

        // Immediately service any pending UI messages after frame presentation
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                PostQuitMessage(static_cast<int>(msg.wParam));
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

int Run()
{
    Log(LogLevel::Info, L"[INIT 1/11] Checking Windows Graphics Capture support...");
    if (!GraphicsCaptureSession::IsSupported())
    {
        const wchar_t* message = L"Windows Graphics Capture is not available, and Unishade needs it to copy the game's picture. "
                                 L"Update Windows and your graphics driver.";
        Log(LogLevel::Error, L"%ls", message);
        ShowError(message);
        return 1;
    }
    RequestBorderlessCapture();

    Log(LogLevel::Info, L"[INIT 2/11] Loading input hotkeys...");
    LoadInputHotkeys();
    Log(LogLevel::Info, L"[INIT 3/11] Loading saved games...");
    LoadGames();
    Log(LogLevel::Info, L"[INIT 4/11] Preparing ReShade config...");
    PrepareReShadeConfig();
    Log(LogLevel::Info, L"[INIT 5/11] Creating overlay window...");
    CreateOverlayWindow();
    Log(LogLevel::Info, L"[INIT 6/11] Loading nvngx virtual bridge...");
    LoadLibraryW((ExeDirectory() + L"nvngx.dll").c_str());
    Log(LogLevel::Info, L"[INIT 7/11] Initializing ReShade add-on...");
    InitAddon();
    Log(LogLevel::Info, L"[INIT 8/11] Initializing ReShade menu...");
    InitMenu();
    g.frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    winrt::check_bool(g.frameEvent != nullptr);
    Log(LogLevel::Info, L"[INIT 9/11] Creating D3D11 graphics device...");
    CreateDevice();
    Log(LogLevel::Info, L"[INIT 10/11] Checking setup & optional add-ons...");
    CheckSetup();
    InitDepth();
    RegisterHotkeys();
    CheckForUpdate();
    Log(LogLevel::Info, L"[INIT 11/11] Creating launcher & message loop ready.");
    CreateLauncher();
    Log(LogLevel::Info, L"Waiting for a supported game...");

    ULONGLONG nextStateLog = GetTickCount64();

    for (;;)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                Log(LogLevel::Info, L"Shutdown requested. Saving presets and stopping depth and the launcher.");
                FlushPresets();
                ShutdownDepth();
                ShutdownAddon();
                DestroyLauncher();
                return 0;
            }
            if (msg.message == WM_HOTKEY)
                loop.fastUntil = GetTickCount64() + kShortcutResponse;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!g.captureEnabled && g.target)
            StopCapture();

        if (g.target && !GameWindowExists(*g.activeGame))
        {
            Log(LogLevel::Info, L"%ls closed.", g.activeGame->name.c_str());
            StopCapture();
        }

        if (g.captureEnabled && EnsureDevice())
        {
            if (loop.reattach)
            {
                const GameWindow game = *loop.reattach;
                loop.reattach.reset();
                if (!g.target && GameWindowExists(game) && (!g.selectedGame || g.selectedGame->window == game.window))
                    Attach(game);
            }
            SearchForGame();
        }

        UpdateOverlay();
        if (!g.overlayVisible)
        {
            g.frameStatistics.Reset(FrameStatistics::Clock::now(), g.capturedFrames.load(std::memory_order_relaxed));
            // ReShade writes preset changes from its present, which stops while the overlay is hidden.
            FlushPresets();
        }
        UpdateInputHotkey();
        UpdateHeldCompare();
        UpdateDiscord();
        UpdateLauncher();

        if (const ULONGLONG now = GetTickCount64(); now >= nextStateLog)
        {
            LogState();
            nextStateLog = now + 30000;
        }

        if (g.target)
        {
            try
            {
                SetCaptureIdle(!g.overlayVisible);
                ShowFrames();
            }
            catch (const winrt::hresult_error& e)
            {
                if (DeviceLost(e.code()))
                {
                    OnDeviceLost(e.code());
                }
                else
                {
                    Log(LogLevel::Warning, L"Frame capture/presentation glitch: %ls (0x%08X)", e.message().c_str(), static_cast<unsigned>(e.code()));
                    const std::lock_guard lock(g.frameMutex);
                    g.latestFrame = nullptr;
                    g.arrivedFrame = nullptr;
                }
            }
            catch (const std::exception& e)
            {
                Log(LogLevel::Warning, L"Frame presentation exception: %hs", e.what());
                const std::lock_guard lock(g.frameMutex);
                g.latestFrame = nullptr;
                g.arrivedFrame = nullptr;
            }
            catch (...)
            {
                Log(LogLevel::Warning, L"Unknown frame presentation exception");
                const std::lock_guard lock(g.frameMutex);
                g.latestFrame = nullptr;
                g.arrivedFrame = nullptr;
            }
        }

        const int refresh = DisplayRefreshRate();
        const DWORD timeout = g.overlayVisible ? static_cast<DWORD>(std::clamp(1000 / std::max(refresh, 60), 1, 16)) : 250;
        const DWORD wait = MsgWaitForMultipleObjects(1, &g.frameEvent, FALSE, timeout, QS_ALLINPUT);
        if (wait == WAIT_FAILED)
        {
            const DWORD error = GetLastError();
            Log(LogLevel::Error, L"Frame/message wait failed (Windows error %lu).", error);
            winrt::throw_hresult(HRESULT_FROM_WIN32(error));
        }
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // Keep the original mutex name so old and new hosts cannot run together.
    const HANDLE instance = CreateMutexW(nullptr, TRUE, L"Local\\RobloxShadeHost");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // From the taskbar, or from the notification area, where it waits as it was, maximized or not.
        if (const HWND other = FindWindowW(kLauncherClass, nullptr))
        {
            ShowWindow(other, IsIconic(other) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(other);
        }
        return 0;
    }

    DisableProcessWindowsGhosting();
    timeBeginPeriod(1);
    InitLog();
    int result = 1;
    try
    {
        if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            Log(LogLevel::Info, L"Could not set per-monitor DPI awareness (Windows error %lu).", GetLastError());
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        result = Run();
    }
    catch (const winrt::hresult_error& e)
    {
        Log(LogLevel::Error, L"Unishade stopped: %ls (0x%08X)", e.message().c_str(), static_cast<unsigned>(e.code()));
        LogState();
        LogStackTrace();
        FlushLog();
        ShowError(L"Unishade stopped because of an error: " + std::wstring(e.message()));
    }
    catch (const std::exception& e)
    {
        Log(LogLevel::Error, L"Unishade stopped: %hs", e.what());
        LogState();
        LogStackTrace();
        FlushLog();
        ShowError(L"Unishade stopped because of an error.");
    }
    catch (...)
    {
        Log(LogLevel::Error, L"Unishade stopped because of an unknown exception.");
        LogStackTrace();
        FlushLog();
        ShowError(L"Unishade stopped because of an error.");
    }
    // Removes the tray icon after an error. After a normal exit the launcher is already gone.
    DestroyLauncher();
    CloseHandle(instance);
    timeEndPeriod(1);
    Log(LogLevel::Info, L"Session ended: exit_code=%d.", result);
    FlushLog();
    // Releasing the swapchain makes ReShade wait for the effects it is still compiling, which can take minutes
    // right after installing. ReShade writes settings and presets a second after they change, so the host exits
    // without that wait.
    ExitProcess(static_cast<UINT>(result));
}
