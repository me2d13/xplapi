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
            if (entry.dataRef == nullptr && !entry.found) {
                // entry.found == false AND dataRef == null means not yet attempted
                // We mark found=true optimistically so we don't re-try every tick;
                // tryResolve will set it back to false if lookup fails.
                toResolve.push_back(name);
            }
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
    }
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
// snapshot — for status page
// ---------------------------------------------------------------------------
std::vector<DataRefRegistry::SnapEntry> DataRefRegistry::snapshot() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    std::vector<SnapEntry> result;
    result.reserve(m_registry.size());
    for (const auto& [name, entry] : m_registry)
        result.push_back({ name, entry.found, entry.type, entry.count });
    return result;
}
