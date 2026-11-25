#pragma once
#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <windows.h>

struct HotkeyBinding {
    UINT modifiers = 0, key = 0;
};
using HotkeyBindings = std::array<HotkeyBinding, 3>;
HotkeyBindings defaultHotkeys();
bool validHotkey(const HotkeyBinding& binding);

// Only the three configurable combinations live here. Hardware media/volume
// bindings are registered separately and cannot be changed by these settings.
class HotkeySettings {
public:
    using Register = std::function<bool(int, const HotkeyBinding&)>;
    using Unregister = std::function<void(int)>;
    HotkeySettings(std::filesystem::path file, Register add, Unregister remove);
    ~HotkeySettings();
    const HotkeyBindings& bindings() const { return active; }
    const std::array<bool, 3>& registered() const { return acquired; }
    bool capturing() const { return suspended; }
    const std::string& warning() const { return loadWarning; }
    void capture(bool enabled);
    // Registration or disk failure leaves the previous bindings/file in place.
    std::string apply(const HotkeyBindings& candidate);

private:
    std::filesystem::path file;
    Register add;
    Unregister remove;
    HotkeyBindings active = defaultHotkeys();
    std::array<bool, 3> acquired{};
    bool suspended = false;
    std::string loadWarning;
    void release();
    void acquire(const HotkeyBindings& bindings);
    void load();
    void save(const HotkeyBindings& bindings);
};
