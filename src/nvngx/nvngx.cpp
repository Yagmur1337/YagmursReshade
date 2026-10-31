#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

using NVSDK_NGX_Result = unsigned int;
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_Success = 0x00000001;
constexpr NVSDK_NGX_Result NVSDK_NGX_Result_Fail = 0xBAD00000;

struct NVSDK_NGX_Parameter
{
    virtual void Set(const char *InName, unsigned long long InValue) = 0;
    virtual void Set(const char *InName, float InValue) = 0;
    virtual void Set(const char *InName, double InValue) = 0;
    virtual void Set(const char *InName, unsigned int InValue) = 0;
    virtual void Set(const char *InName, int InValue) = 0;
    virtual void Set(const char *InName, ID3D11Resource *InValue) = 0;
    virtual void Set(const char *InName, ID3D12Resource *InValue) = 0;
    virtual void Set(const char *InName, void *InValue) = 0;

    virtual NVSDK_NGX_Result Get(const char *InName, unsigned long long *OutValue) = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, float *OutValue) = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, double *OutValue) = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, unsigned int *OutValue) = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, int *OutValue) = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, ID3D11Resource **OutValue) = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, ID3D12Resource **OutValue) = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, void **OutValue) = 0;

    virtual void Reset() = 0;
};

class NVSDK_NGX_Parameter_Impl : public NVSDK_NGX_Parameter
{
private:
    std::mutex m_mutex;
    std::unordered_map<std::string, unsigned long long> m_u64Values;
    std::unordered_map<std::string, double> m_f64Values;
    std::unordered_map<std::string, void*> m_ptrValues;

public:
    void Set(const char *InName, unsigned long long InValue) override
    {
        if (!InName) return;
        std::lock_guard lock(m_mutex);
        m_u64Values[InName] = InValue;
    }

    void Set(const char *InName, float InValue) override
    {
        if (!InName) return;
        std::lock_guard lock(m_mutex);
        m_f64Values[InName] = static_cast<double>(InValue);
    }

    void Set(const char *InName, double InValue) override
    {
        if (!InName) return;
        std::lock_guard lock(m_mutex);
        m_f64Values[InName] = InValue;
    }

    void Set(const char *InName, unsigned int InValue) override
    {
        if (!InName) return;
        std::lock_guard lock(m_mutex);
        m_u64Values[InName] = InValue;
    }

    void Set(const char *InName, int InValue) override
    {
        if (!InName) return;
        std::lock_guard lock(m_mutex);
        m_u64Values[InName] = static_cast<unsigned long long>(static_cast<long long>(InValue));
    }

    void Set(const char *InName, ID3D11Resource *InValue) override
    {
        if (!InName) return;
        std::lock_guard lock(m_mutex);
        m_ptrValues[InName] = InValue;
    }

    void Set(const char *InName, ID3D12Resource *InValue) override
    {
        if (!InName) return;
        std::lock_guard lock(m_mutex);
        m_ptrValues[InName] = InValue;
    }

    void Set(const char *InName, void *InValue) override
    {
        if (!InName) return;
        std::lock_guard lock(m_mutex);
        m_ptrValues[InName] = InValue;
    }

    NVSDK_NGX_Result Get(const char *InName, unsigned long long *OutValue) override
    {
        if (!InName || !OutValue) return NVSDK_NGX_Result_Fail;
        std::lock_guard lock(m_mutex);
        auto it = m_u64Values.find(InName);
        if (it != m_u64Values.end())
        {
            *OutValue = it->second;
            return NVSDK_NGX_Result_Success;
        }
        if (strstr(InName, "Available") || strstr(InName, "Supported") || strstr(InName, "FeatureSupported"))
        {
            *OutValue = 1;
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char *InName, float *OutValue) override
    {
        if (!InName || !OutValue) return NVSDK_NGX_Result_Fail;
        std::lock_guard lock(m_mutex);
        auto it = m_f64Values.find(InName);
        if (it != m_f64Values.end())
        {
            *OutValue = static_cast<float>(it->second);
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char *InName, double *OutValue) override
    {
        if (!InName || !OutValue) return NVSDK_NGX_Result_Fail;
        std::lock_guard lock(m_mutex);
        auto it = m_f64Values.find(InName);
        if (it != m_f64Values.end())
        {
            *OutValue = it->second;
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char *InName, unsigned int *OutValue) override
    {
        if (!InName || !OutValue) return NVSDK_NGX_Result_Fail;
        std::lock_guard lock(m_mutex);
        auto it = m_u64Values.find(InName);
        if (it != m_u64Values.end())
        {
            *OutValue = static_cast<unsigned int>(it->second);
            return NVSDK_NGX_Result_Success;
        }
        if (strstr(InName, "Available") || strstr(InName, "Supported") || strstr(InName, "FeatureSupported"))
        {
            *OutValue = 1;
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char *InName, int *OutValue) override
    {
        if (!InName || !OutValue) return NVSDK_NGX_Result_Fail;
        std::lock_guard lock(m_mutex);
        auto it = m_u64Values.find(InName);
        if (it != m_u64Values.end())
        {
            *OutValue = static_cast<int>(it->second);
            return NVSDK_NGX_Result_Success;
        }
        if (strstr(InName, "Available") || strstr(InName, "Supported") || strstr(InName, "FeatureSupported"))
        {
            *OutValue = 1;
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char *InName, ID3D11Resource **OutValue) override
    {
        if (!InName || !OutValue) return NVSDK_NGX_Result_Fail;
        std::lock_guard lock(m_mutex);
        auto it = m_ptrValues.find(InName);
        if (it != m_ptrValues.end())
        {
            *OutValue = static_cast<ID3D11Resource*>(it->second);
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char *InName, ID3D12Resource **OutValue) override
    {
        if (!InName || !OutValue) return NVSDK_NGX_Result_Fail;
        std::lock_guard lock(m_mutex);
        auto it = m_ptrValues.find(InName);
        if (it != m_ptrValues.end())
        {
            *OutValue = static_cast<ID3D12Resource*>(it->second);
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char *InName, void **OutValue) override
    {
        if (!InName || !OutValue) return NVSDK_NGX_Result_Fail;
        std::lock_guard lock(m_mutex);
        auto it = m_ptrValues.find(InName);
        if (it != m_ptrValues.end())
        {
            *OutValue = it->second;
            return NVSDK_NGX_Result_Success;
        }
        return NVSDK_NGX_Result_Fail;
    }

    void Reset() override
    {
        std::lock_guard lock(m_mutex);
        m_u64Values.clear();
        m_f64Values.clear();
        m_ptrValues.clear();
    }
};

namespace
{
    static NVSDK_NGX_Parameter_Impl g_capabilityParameters;

    HMODULE GetRealNvngx()
    {
        static HMODULE hReal = []() -> HMODULE {
            wchar_t sysPath[MAX_PATH];
            if (GetSystemDirectoryW(sysPath, MAX_PATH))
            {
                std::wstring full = std::wstring(sysPath) + L"\\nvngx.dll";
                return LoadLibraryW(full.c_str());
            }
            return nullptr;
        }();
        return hReal;
    }
}

extern "C"
{
    NVSDK_NGX_Result NVSDK_NGX_D3D11_AllocateParameters(NVSDK_NGX_Parameter **OutParameters)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(NVSDK_NGX_Parameter **);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D11_AllocateParameters")))
                return pfn(OutParameters);
        }
        if (!OutParameters) return NVSDK_NGX_Result_Fail;
        *OutParameters = new NVSDK_NGX_Parameter_Impl();
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D11_GetCapabilityParameters(NVSDK_NGX_Parameter **OutParameters)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(NVSDK_NGX_Parameter **);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D11_GetCapabilityParameters")))
                return pfn(OutParameters);
        }
        if (!OutParameters) return NVSDK_NGX_Result_Fail;
        *OutParameters = &g_capabilityParameters;
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D11_GetParameters(NVSDK_NGX_Parameter **OutParameters)
    {
        return NVSDK_NGX_D3D11_GetCapabilityParameters(OutParameters);
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D11_Init(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, ID3D11Device *InDevice, const void *InFeatureInfo, unsigned int InSDKVersion)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(unsigned long long, const wchar_t *, ID3D11Device *, const void *, unsigned int);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D11_Init")))
                return pfn(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
        }
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D11_Init_with_ProjectID(const char *InProjectId, int InEngineType, const char *InEngineVersion, const wchar_t *InApplicationDataPath, ID3D11Device *InDevice, const void *InFeatureInfo, unsigned int InSDKVersion)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(const char *, int, const char *, const wchar_t *, ID3D11Device *, const void *, unsigned int);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D11_Init_with_ProjectID")))
                return pfn(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
        }
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D11_CreateFeature(ID3D11DeviceContext *InDevCtx, int InFeatureID, const NVSDK_NGX_Parameter *InParameters, void **OutHandle)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(ID3D11DeviceContext *, int, const NVSDK_NGX_Parameter *, void **);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D11_CreateFeature")))
                return pfn(InDevCtx, InFeatureID, InParameters, OutHandle);
        }
        if (OutHandle) *OutHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1337));
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D11_ReleaseFeature(void *InHandle)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(void *);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D11_ReleaseFeature")))
                return pfn(InHandle);
        }
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D11_EvaluateFeature(ID3D11DeviceContext *InDevCtx, const void *InHandle, const NVSDK_NGX_Parameter *InParameters, void *InCallback)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(ID3D11DeviceContext *, const void *, const NVSDK_NGX_Parameter *, void *);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D11_EvaluateFeature")))
                return pfn(InDevCtx, InHandle, InParameters, InCallback);
        }
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D11_EvaluateFeature_C(ID3D11DeviceContext *InDevCtx, const void *InHandle, const NVSDK_NGX_Parameter *InParameters, void *InCallback)
    {
        return NVSDK_NGX_D3D11_EvaluateFeature(InDevCtx, InHandle, InParameters, InCallback);
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D11_Shutdown1(ID3D11Device *InDevice)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(ID3D11Device *);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D11_Shutdown1")))
                return pfn(InDevice);
        }
        return NVSDK_NGX_Result_Success;
    }

    // D3D12 Exports
    NVSDK_NGX_Result NVSDK_NGX_D3D12_AllocateParameters(NVSDK_NGX_Parameter **OutParameters)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(NVSDK_NGX_Parameter **);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D12_AllocateParameters")))
                return pfn(OutParameters);
        }
        if (!OutParameters) return NVSDK_NGX_Result_Fail;
        *OutParameters = new NVSDK_NGX_Parameter_Impl();
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D12_DestroyParameters(NVSDK_NGX_Parameter *InParameters)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(NVSDK_NGX_Parameter *);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D12_DestroyParameters")))
                return pfn(InParameters);
        }
        delete static_cast<NVSDK_NGX_Parameter_Impl*>(InParameters);
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D12_GetCapabilityParameters(NVSDK_NGX_Parameter **OutParameters)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(NVSDK_NGX_Parameter **);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D12_GetCapabilityParameters")))
                return pfn(OutParameters);
        }
        if (!OutParameters) return NVSDK_NGX_Result_Fail;
        *OutParameters = &g_capabilityParameters;
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D12_GetParameters(NVSDK_NGX_Parameter **OutParameters)
    {
        return NVSDK_NGX_D3D12_GetCapabilityParameters(OutParameters);
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D12_Init(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, ID3D12Device *InDevice, const void *InFeatureInfo, unsigned int InSDKVersion)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(unsigned long long, const wchar_t *, ID3D12Device *, const void *, unsigned int);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D12_Init")))
                return pfn(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
        }
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_Ext(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, ID3D12Device *InDevice, const void *InFeatureInfo, unsigned int InSDKVersion, void *InExt)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(unsigned long long, const wchar_t *, ID3D12Device *, const void *, unsigned int, void *);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D12_Init_Ext")))
                return pfn(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion, InExt);
        }
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_with_ProjectID(const char *InProjectId, int InEngineType, const char *InEngineVersion, const wchar_t *InApplicationDataPath, ID3D12Device *InDevice, const void *InFeatureInfo, unsigned int InSDKVersion)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(const char *, int, const char *, const wchar_t *, ID3D12Device *, const void *, unsigned int);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D12_Init_with_ProjectID")))
                return pfn(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
        }
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D12_CreateFeature(ID3D12GraphicsCommandList *InCmdList, int InFeatureID, const NVSDK_NGX_Parameter *InParameters, void **OutHandle)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(ID3D12GraphicsCommandList *, int, const NVSDK_NGX_Parameter *, void **);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D12_CreateFeature")))
                return pfn(InCmdList, InFeatureID, InParameters, OutHandle);
        }
        if (OutHandle) *OutHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1337));
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D12_ReleaseFeature(void *InHandle)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(void *);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D12_ReleaseFeature")))
                return pfn(InHandle);
        }
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D12_EvaluateFeature(ID3D12GraphicsCommandList *InCmdList, const void *InHandle, const NVSDK_NGX_Parameter *InParameters, void *InCallback)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(ID3D12GraphicsCommandList *, const void *, const NVSDK_NGX_Parameter *, void *);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D12_EvaluateFeature")))
                return pfn(InCmdList, InHandle, InParameters, InCallback);
        }
        return NVSDK_NGX_Result_Success;
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D12_EvaluateFeature_C(ID3D12GraphicsCommandList *InCmdList, const void *InHandle, const NVSDK_NGX_Parameter *InParameters, void *InCallback)
    {
        return NVSDK_NGX_D3D12_EvaluateFeature(InCmdList, InHandle, InParameters, InCallback);
    }

    NVSDK_NGX_Result NVSDK_NGX_D3D12_Shutdown1(ID3D12Device *InDevice)
    {
        if (HMODULE h = GetRealNvngx())
        {
            using Fn = NVSDK_NGX_Result(*)(ID3D12Device *);
            if (auto pfn = reinterpret_cast<Fn>(GetProcAddress(h, "NVSDK_NGX_D3D12_Shutdown1")))
                return pfn(InDevice);
        }
        return NVSDK_NGX_Result_Success;
    }
}

