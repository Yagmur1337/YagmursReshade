#include "reshade_config.h"
#include "config.h"
#include "ini_text.h"
#include "log.h"
#include "text.h"
#include "theme.h"

#include <windows.h>
#include <shlobj.h>
#include <dxgi.h>
#include <winrt/base.h>

#pragma comment(lib, "dxgi.lib")

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
UINT DetectGpuVendor()
{
    winrt::com_ptr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void())))
    {
        winrt::com_ptr<IDXGIAdapter1> adapter;
        if (SUCCEEDED(factory->EnumAdapters1(0, adapter.put())))
        {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)))
                return desc.VendorId;
        }
    }
    return 0;
}
std::string Color(unsigned rgb, float alpha = 1.0f)
{
    char text[64];
    snprintf(text, sizeof(text), "%f,%f,%f,%f", ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
    return text;
}

// ReShade picks a font with the needed glyphs for these languages, which Segoe UI lacks.
bool NeedsReShadeFont()
{
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    LCIDToLocaleName(GetUserDefaultUILanguage(), name, LOCALE_NAME_MAX_LENGTH, 0);
    for (const wchar_t* prefix : { L"ja", L"ko", L"zh", L"th" })
        if (_wcsnicmp(name, prefix, 2) == 0)
            return true;
    return false;
}

void ApplyTheme(IniText& ini)
{
    using namespace theme;
    const struct
    {
        const char* name;
        std::string value;
    } colors[] = {
        { "Text", Color(kText) },
        { "TextDisabled", Color(kDim) },
        { "WindowBg", Color(kBackground, 0.97f) },
        { "ChildBg", Color(0, 0) },
        { "PopupBg", Color(0x16171D, 0.98f) },
        { "Border", Color(kBorder) },
        { "BorderShadow", Color(0, 0) },
        { "FrameBg", Color(kCard) },
        { "FrameBgHovered", Color(kCardHover) },
        { "FrameBgActive", Color(kCardHover) },
        { "TitleBg", Color(kSidebar) },
        { "TitleBgActive", Color(kSidebar) },
        { "TitleBgCollapsed", Color(kSidebar) },
        { "MenuBarBg", Color(kSidebar) },
        { "ScrollbarBg", Color(0, 0) },
        { "ScrollbarGrab", Color(kBorder) },
        { "ScrollbarGrabHovered", Color(kBorderStrong) },
        { "ScrollbarGrabActive", Color(kBorderStrong) },
        { "CheckMark", Color(kAccentHover) },
        { "SliderGrab", Color(kAccent) },
        { "SliderGrabActive", Color(kAccentHover) },
        { "Button", Color(kCard) },
        { "ButtonHovered", Color(kCardHover) },
        { "ButtonActive", Color(kBorder) },
        { "Header", Color(kAccent, 0.35f) },
        { "HeaderHovered", Color(kAccent, 0.5f) },
        { "HeaderActive", Color(kAccent, 0.65f) },
        { "Separator", Color(kBorder) },
        { "SeparatorHovered", Color(kBorderStrong) },
        { "SeparatorActive", Color(kAccent) },
        { "ResizeGrip", Color(kBorder) },
        { "ResizeGripHovered", Color(kBorderStrong) },
        { "ResizeGripActive", Color(kAccent) },
        { "TabHovered", Color(kCardHover) },
        { "Tab", Color(kSidebar) },
        { "TabSelected", Color(kCard) },
        { "TabSelectedOverline", Color(kAccent) },
        { "TabDimmed", Color(kSidebar) },
        { "TabDimmedSelected", Color(kCard) },
        { "DockingPreview", Color(kAccent, 0.5f) },
        { "DockingEmptyBg", Color(kBackground) },
        { "TextLink", Color(kAccentHover) },
        { "TextSelectedBg", Color(kAccent, 0.35f) },
        { "NavCursor", Color(kAccent) },
        { "ModalWindowDimBg", Color(0, 0.6f) },
    };
    ini.Set("STYLE", "StyleIndex", "4");
    for (const auto& color : colors)
        ini.Set("STYLE", color.name, color.value);
    for (const char* rounding : { "WindowRounding", "ChildRounding", "FrameRounding", "PopupRounding", "ScrollbarRounding", "GrabRounding", "TabRounding" })
        ini.Set("STYLE", rounding, "6.000000");
}
} // namespace

void PrepareReShadeConfig()
{
    const std::filesystem::path path = ExeDirectory() + L"ReShade.ini";
    std::ifstream input(path, std::ios::binary);
    IniText ini{ std::string(std::istreambuf_iterator<char>(input), {}) };
    input.close();

    std::string value;
    // Restore renodx-dlss addon if disabled, so all features run on all GPUs (AMD, Intel, NVIDIA).
    std::error_code fsEc;
    const auto dlssFile = ExeDirectory() + L"renodx-dlss.addon64";
    const auto dlssDisabled = ExeDirectory() + L"renodx-dlss.addon64.disabled";
    if (std::filesystem::exists(dlssDisabled) && !std::filesystem::exists(dlssFile))
        std::filesystem::rename(dlssDisabled, dlssFile, fsEc);

    if (ini.Get("ADDON", "DisabledAddons", value))
    {
        std::string kept;
        for (size_t start = 0; start <= value.size();)
        {
            const size_t end = std::min(value.find(',', start), value.size());
            const std::string name = value.substr(start, end - start);
            start = end + 1;
            if (!name.empty() && name != "Unishade" && name.rfind("Unishade@", 0) != 0 &&
                name != "RobloxShadeHost" && name.rfind("RobloxShadeHost@", 0) != 0 &&
                name != "renodx-dlss")
                kept += (kept.empty() ? "" : ",") + name;
        }
        ini.Set("ADDON", "DisabledAddons", kept);
    }

    if (!ini.Get("OVERLAY", "TutorialProgress", value) || value != "4")
        ini.Set("OVERLAY", "TutorialProgress", "4");

    // Limit shader compilation search paths to the preset shader directories.
    // This stops compiling 400+ unused heavy shaders (ray tracing, CRT, VR, etc.) that freeze the CPU and hang the game.
    const std::string presetEffectPaths = ".\\reshade-shaders\\Shaders,"
                                          ".\\reshade-shaders\\Shaders\\AstrayFX,"
                                          ".\\reshade-shaders\\Shaders\\FXShaders,"
                                          ".\\reshade-shaders\\Shaders\\SweetFX";
    ini.Set("GENERAL", "EffectSearchPaths", presetEffectPaths);

    const std::string presetTexturePaths = ".\\reshade-shaders\\Textures,"
                                           ".\\reshade-shaders\\Textures\\AstrayFX,"
                                           ".\\reshade-shaders\\Textures\\FXShaders,"
                                           ".\\reshade-shaders\\Textures\\SweetFX";
    ini.Set("GENERAL", "TextureSearchPaths", presetTexturePaths);

    const std::filesystem::path cachePath = ExeDirectory() + L"IntermediateCache";
    std::error_code ec;
    std::filesystem::create_directories(cachePath, ec);
    ini.Set("GENERAL", "IntermediateCachePath", Utf8(cachePath.wstring()));
    // Only compile the effects used by the active preset.
    // Prevents compiling 100+ unused shaders, avoiding freezes, long startup lags, and application hangs.
    ini.Set("GENERAL", "LoadOnlyEnabledEffects", "1");
    ini.Set("GENERAL", "NoReloadOnInit", "0");

    if (!ini.Get("GENERAL", "PresetPath", value) || value.empty() || !std::filesystem::exists(ExeDirectory() / std::filesystem::u8path(value)))
    {
        if (std::filesystem::exists(ExeDirectory() + L"presets\\GenericPreset1.ini"))
            ini.Set("GENERAL", "PresetPath", ".\\presets\\GenericPreset1.ini");
    }
    if ((!ini.Get("STYLE", "Font", value) || value.empty() || _stricmp(value.c_str(), "ProggyClean.ttf") == 0) && !NeedsReShadeFont())
    {
        wchar_t windows[MAX_PATH]{};
        GetWindowsDirectoryW(windows, MAX_PATH);
        ini.Set("STYLE", "Font", Utf8(windows) + "\\Fonts\\segoeui.ttf");
    }
    // ReShade saves screenshots beside the exe unless told otherwise, where nobody looks for them.
    if (!ini.Get("SCREENSHOT", "SavePath", value) || value.empty() || value == ".\\")
    {
        PWSTR pictures = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pictures)))
            ini.Set("SCREENSHOT", "SavePath", Utf8(std::wstring(pictures) + L"\\Unishade\\"));
        CoTaskMemFree(pictures);
    }
    if (!ini.Get("STYLE", "StyleIndex", value))
        ApplyTheme(ini);
    // RenoDX's add-on otherwise waits for the inputs of a game's own DLSS, which Unishade never has.
    if (std::filesystem::exists(ExeDirectory() + L"renodx-dlss.addon64") && !ini.Get("RENODX-DLSS", "DirectNeuralRenderingRequireDlss", value))
        ini.Set("RENODX-DLSS", "DirectNeuralRenderingRequireDlss", "0");

    if (!ini.Changed())
        return;
    // Written beside it and moved over it, so a crash while writing cannot damage the user's ReShade settings.
    const std::filesystem::path temporary = path.native() + L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << ini.Text();
    output.close();
    if (!output || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        Log(LogLevel::Warning, L"Could not update %ls, so ReShade may show its own tutorial.", path.c_str());
    }
}
