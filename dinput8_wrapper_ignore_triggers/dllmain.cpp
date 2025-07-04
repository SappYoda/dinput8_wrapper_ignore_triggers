// dinput8_wrapper.cpp
//
// A DirectInput8 wrapper (proxy DLL) designed to filter out specific joystick axes.
// Target Problem: Some games incorrectly interpret the X/Y Rotation axes of controllers
// like the DualShock 4 and DualSense as primary stick inputs, causing camera spin
// or other unwanted behavior.
// Solution: This wrapper intercepts the data retrieval calls to DirectInput and sets
// the values for the X/Y Rotation axes to zero before passing the data to the game.
//
// How to Compile:
// 1. Use Visual Studio with the "Desktop development with C++" workload.
// 2. Create a new "Dynamic-Link Library (DLL)" project.
// 3. Add this file (`dinput8_wrapper.cpp`) and the `dinput8.def` file to the project.
// 4. Go to Project Properties -> Linker -> Input -> Module Definition File and enter "dinput8.def".
// 5. Make sure to link against `dinput8.lib` and `dxguid.lib`.
//    (Project Properties -> Linker -> Input -> Additional Dependencies).
// 6. Build the project for the target architecture (x86 for 32-bit games, x64 for 64-bit games).
//
// How to Use:
// 1. Copy the compiled DLL into the same directory as the game's main executable.
// 2. Rename the compiled DLL to "dinput8.dll".
// 3. The game will now load this wrapper instead of the system's original DLL.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>
#include <vector>
#include <string>
#include <fstream> // LOGGING: Include for file writing
#include <chrono>  // LOGGING: Include for timestamps
#include <ctime>   // LOGGING: Include for timestamps
#include <sstream>
#include <iostream>
#include <iomanip>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

// LOGGING: Simple function to write messages to a log file.
void Log(const std::string& message) {
    // Check environment variable DINPUT8_LOG_ENABLE
    char envBuffer[16];
    DWORD result = GetEnvironmentVariableA("DINPUT8_LOG_ENABLE", envBuffer, sizeof(envBuffer));

    if (result > 0 && (std::string(envBuffer) == "1" || _stricmp(envBuffer, "true") == 0)) {
        std::ofstream log_file("dinput8-wrapper.log", std::ios_base::out | std::ios_base::app);
        if (log_file.is_open()) {
            auto now = std::chrono::system_clock::now();
            std::time_t time = std::chrono::system_clock::to_time_t(now);
            char time_str[26];
            ctime_s(time_str, sizeof(time_str), &time);
            time_str[strlen(time_str) - 1] = '\0'; // Remove newline
            log_file << "[" << time_str << "] " << message << std::endl;
        }
    }
}

// Forward declarations for our wrapper classes
class WrapperIDirectInput8A;
class WrapperIDirectInputDevice8A;

// Global pointer to the real DirectInput8Create function
typedef HRESULT(WINAPI* DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8Create_t g_pfnDirectInput8Create = nullptr;

// --- Wrapper for IDirectInputDevice8A ---
// This class intercepts the device-specific calls. Note the explicit 'A' for ANSI.
class WrapperIDirectInputDevice8A : public IDirectInputDevice8A {
private:
    IDirectInputDevice8A* m_pRealDevice;
    LPDIDATAFORMAT m_pDataFormat = nullptr;
    DWORD m_dwRxOffset = (DWORD)-1;
    DWORD m_dwRyOffset = (DWORD)-1;
    DWORD m_dwZOffset = (DWORD)-1; // Added for triggers

public:
    WrapperIDirectInputDevice8A(IDirectInputDevice8A* pRealDevice) : m_pRealDevice(pRealDevice) {
        Log("WrapperIDirectInputDevice8A created.");
    }
    ~WrapperIDirectInputDevice8A() {
        if (m_pDataFormat) {
            delete[] m_pDataFormat->rgodf;
            delete m_pDataFormat;
        }
        Log("WrapperIDirectInputDevice8A destroyed.");
    }

    // --- IUnknown methods ---
    HRESULT __stdcall QueryInterface(REFIID riid, LPVOID* ppvObj) override {
        if (riid == IID_IUnknown || riid == IID_IDirectInputDevice8A) {
            *ppvObj = this;
            AddRef();
            return S_OK;
        }
        return m_pRealDevice->QueryInterface(riid, ppvObj);
    }

    ULONG __stdcall AddRef() override {
        return m_pRealDevice->AddRef();
    }

    ULONG __stdcall Release() override {
        ULONG uRet = m_pRealDevice->Release();
        if (uRet == 0) {
            delete this;
        }
        return uRet;
    }

    // --- IDirectInputDevice8A methods ---
    HRESULT __stdcall GetCapabilities(LPDIDEVCAPS lpDIDevCaps) override {
        return m_pRealDevice->GetCapabilities(lpDIDevCaps);
    }

    HRESULT __stdcall EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) override {
        return m_pRealDevice->EnumObjects(lpCallback, pvRef, dwFlags);
    }

    HRESULT __stdcall GetProperty(REFGUID rguidProp, LPDIPROPHEADER pdiph) override {
        return m_pRealDevice->GetProperty(rguidProp, pdiph);
    }

    HRESULT __stdcall SetProperty(REFGUID rguidProp, LPCDIPROPHEADER pdiph) override {
        return m_pRealDevice->SetProperty(rguidProp, pdiph);
    }

    HRESULT __stdcall Acquire() override {
        Log("Acquire() called.");
        return m_pRealDevice->Acquire();
    }

    HRESULT __stdcall Unacquire() override {
        Log("Unacquire() called.");
        return m_pRealDevice->Unacquire();
    }

    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD cbData, LPVOID lpvData) override {
        HRESULT hr = m_pRealDevice->GetDeviceState(cbData, lpvData);
        if (SUCCEEDED(hr) && cbData == sizeof(DIJOYSTATE)) {
            // Zero out rotational X and Y (Rx and Ry) for 6DOF device
            DIJOYSTATE* state = static_cast<DIJOYSTATE*>(lpvData);
            state->lRx = 0;
            state->lRy = 0;
        }
        return hr;
    }

    HRESULT __stdcall GetDeviceData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) override {
        return m_pRealDevice->GetDeviceData(cbObjectData, rgdod, pdwInOut, dwFlags);
    }

    HRESULT __stdcall SetDataFormat(LPCDIDATAFORMAT lpdf) override {
        return m_pRealDevice->SetDataFormat(lpdf);
    }

    HRESULT __stdcall SetEventNotification(HANDLE hEvent) override {
        return m_pRealDevice->SetEventNotification(hEvent);
    }

    HRESULT __stdcall SetCooperativeLevel(HWND hwnd, DWORD dwFlags) override {
        return m_pRealDevice->SetCooperativeLevel(hwnd, dwFlags);
    }

    HRESULT __stdcall GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA pdidoi, DWORD dwObj, DWORD dwHow) override {
        return m_pRealDevice->GetObjectInfo(pdidoi, dwObj, dwHow);
    }

    HRESULT __stdcall GetDeviceInfo(LPDIDEVICEINSTANCEA pdidi) override {
        return m_pRealDevice->GetDeviceInfo(pdidi);
    }

    HRESULT __stdcall RunControlPanel(HWND hwndOwner, DWORD dwFlags) override {
        return m_pRealDevice->RunControlPanel(hwndOwner, dwFlags);
    }

    HRESULT __stdcall Initialize(HINSTANCE hinst, DWORD dwVersion, REFGUID rguid) override {
        return m_pRealDevice->Initialize(hinst, dwVersion, rguid);
    }

    HRESULT __stdcall CreateEffect(REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT* ppdeff, LPUNKNOWN punkOuter) override {
        return m_pRealDevice->CreateEffect(rguid, lpeff, ppdeff, punkOuter);
    }

    HRESULT __stdcall EnumEffects(LPDIENUMEFFECTSCALLBACKA lpCallback, LPVOID pvRef, DWORD dwEffType) override {
        return m_pRealDevice->EnumEffects(lpCallback, pvRef, dwEffType);
    }

    HRESULT __stdcall GetEffectInfo(LPDIEFFECTINFOA pdei, REFGUID rguid) override {
        return m_pRealDevice->GetEffectInfo(pdei, rguid);
    }

    HRESULT __stdcall GetForceFeedbackState(LPDWORD pdwOut) override {
        return m_pRealDevice->GetForceFeedbackState(pdwOut);
    }

    HRESULT __stdcall SendForceFeedbackCommand(DWORD dwFlags) override {
        return m_pRealDevice->SendForceFeedbackCommand(dwFlags);
    }

    HRESULT __stdcall EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl) override {
        return m_pRealDevice->EnumCreatedEffectObjects(lpCallback, pvRef, fl);
    }

    HRESULT __stdcall Escape(LPDIEFFESCAPE pesc) override {
        return m_pRealDevice->Escape(pesc);
    }

    HRESULT __stdcall Poll() override {
        return m_pRealDevice->Poll();
    }

    HRESULT __stdcall SendDeviceData(DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl) override {
        return m_pRealDevice->SendDeviceData(cbObjectData, rgdod, pdwInOut, fl);
    }

    HRESULT __stdcall EnumEffectsInFile(LPCSTR lpszFileName, LPDIENUMEFFECTSINFILECALLBACK pec, LPVOID pvRef, DWORD dwFlags) override {
        return m_pRealDevice->EnumEffectsInFile(lpszFileName, pec, pvRef, dwFlags);
    }

    HRESULT __stdcall WriteEffectToFile(LPCSTR lpszFileName, DWORD dwEntries, LPDIFILEEFFECT rgDiFileEft, DWORD dwFlags) override {
        return m_pRealDevice->WriteEffectToFile(lpszFileName, dwEntries, rgDiFileEft, dwFlags);
    }

    HRESULT __stdcall BuildActionMap(LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags) override {
        return m_pRealDevice->BuildActionMap(lpdiaf, lpszUserName, dwFlags);
    }

    HRESULT __stdcall SetActionMap(LPDIACTIONFORMATA lpdiaf, LPCSTR lpszUserName, DWORD dwFlags) override {
        return m_pRealDevice->SetActionMap(lpdiaf, lpszUserName, dwFlags);
    }

    HRESULT __stdcall GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA lpdiDevImageInfoHeader) override {
        return m_pRealDevice->GetImageInfo(lpdiDevImageInfoHeader);
    }
};

// --- Wrapper for IDirectInput8A ---
class WrapperIDirectInput8A : public IDirectInput8A {
private:
    IDirectInput8A* m_pRealDInput;

public:
    WrapperIDirectInput8A(IDirectInput8A* pRealDInput) : m_pRealDInput(pRealDInput) {}

    HRESULT __stdcall QueryInterface(REFIID riid, LPVOID* ppvObj) override {
        if (riid == IID_IUnknown || riid == IID_IDirectInput8A) {
            *ppvObj = this;
            AddRef();
            return S_OK;
        }
        return m_pRealDInput->QueryInterface(riid, ppvObj);
    }

    ULONG __stdcall AddRef() override {
        return m_pRealDInput->AddRef();
    }

    ULONG __stdcall Release() override {
        ULONG uRet = m_pRealDInput->Release();
        if (uRet == 0) {
            delete this;
        }
        return uRet;
    }

    HRESULT __stdcall CreateDevice(REFGUID rguid, LPDIRECTINPUTDEVICE8A* lplpDirectInputDevice, LPUNKNOWN pUnkOuter) override {
        Log("CreateDevice() called.");
        IDirectInputDevice8A* pRealDevice = nullptr;
        HRESULT hr = m_pRealDInput->CreateDevice(rguid, &pRealDevice, pUnkOuter);
        if (SUCCEEDED(hr)) {
            DIDEVICEINSTANCEA didi;
            didi.dwSize = sizeof(didi);
            if (SUCCEEDED(pRealDevice->GetDeviceInfo(&didi))) {
                Log("Device Info: " + std::string(didi.tszProductName));

                std::stringstream ss;
                ss << std::hex << std::setw(8) << std::setfill('0') << didi.dwDevType;
                Log("Device Type: 0x" + ss.str());

                if (GET_DIDEVICE_TYPE(didi.dwDevType) == DI8DEVTYPE_1STPERSON && GET_DIDEVICE_SUBTYPE(didi.dwDevType) == DI8DEVTYPE1STPERSON_SIXDOF) {
                    Log("Device is a six degrees of freedom, first-person controller. Wrapping it.");
                    *lplpDirectInputDevice = new WrapperIDirectInputDevice8A(pRealDevice);
                }
                else {
                    Log("Device is not a six degrees of freedom, first-person controller. Passing it through.");
                    *lplpDirectInputDevice = pRealDevice;
                }
            }
            else {
                Log("Could not get device info. Passing it through.");
                *lplpDirectInputDevice = pRealDevice;
            }
        }
        return hr;
    }

    HRESULT __stdcall EnumDevices(DWORD dwDevType, LPDIENUMDEVICESCALLBACKA lpCallback, LPVOID pvRef, DWORD dwFlags) override {
        return m_pRealDInput->EnumDevices(dwDevType, lpCallback, pvRef, dwFlags);
    }

    HRESULT __stdcall GetDeviceStatus(REFGUID rguidInstance) override {
        return m_pRealDInput->GetDeviceStatus(rguidInstance);
    }

    HRESULT __stdcall RunControlPanel(HWND hwndOwner, DWORD dwFlags) override {
        return m_pRealDInput->RunControlPanel(hwndOwner, dwFlags);
    }

    HRESULT __stdcall Initialize(HINSTANCE hinst, DWORD dwVersion) override {
        return m_pRealDInput->Initialize(hinst, dwVersion);
    }

    HRESULT __stdcall FindDevice(REFGUID rguidClass, LPCSTR ptszName, LPGUID pguidInstance) override {
        return m_pRealDInput->FindDevice(rguidClass, ptszName, pguidInstance);
    }

    HRESULT __stdcall EnumDevicesBySemantics(LPCSTR ptszUserName, LPDIACTIONFORMATA lpdiActionFormat, LPDIENUMDEVICESBYSEMANTICSCBA lpCallback, LPVOID pvRef, DWORD dwFlags) override {
        return m_pRealDInput->EnumDevicesBySemantics(ptszUserName, lpdiActionFormat, lpCallback, pvRef, dwFlags);
    }

    HRESULT __stdcall ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK lpdiCallback, LPDICONFIGUREDEVICESPARAMSA lpdiCDParams, DWORD dwFlags, LPVOID pvRefData) override {
        return m_pRealDInput->ConfigureDevices(lpdiCallback, lpdiCDParams, dwFlags, pvRefData);
    }
};

// --- DLL Export ---
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riid, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
    if (!g_pfnDirectInput8Create) {
        char szSystemPath[MAX_PATH];
        GetSystemDirectoryA(szSystemPath, MAX_PATH);
        strcat_s(szSystemPath, "\\dinput8.dll");
        HMODULE hMod = LoadLibraryA(szSystemPath);
        if (!hMod) return E_FAIL;
        g_pfnDirectInput8Create = (DirectInput8Create_t)GetProcAddress(hMod, "DirectInput8Create");
        if (!g_pfnDirectInput8Create) return E_FAIL;
    }

    Log("DirectInput8Create() export called by the game.");

    if (riid != IID_IDirectInput8A) {
        Log("Game requested non-ANSI interface. Passing call to real DLL.");
        return g_pfnDirectInput8Create(hinst, dwVersion, riid, ppvOut, punkOuter);
    }

    IDirectInput8A* pRealDInputA = nullptr;
    HRESULT hr = g_pfnDirectInput8Create(hinst, dwVersion, IID_IDirectInput8A, (LPVOID*)&pRealDInputA, punkOuter);

    if (SUCCEEDED(hr)) {
        Log("Successfully created real DirectInput8 object. Wrapping it now.");
        *ppvOut = new WrapperIDirectInput8A(pRealDInputA);
    }
    else {
        Log("Failed to create real DirectInput8 object. HRESULT: " + std::to_string(hr));
    }

    return hr;
}

// --- DllMain ---
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        // LOGGING: Log when the DLL is first loaded into the game process.
        Log("DLL attached to process.");
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
