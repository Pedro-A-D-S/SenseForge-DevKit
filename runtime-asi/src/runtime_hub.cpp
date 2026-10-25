// runtime_hub.cpp — SF-14 worker <-> ReShade panel bridge.

#include "runtime_hub.hpp"

namespace senseforge {

RuntimeHub& RuntimeHub::Instance() {
    // Function-local static: the module is pinned for the whole process
    // (DllMain), so the hub outlives every ReShade load/unload cycle.
    static RuntimeHub hub;
    return hub;
}

void RuntimeHub::Publish(const RuntimeStatus& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = status;
}

bool RuntimeHub::ConsumeTestEffect() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool pending = test_effect_;
    test_effect_ = false;
    return pending;
}

std::optional<std::string> RuntimeHub::ConsumeTestFlash() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<std::string> pending = std::move(test_flash_);
    test_flash_.reset();
    return pending;
}

bool RuntimeHub::ConsumeReloadManifest() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool pending = reload_manifest_;
    reload_manifest_ = false;
    return pending;
}

RuntimeStatus RuntimeHub::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void RuntimeHub::RequestTestEffect() {
    std::lock_guard<std::mutex> lock(mutex_);
    test_effect_ = true;
}

void RuntimeHub::RequestTestFlash(const std::string& event_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    test_flash_ = event_id;
}

void RuntimeHub::RequestReloadManifest() {
    std::lock_guard<std::mutex> lock(mutex_);
    reload_manifest_ = true;
}

}  // namespace senseforge
