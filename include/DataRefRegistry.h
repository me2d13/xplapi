#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <map>
#include "XPLMDataAccess.h"
#include "json.hpp"

using json = nlohmann::json;

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
    XPLMDataRef dataRef   { nullptr };
    int         type      { 0 };       // xplmType_* bitmask
    int         count     { 0 };       // for array types
    XplValue    value     {};
    bool        attempted { false };   // true once the flight loop has called XPLMFindDataRef
    bool        found     { false };   // true if XPLMFindDataRef succeeded
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
        bool        attempted;
        bool        found;
        int         type;
        int         count;
        std::string valueDisplay;  // pre-formatted for the status page; empty if not yet resolved
    };

    /**
     * Blocks the calling thread until the next update() cycle completes
     * or the timeout is reached. Returns true if an update happened.
     */
    bool waitForUpdate(int timeoutMs);

    /**
     * Queues a write request to be processed by the next flight loop update.
     */
    void queueWrite(const std::string& name, const json& value);

    std::vector<SnapEntry> snapshot() const;

    // New: Write support
    struct WriteRequest {
        std::string name;
        json        value; // Using json type directly for flexibility
    };

    mutable std::mutex                  m_mutex;
    std::condition_variable             m_cv;
    uint64_t                            m_updateCounter{ 0 };
    std::map<std::string, DataRefEntry> m_registry;
    std::vector<WriteRequest>           m_writeQueue;

    // Internal: must be called on XP main thread, mutex NOT held
    bool tryResolve(const std::string& name, DataRefEntry& entry);
    void readXplValue(DataRefEntry& entry);
    void writeXplValue(DataRefEntry& entry, const json& value);
};
