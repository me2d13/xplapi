#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include "XPLMDataAccess.h"

// ---------------------------------------------------------------------------
// Internal value cache for one X-Plane dataref
// ---------------------------------------------------------------------------
union XplValue {
    float fValue;
    int   iValue;
    float fArrayValue[64];
    int   iArrayValue[64];
    char  cArrayValue[256];
};

struct DataRefEntry {
    XPLMDataRef dataRef  { nullptr };
    int         type     { 0 };       // xplmType_* bitmask
    int         count    { 0 };       // for array types
    XplValue    value    {};
    bool        found    { false };   // false = XPLMFindDataRef returned null
};

// ---------------------------------------------------------------------------
// DataRefRegistry
//
// Thread-safety model:
//   - Lookup (XPLMFindDataRef) and reads (XPLMGetData*) MUST happen on the
//     X-Plane flight-loop thread.  The WebServer thread calls readValue()
//     to request a read; the flight loop calls update() every tick which
//     actually performs the XPLM calls and stores results.
//   - A single mutex protects the registry map.  The flight loop holds it
//     briefly while writing values; the HTTP thread holds it briefly while
//     reading them.
// ---------------------------------------------------------------------------
class DataRefRegistry {
public:
    // Called by HTTP handler (any thread): ensure the name is tracked.
    // Returns false if the name is already known to not exist in X-Plane.
    void ensureTracked(const std::string& name);

    // Called by X-Plane flight loop (main thread): resolve pending lookups
    // and refresh cached values for all tracked datarefs.
    void update();

    // Called by HTTP handler: read the current cached value for one name.
    // Returns false if not found / not yet resolved.
    bool readValue(const std::string& name, DataRefEntry& out) const;

    // Snapshot of all currently tracked entries (for status page)
    struct SnapEntry {
        std::string name;
        bool        found;
        int         type;
        int         count;
    };
    std::vector<SnapEntry> snapshot() const;

private:
    mutable std::mutex                            m_mutex;
    std::unordered_map<std::string, DataRefEntry> m_registry;

    // Internal: must be called on XP main thread, mutex NOT held
    bool tryResolve(const std::string& name, DataRefEntry& entry);
    void readXplValue(DataRefEntry& entry);
};
