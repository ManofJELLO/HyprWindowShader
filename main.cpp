// 1. ABSOLUTE FIRST: Include native GLES3. 
#include <GLES3/gl32.h>
#include <functional>
#include <any>
#include <string>
#include <vector>
#include <stdexcept>

// --- DARKWINDOW FIX 1 (AMENDED): CLEAN INCLUDES ---
// We completely removed the `#define private public` hack. It breaks modern 
// GCC C++15 STL headers. We will track the rendering window natively instead!
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>

// --- CRITICAL FIX 5: PHYSICAL MEMORY HOOKING ---
// We are bypassing the event system entirely using physical memory hooks.
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/desktop/view/Window.hpp> 

// --- LAYER SURFACE SUPPORT (FIXED) ---
// In v0.54.2, LayerSurface was moved into the view/ subdirectory alongside Window.hpp
#include <hyprland/src/desktop/view/LayerSurface.hpp>

#include <hyprland/src/event/EventBus.hpp>
#include <hyprutils/memory/UniquePtr.hpp>

#include <fstream>
#include <sstream>
#include <map>
#include <chrono>
#include <sys/stat.h>
#include <iostream> 

// --- SAFEGUARD 1: ABI VERSION SHIELD ---
const std::string TARGET_HYPRLAND_VERSION = "v0.54.2"; 

inline HANDLE PHANDLE = nullptr;

// Native event listeners must be stored so they aren't destroyed
std::vector<CHyprSignalListener> g_Listeners;

// Use raw CWindow* to completely decouple from PHLWINDOW smart pointer issues
std::map<Desktop::View::CWindow*, std::string> g_mWindowShaderMap;

// Map for associating Layer Surface namespaces to shader paths
std::map<std::string, std::string> g_mLayerNamespaceShaderMap;

// --- CRITICAL FIX 50: NATIVE CSHADER CACHE ---
std::map<std::string, Hyprutils::Memory::CSharedPointer<CShader>> g_mCompiledCShaders;


// --- HELPER: SHADER COMPILATION ---
// Extracts the compilation logic so both Windows and Layers can use it cleanly.
Hyprutils::Memory::CSharedPointer<CShader> getOrCompileShader(CHyprOpenGLImpl* thisptr, const std::string& shaderPath) {
    if (g_mCompiledCShaders.find(shaderPath) == g_mCompiledCShaders.end()) {
        std::ifstream shaderFile(shaderPath);
        if (shaderFile.is_open()) {
            std::stringstream buffer;
            buffer << shaderFile.rdbuf();
            
            auto customShader = Hyprutils::Memory::makeShared<CShader>();
            
            // --- CRITICAL FIX 53: MATCH GLES VERSIONS ---
            // Because pixelate.glsl uses #version 320 es, we MUST link it
            // against Hyprland's TEXVERTSRC320 vertex shader!
            customShader->createProgram(thisptr->m_shaders->TEXVERTSRC320, buffer.str(), true, true);
            g_mCompiledCShaders[shaderPath] = customShader;
            
            if (customShader->program() == 0) {
                HyprlandAPI::addNotification(PHANDLE, "[HyprWindowShade] Shader Compile FAILED!", CHyprColor(1.0f, 0.0f, 0.0f, 1.0f), 10000.0f);
            }
        } else {
            return nullptr; // File not found
        }
    }
    return g_mCompiledCShaders[shaderPath];
}

// --- DARKWINDOW FIX 3 (AMENDED): Hooking getSurfaceShader ---
inline CFunctionHook* g_pGetSurfaceShaderHook = nullptr;
typedef Hyprutils::Memory::CWeakPointer<CShader> (*TGetSurfaceShader)(CHyprOpenGLImpl* thisptr, uint8_t features);

Hyprutils::Memory::CWeakPointer<CShader> hkGetSurfaceShader(CHyprOpenGLImpl* thisptr, uint8_t features) {
    
    // --- CRITICAL FIX 52: NATIVE RENDER DATA TRACKING ---
    // Hyprland v0.54.2 uses an async Render Pass system. We extract the active
    // window OR layer directly from the OpenGL implementation's native state machine!
    auto window = thisptr->m_renderData.currentWindow.lock();
    auto layer  = thisptr->m_renderData.currentLS.lock();

    // --- THE FIX: GETSURFACESHADER ---
    // getShaderVariant is gone! Vaxry replaced it with getSurfaceShader, which 
    // ONLY targets the textured window surface. We drop the 'frag' enum entirely!
    
    if (window) {
        Desktop::View::CWindow* rawWin = window.get();
        if (g_mWindowShaderMap.find(rawWin) != g_mWindowShaderMap.end()) {
            auto customShader = getOrCompileShader(thisptr, g_mWindowShaderMap[rawWin]);
            if (customShader && customShader->program() != 0) return customShader;
        }
    } 
    else if (layer) {
        // --- LAYER NAMESPACE FIX ---
        // Updated to use 'm_namespace' as per the v0.54.2 LayerSurface.hpp
        std::string ns = layer->m_namespace;
        if (g_mLayerNamespaceShaderMap.find(ns) != g_mLayerNamespaceShaderMap.end()) {
            auto customShader = getOrCompileShader(thisptr, g_mLayerNamespaceShaderMap[ns]);
            if (customShader && customShader->program() != 0) return customShader;
        }
    }
    
    // Pass through to the original Hyprland shader builder for non-tagged elements
    return ((TGetSurfaceShader)g_pGetSurfaceShaderHook->m_original)(thisptr, features);
}

void applyShaderRulesSafe(PHLWINDOW pWindow) {
    if (!pWindow || !pWindow->m_ruleApplicator) return;
    Desktop::View::CWindow* rawWin = pWindow.get();

    if (g_mWindowShaderMap.find(rawWin) != g_mWindowShaderMap.end()) return;

    const auto& tagsSet = pWindow->m_ruleApplicator->m_tagKeeper.getTags();
    for (const auto& tag : tagsSet) {
        if (tag.find("shader:") != std::string::npos) {
            std::string shaderPath = tag.substr(tag.find("shader:") + 7);
            while (!shaderPath.empty() && (shaderPath.back() == '*' || shaderPath.back() == ' ')) shaderPath.pop_back();

            g_mWindowShaderMap[rawWin] = shaderPath;
            
            // --- DARKWINDOW FIX 2: Force Window Redraw ---
            g_pHyprRenderer->damageWindow(pWindow);
            return;
        }
    }
}

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    // 2. Hook getSurfaceShader to perform the injection
    auto methodsVariant = HyprlandAPI::findFunctionsByName(PHANDLE, "getSurfaceShader");
    void* getVariantAddr = nullptr;
    for (auto& m : methodsVariant) {
        if (m.demangled.find("CHyprOpenGLImpl::getSurfaceShader") != std::string::npos) {
            getVariantAddr = m.address;
            break;
        }
    }
    if (getVariantAddr) {
        g_pGetSurfaceShaderHook = HyprlandAPI::createFunctionHook(PHANDLE, getVariantAddr, (void*)&hkGetSurfaceShader);
        g_pGetSurfaceShaderHook->hook();
    } else {
        HyprlandAPI::addNotification(PHANDLE, "[HyprWindowShade] FATAL: getSurfaceShader not found!", CHyprColor(1.0f, 0.0f, 0.0f, 1.0f), 10000.0f);
    }

    // 3. Listen to native rules
    g_Listeners.push_back(Event::bus()->m_events.window.updateRules.listen([&](PHLWINDOW window) {
        try { applyShaderRulesSafe(window); } catch (...) {}
    }));

    // 4. Custom Dispatcher for Layer Surfaces
    HyprlandAPI::addDispatcherV2(PHANDLE, "layershader", [&](std::string args) -> SDispatchResult {
        size_t spacePos = args.find_first_of(" \t");
        if (spacePos != std::string::npos) {
            std::string ns = args.substr(0, spacePos);
            std::string path = args.substr(spacePos + 1);
            
            // Trim path spaces
            size_t start = path.find_first_not_of(" \t");
            if (start != std::string::npos) path = path.substr(start);
            while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();

            if (path == "clear" || path == "none") {
                g_mLayerNamespaceShaderMap.erase(ns);
            } else {
                g_mLayerNamespaceShaderMap[ns] = path;
            }
        }
        return SDispatchResult{};
    });

    // --- CRITICAL FIX 56: TOGGLE DISPATCHER ---
    // Added a dedicated toggle dispatcher to easily switch shaders on and off with a single keybind!
    HyprlandAPI::addDispatcherV2(PHANDLE, "togglelayershader", [&](std::string args) -> SDispatchResult {
        size_t spacePos = args.find_first_of(" \t");
        if (spacePos != std::string::npos) {
            std::string ns = args.substr(0, spacePos);
            std::string path = args.substr(spacePos + 1);
            
            // Trim path spaces
            size_t start = path.find_first_not_of(" \t");
            if (start != std::string::npos) path = path.substr(start);
            while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();

            // Toggle logic: remove it if it exists, apply it if it doesn't
            if (g_mLayerNamespaceShaderMap.find(ns) != g_mLayerNamespaceShaderMap.end()) {
                g_mLayerNamespaceShaderMap.erase(ns);
            } else {
                g_mLayerNamespaceShaderMap[ns] = path;
            }
        }
        return SDispatchResult{};
    });

    return {"HyprWindowShade", "Native CShader Injection", "ManofJELLO", "1.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_Listeners.clear();
    
    // --- CRITICAL FIX 54: STRICT MEMORY UNLOADING ---
    g_mWindowShaderMap.clear();
    g_mLayerNamespaceShaderMap.clear();
    g_mCompiledCShaders.clear();

    if (g_pGetSurfaceShaderHook) {
        g_pGetSurfaceShaderHook->unhook();
        HyprlandAPI::removeFunctionHook(PHANDLE, g_pGetSurfaceShaderHook);
    }

    // --- CRITICAL FIX 55: DISPATCHER CLEANUP ---
    // We MUST unregister our custom dispatchers. Otherwise, Hyprland holds 
    // function pointers to our memory, completely blocking the unload process!
    HyprlandAPI::removeDispatcher(PHANDLE, "layershader");
    HyprlandAPI::removeDispatcher(PHANDLE, "togglelayershader");
}