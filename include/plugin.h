#pragma once

// ---------------------------------------------------------------------------
// Plugin identity
// ---------------------------------------------------------------------------
#define XPLAPI_PLUGIN_NAME  "xplapi"
#define XPLAPI_PLUGIN_SIG   "eu.me2d.xplapi"
#define XPLAPI_PLUGIN_DESC  "X-Plane 12 REST API – datarefs and commands over HTTP"
#define XPLAPI_VERSION      "0.1.0"

// ---------------------------------------------------------------------------
// Platform guard – IBM = Windows, APL = macOS, LIN = Linux
// These must be defined before including any XPLM header.
// ---------------------------------------------------------------------------
#if !defined(IBM) && !defined(APL) && !defined(LIN)
    #define IBM 1
#endif

// PLUGIN_API is defined by XPLMDefs.h; suppress any earlier definition  
// from windows.h to avoid the C4005 macro-redefinition warning.
#ifdef PLUGIN_API
    #undef PLUGIN_API
#endif
