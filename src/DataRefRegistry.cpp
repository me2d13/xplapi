#include "DataRefRegistry.h"
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
#include <string>

// ---------------------------------------------------------------------------
// ensureTracked — called from HTTP thread; just inserts an unresolved slot
// ---------------------------------------------------------------------------
void DataRefRegistry::ensureTracked(const std::string& name)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_registry.emplace(name, DataRefEntry{});  // no-op if already present
}

// ---------------------------------------------------------------------------
// tryResolve — MUST be called on XP main thread, mutex NOT held
// ---------------------------------------------------------------------------
bool DataRefRegistry::tryResolve(const std::string& name, DataRefEntry& entry)
{
    entry.attempted = true;   // mark regardless of outcome
    entry.dataRef = XPLMFindDataRef(name.c_str());
    if (entry.dataRef == nullptr) {
        XPLMDebugString(("xplapi: dataref not found: " + name + "\n").c_str());
        entry.found = false;
        return false;
    }
    entry.type  = XPLMGetDataRefTypes(entry.dataRef);
    entry.found = true;

    // For array types, determine element count
    if (entry.type & xplmType_FloatArray)
        entry.count = XPLMGetDatavf(entry.dataRef, nullptr, 0, 0);
    else if (entry.type & xplmType_IntArray)
        entry.count = XPLMGetDatavi(entry.dataRef, nullptr, 0, 0);
    else if (entry.type & xplmType_Data)
        entry.count = XPLMGetDatab(entry.dataRef, nullptr, 0, 0);

    XPLMDebugString(("xplapi: resolved dataref: " + name +
                     " type=" + std::to_string(entry.type) + "\n").c_str());
    return true;
}

// ---------------------------------------------------------------------------
// readXplValue — reads current value from X-Plane; MUST be on XP main thread
// ---------------------------------------------------------------------------
void DataRefRegistry::readXplValue(DataRefEntry& entry)
{
    if (!entry.found || entry.dataRef == nullptr) return;

    if (entry.type & xplmType_Float) {
        entry.value.fValue = XPLMGetDataf(entry.dataRef);
    } else if (entry.type & xplmType_Int) {
        entry.value.iValue = XPLMGetDatai(entry.dataRef);
    } else if (entry.type & xplmType_FloatArray) {
        int n = entry.count < 64 ? entry.count : 64;
        XPLMGetDatavf(entry.dataRef, entry.value.fArrayValue, 0, n);
    } else if (entry.type & xplmType_IntArray) {
        int n = entry.count < 64 ? entry.count : 64;
        XPLMGetDatavi(entry.dataRef, entry.value.iArrayValue, 0, n);
    } else if (entry.type & xplmType_Data) {
        int n = entry.count < 255 ? entry.count : 255;
        XPLMGetDatab(entry.dataRef, entry.value.cArrayValue, 0, n);
        entry.value.cArrayValue[n] = '\0';
    }
}

// ---------------------------------------------------------------------------
// update — called from XP flight loop (main thread) every tick
// ---------------------------------------------------------------------------
void DataRefRegistry::update()
{
    // Collect names of entries that still need resolving (without holding lock)
    std::vector<std::string> toResolve;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& [name, entry] : m_registry) {
            if (!entry.attempted)
                toResolve.push_back(name);
        }
    }

    // Resolve outside the lock (XPLM functions must not be called under our lock
    // since X-Plane itself is single-threaded and we're on the main thread here)
    std::vector<std::pair<std::string, DataRefEntry>> resolved;
    for (const auto& name : toResolve) {
        DataRefEntry e{};
        tryResolve(name, e);
        resolved.emplace_back(name, e);
    }

    // Write resolved entries back + refresh all values
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& [name, e] : resolved)
            m_registry[name] = e;

        for (auto& [name, entry] : m_registry)
            readXplValue(entry);
        
        m_updateCounter++;
    }

    // Wake up any HTTP threads waiting for this update
    m_cv.notify_all();
}

// ---------------------------------------------------------------------------
// waitForUpdate — blocks calling thread until next update cycle
// ---------------------------------------------------------------------------
bool DataRefRegistry::waitForUpdate(int timeoutMs)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    uint64_t startCounter = m_updateCounter;
    return m_cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this, startCounter] {
        return m_updateCounter > startCounter;
    });
}

// ---------------------------------------------------------------------------
// readValue — called from HTTP thread; returns a copy of the cached entry
// ---------------------------------------------------------------------------
bool DataRefRegistry::readValue(const std::string& name, DataRefEntry& out) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_registry.find(name);
    if (it == m_registry.end()) return false;
    out = it->second;
    return it->second.found;
}

// ---------------------------------------------------------------------------
// formatValueDisplay — compact string representation for the status page
// ---------------------------------------------------------------------------
static std::string formatValueDisplay(const DataRefEntry& e)
{
    if (!e.found) return {};

    // Helper to trim trailing zeros from to_string floats
    auto fmtF = [](float v) -> std::string {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6g", v);
        return buf;
    };

    if (e.type & xplmType_FloatArray) {
        int n = (std::min)(e.count, 6);
        std::string s = "[";
        for (int i = 0; i < n; i++) {
            if (i) s += ", ";
            s += fmtF(e.value.fArrayValue[i]);
        }
        if (e.count > 6) s += ", ...(" + std::to_string(e.count) + ")";
        return s + "]";
    }
    if (e.type & xplmType_IntArray) {
        int n = (std::min)(e.count, 6);
        std::string s = "[";
        for (int i = 0; i < n; i++) {
            if (i) s += ", ";
            s += std::to_string(e.value.iArrayValue[i]);
        }
        if (e.count > 6) s += ", ...(" + std::to_string(e.count) + ")";
        return s + "]";
    }
    if (e.type & xplmType_Data) {
        std::string s(e.value.cArrayValue);
        if (s.size() > 40) s = s.substr(0, 40) + "...";
        return "\"" + s + "\"";
    }
    if (e.type & xplmType_Float)  return fmtF(e.value.fValue);
    if (e.type & xplmType_Int)    return std::to_string(e.value.iValue);
    return {};
}

// ---------------------------------------------------------------------------
// snapshot — for status page
// ---------------------------------------------------------------------------
std::vector<DataRefRegistry::SnapEntry> DataRefRegistry::snapshot() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    std::vector<SnapEntry> result;
    result.reserve(m_registry.size());
    for (const auto& [name, entry] : m_registry)
        result.push_back({ name, entry.attempted, entry.found, entry.type, entry.count,
                           formatValueDisplay(entry) });
    return result;
}
