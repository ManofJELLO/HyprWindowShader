#include <GL/glew.h>        // Required for OpenGL symbols and extensions
#include <GL/gl.h>          // Standard OpenGL header
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/desktop/Window.hpp> 
#include <hyprland/src/render/Renderer.hpp>
#include <fstream>
#include <sstream>
#include <map>
#include <chrono>

inline HANDLE PHANDLE = nullptr;
auto startTime = std::chrono::high_resolution_clock::now();

struct SWindowShader {
    GLuint programID = 0;
    GLint  locPos    = -1;
    GLint  locSize   = -1;
    GLint  locTime   = -1;
    GLint  locTex    = -1;
};

std::map<std::string, SWindowShader> g_mCompiledShaders;
std::map<PHLWINDOW, std::string> g_mWindowShaderMap;

// --- COMPATIBLE SHADER WRAPPER ---
// Uses a pass-through vertex shader to support v_texcoord and fragColor for GLES 3.20
SWindowShader compileShader(const std::string& path) {
    SWindowShader shaderData;

    // Minimal Pass-through Vertex Shader
    // This provides the 'v_texcoord' that pixelate.glsl expects
    const char* vertSource = R"(
        #version 320 es
        precision highp float;
        layout (location = 0) in vec2 a_pos;
        layout (location = 1) in vec2 a_tex;
        out vec2 v_texcoord;
        void main() {
            gl_Position = vec4(a_pos, 0.0, 1.0);
            v_texcoord = a_tex;
        }
    )";

    // Load Fragment Shader from file (located in ~/.config/hypr/shaders/)
    std::ifstream shaderFile(path);
    if (!shaderFile.is_open()) {
        Debug::log(ERR, "[HyprWindowShade] Failed to open shader at: {}", path);
        return shaderData;
    }

    std::stringstream buffer;
    buffer << shaderFile.rdbuf();
    std::string fragSource = buffer.str();
    const char* fragSrcPtr = fragSource.c_str();

    // Compile Vertex Shader
    GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShader, 1, &vertSource, nullptr);
    glCompileShader(vertShader);

    // Compile Fragment Shader
    GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragShader, 1, &fragSrcPtr, nullptr);
    glCompileShader(fragShader);

    // Link Program
    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);
    
    // Cache locations - Matching pixelate.glsl 'tex' sampler
    shaderData.programID = program;
    shaderData.locTex    = glGetUniformLocation(program, "tex"); 
    shaderData.locPos    = glGetUniformLocation(program, "pos");
    shaderData.locSize   = glGetUniformLocation(program, "size");
    shaderData.locTime   = glGetUniformLocation(program, "time");

    glDeleteShader(vertShader);
    glDeleteShader(fragShader); 
    
    Debug::log(LOG, "[HyprWindowShade] Successfully compiled: {}", path);
    return shaderData;
}

void applyShaderRules(PHLWINDOW pWindow) {
    if (!pWindow) return;

    const auto& RULES = HyprlandAPI::getWindowRules(PHANDLE, pWindow);
    
    for (auto& rule : RULES) {
        if (rule.first.starts_with("plugin:shader:")) {
            std::string shaderPath = rule.first.substr(14); 
            
            if (g_mCompiledShaders.find(shaderPath) == g_mCompiledShaders.end()) {
                g_mCompiledShaders[shaderPath] = compileShader(shaderPath);
            }
            
            g_mWindowShaderMap[pWindow] = shaderPath;
            break; 
        }
    }
}

// --- PRE-RENDER HOOK ---
void OnBeforeRenderWindow(void* self, std::any data) {
    auto pWindow = std::any_cast<PHLWINDOW>(data);

    if (pWindow && g_mWindowShaderMap.find(pWindow) == g_mWindowShaderMap.end()) {
        applyShaderRules(pWindow);
    }

    if (!pWindow || g_mWindowShaderMap.find(pWindow) == g_mWindowShaderMap.end())
        return;

    SWindowShader& shader = g_mCompiledShaders[g_mWindowShaderMap[pWindow]];

    const auto POS  = pWindow->m_vRealPosition.goal();
    const auto SIZE = pWindow->m_vRealSize.goal();
    float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

    // Activate custom shader and inject uniforms
    glUseProgram(shader.programID);
    if (shader.locPos != -1)  glUniform2f(shader.locPos, POS.x, POS.y);
    if (shader.locSize != -1) glUniform2f(shader.locSize, SIZE.x, SIZE.y);
    if (shader.locTime != -1) glUniform1f(shader.locTime, time);
    if (shader.locTex != -1)  glUniform1i(shader.locTex, 0); 
}

// --- POST-RENDER HOOK ---
// Safely restores Hyprland's default shader state
void OnAfterRenderWindow(void* self, std::any data) {
    auto pWindow = std::any_cast<PHLWINDOW>(data);

    if (pWindow && g_mWindowShaderMap.count(pWindow)) {
        glUseProgram(0);
    }
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    glewInit(); 

    HyprlandAPI::registerCallback(PHANDLE, "renderWindow", OnBeforeRenderWindow);
    HyprlandAPI::registerCallback(PHANDLE, "renderWindowPost", OnAfterRenderWindow);

    return {
        "HyprWindowShade",
        "Per-window GLSL shader application",
        "YourName",
        "1.0"
    };
}

APICALL EXPORT void PLUGIN_EXIT() {
    for (auto const& [path, shader] : g_mCompiledShaders) {
        if (shader.programID != 0)
            glDeleteProgram(shader.programID);
    }
    g_mCompiledShaders.clear();
    g_mWindowShaderMap.clear();
}
