#include "plugin.h"
#include "Config.h"
#include "DataRefRegistry.h"
#include "WebServer.h"

#include "XPLMPlugin.h"
#include "XPLMUtilities.h"
#include "XPLMProcessing.h"

#include <windows.h>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------
static Config           g_config;
static DataRefRegistry  g_registry;
static WebServer        g_webServer;
static std::thread      g_webThread;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static float FlightLoopCallback(float, float, int, void*);

// ---------------------------------------------------------------------------
// XPluginStart
// ---------------------------------------------------------------------------
PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc)
{
    strcpy(outName, XPLAPI_PLUGIN_NAME);
    strcpy(outSig,  XPLAPI_PLUGIN_SIG);
    strcpy(outDesc, XPLAPI_PLUGIN_DESC);

    // Resolve the directory that contains this .xpl file
    char pluginPath[512]{};
    XPLMPluginID myID = XPLMGetMyID();
    XPLMGetPluginInfo(myID, nullptr, pluginPath, nullptr, nullptr);
    XPLMExtractFileAndPath(pluginPath);   // strips filename, leaves dir
    std::string pluginDir(pluginPath);

    XPLMDebugString(("xplapi: plugin dir = " + pluginDir + "\n").c_str());

    // Load config (falls back to defaults if file is missing)
    g_config.load(pluginDir);

    // Wire up the web server
    g_webServer.setPort(g_config.port());
    g_webServer.setRegistry(&g_registry);
    g_webServer.setPluginDir(pluginDir);

    if (g_webServer.init() != 0) {
        XPLMDebugString("xplapi: FAILED to bind web server\n");
        // Non-fatal: plugin still loads, X-Plane datarefs untouched
    } else {
        XPLMDebugString(("xplapi: web server listening on port " +
                         std::to_string(g_config.port()) + "\n").c_str());
        g_webThread = std::thread([]() { g_webServer.run(); });
    }

    // Register flight loop — called every 100 ms
    XPLMRegisterFlightLoopCallback(FlightLoopCallback, 0.1f, nullptr);

    return 1;
}

// ---------------------------------------------------------------------------
// XPluginStop
// ---------------------------------------------------------------------------
PLUGIN_API void XPluginStop()
{
    XPLMUnregisterFlightLoopCallback(FlightLoopCallback, nullptr);

    if (g_webThread.joinable()) {
        g_webServer.stop();
        g_webThread.join();
    }

    XPLMDebugString("xplapi: stopped\n");
}

// ---------------------------------------------------------------------------
// XPluginEnable / XPluginDisable
// ---------------------------------------------------------------------------
PLUGIN_API int  XPluginEnable()  { return 1; }
PLUGIN_API void XPluginDisable() {}

// ---------------------------------------------------------------------------
// XPluginReceiveMessage
// ---------------------------------------------------------------------------
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void*) {}

// ---------------------------------------------------------------------------
// FlightLoopCallback — runs on XP main thread every 100 ms
// This is the only place we call XPLM dataref functions.
// ---------------------------------------------------------------------------
static float FlightLoopCallback(float, float, int, void*)
{
    g_registry.update();
    return 0.1f;
}
