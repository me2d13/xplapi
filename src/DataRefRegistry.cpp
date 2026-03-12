#include "DataRefRegistry.h"
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
#include "json.hpp"
#include <string>
#include <algorithm>

using json = nlohmann::json;

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
// writeXplValue — writes value to X-Plane; MUST be on XP main thread
// ---------------------------------------------------------------------------
void DataRefRegistry::writeXplValue(DataRefEntry& entry, const json& val)
{
    if (!entry.found || entry.dataRef == nullptr) return;

    if (entry.type & xplmType_Float) {
        if (val.is_number()) XPLMSetDataf(entry.dataRef, val.get<float>());
    } else if (entry.type & xplmType_Int) {
        if (val.is_number()) XPLMSetDatai(entry.dataRef, val.get<int>());
    } else if (entry.type & xplmType_FloatArray) {
        if (val.is_array()) {
            std::vector<float> fv = val.get<std::vector<float>>();
            int count = (int)fv.size();
            XPLMSetDatavf(entry.dataRef, fv.data(), 0, count);
        }
    } else if (entry.type & xplmType_IntArray) {
        if (val.is_array()) {
            std::vector<int> iv = val.get<std::vector<int>>();
            int count = (int)iv.size();
            XPLMSetDatavi(entry.dataRef, iv.data(), 0, count);
        }
    } else if (entry.type & xplmType_Data) {
        if (val.is_string()) {
            std::string s = val.get<std::string>();
            XPLMSetDatab(entry.dataRef, (void*)s.c_str(), 0, (int)s.size());
        }
    }
}

// ---------------------------------------------------------------------------
// executeCommand — MUST be on XP main thread
// ---------------------------------------------------------------------------
void DataRefRegistry::executeCommand(const std::string& name, DataRefRegistry::CommandAction action)
{
    // Try to get cached handle; if not, find it.
    XPLMCommandRef cmd = nullptr;
    auto it = m_commands.find(name);
    if (it != m_commands.end()) {
        cmd = it->second;
    } else {
        cmd = XPLMFindCommand(name.c_str());
        if (cmd) {
            m_commands[name] = cmd;
        } else {
            XPLMDebugString(("xplapi: command not found: " + name + "\n").c_str());
            return;
        }
    }

    if (cmd) {
        switch (action) {
            case CommandAction::Once:  XPLMCommandOnce(cmd);  break;
            case CommandAction::Begin: XPLMCommandBegin(cmd); break;
            case CommandAction::End:   XPLMCommandEnd(cmd);   break;
        }
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

    // Resolve outside the lock ... (unchanged logic)
    std::vector<std::pair<std::string, DataRefEntry>> resolved;
    for (const auto& name : toResolve) {
        DataRefEntry e{};
        tryResolve(name, e);
        resolved.emplace_back(name, e);
    }

    // Process writes + refresh all values
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        
        // 1. Resolve new entries
        for (auto& [name, e] : resolved)
            m_registry[name] = e;

        // 2. Process pending writes
        for (const auto& w : m_writeQueue) {
            auto it = m_registry.find(w.name);
            if (it == m_registry.end()) {
                // Not tracked? Resolve first.
                DataRefEntry e{};
                if (tryResolve(w.name, e)) {
                    m_registry[w.name] = e;
                    writeXplValue(e, w.value);
                }
            } else {
                writeXplValue(it->second, w.value);
            }
        }
        m_writeQueue.clear();

        // 3. Process pending commands
        for (const auto& cmd : m_commandQueue) {
            executeCommand(cmd.name, cmd.action);
        }
        m_commandQueue.clear();

        // 4. Refresh all values
        for (auto& [name, entry] : m_registry)
            readXplValue(entry);
        
        m_updateCounter++;
    }

    // Wake up any HTTP threads waiting for this update
    m_cv.notify_all();
}

// ---------------------------------------------------------------------------
// queueWrite — adds a write request to the queue (called from HTTP thread)
// ---------------------------------------------------------------------------
void DataRefRegistry::queueWrite(const std::string& name, const json& value)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_writeQueue.push_back({ name, value });
}

// ---------------------------------------------------------------------------
// queueCommand — adds a command request to the queue (called from HTTP thread)
// ---------------------------------------------------------------------------
void DataRefRegistry::queueCommand(const std::string& name, DataRefRegistry::CommandAction action)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_commandQueue.push_back({ name, action });
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

// ---------------------------------------------------------------------------
// snapshotCommands — for status page
// ---------------------------------------------------------------------------
std::vector<std::string> DataRefRegistry::snapshotCommands() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    std::vector<std::string> result;
    result.reserve(m_commands.size());
    for (const auto& [name, handle] : m_commands)
        result.push_back(name);
    return result;
}
