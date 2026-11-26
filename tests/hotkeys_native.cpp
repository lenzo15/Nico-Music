// Native settings tests use a fake registry; they never steal desktop shortcuts.
#include "../src/desktop/hotkeys.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool equal(const HotkeyBindings& a, const HotkeyBindings& b) {
    for (size_t i = 0; i < a.size(); ++i) if (a[i].key != b[i].key || a[i].modifiers != b[i].modifiers) return false;
    return true;
}
std::string contents(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
struct Registry {
    std::map<int, HotkeyBinding> values{{4, {0, VK_MEDIA_PLAY_PAUSE}}};
    HotkeyBinding blocked{};
    bool add(int id, const HotkeyBinding& b) {
        if (b.key == blocked.key && b.modifiers == blocked.modifiers) return false;
        for (const auto& [other, current] : values)
            if (other != id && current.key == b.key && current.modifiers == b.modifiers) return false;
        values[id] = b; return true;
    }
    void remove(int id) { values.erase(id); }
};
int main() {
    const auto root = fs::temp_directory_path() / (L"NicoMusic-hotkeys-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code ignored; fs::remove_all(root, ignored); } } cleanup{root};
    try {
        fs::create_directories(root);
        const auto defaults = defaultHotkeys();
        for (const auto& b : defaults) require(validHotkey(b), "default rejected");
        for (auto key : {VK_MEDIA_PLAY_PAUSE, VK_MEDIA_PREV_TRACK, VK_MEDIA_NEXT_TRACK, VK_VOLUME_UP, VK_VOLUME_DOWN, VK_F12})
            require(!validHotkey({MOD_CONTROL, static_cast<UINT>(key)}), "fixed/reserved key accepted");
        require(!validHotkey({MOD_SHIFT, 'K'}) && !validHotkey({MOD_WIN, 'K'}), "unsafe modifiers accepted");
        require(!validHotkey({MOD_ALT, VK_F4}) && !validHotkey({MOD_CONTROL | MOD_ALT, VK_DELETE}), "reserved combination accepted");
        require(!validHotkey({MOD_CONTROL, 'Z'}) && !validHotkey({MOD_CONTROL, 'Y'}), "edit history chord accepted as a global transport key");
        require(validHotkey({0, 0}) && !validHotkey({MOD_CONTROL, 0}), "disabled binding validation failed");
        Registry registry;
        const auto add = [&](int id, const HotkeyBinding& b) { return registry.add(id, b); };
        const auto remove = [&](int id) { registry.remove(id); };
        const auto file = root / "hotkeys.ini";
        const HotkeyBindings custom{{{MOD_CONTROL | MOD_ALT, VK_F8}, {MOD_CONTROL | MOD_SHIFT, 'J'}, {0, 0}}};
        {
            HotkeySettings settings(file, add, remove);
            require(equal(settings.bindings(), defaults) && registry.values.size() == 4, "startup defaults failed");
            settings.capture(true);
            require(settings.capturing() && registry.values.size() == 1 && registry.values.count(4), "capture changed fixed media keys");
            settings.capture(false);
            require(!settings.capturing() && registry.values.size() == 4, "capture cancel did not restore shortcuts");
            require(settings.apply(custom).empty(), "valid custom save failed");
            require(equal(settings.bindings(), custom) && !settings.registered()[2], "custom/disabled binding not applied");
            const auto before = contents(file);
            auto occupied = custom; occupied[0] = {MOD_ALT | MOD_CONTROL, VK_F9}; registry.blocked = occupied[0];
            require(!settings.apply(occupied).empty(), "occupied combination accepted");
            require(equal(settings.bindings(), custom) && contents(file) == before && registry.values[1].key == VK_F8, "conflict did not roll back");
            auto duplicate = custom; duplicate[1] = duplicate[0];
            require(!settings.apply(duplicate).empty() && equal(settings.bindings(), custom), "duplicate accepted");
            auto fixed = custom; fixed[0] = {MOD_CONTROL, VK_VOLUME_UP};
            require(!settings.apply(fixed).empty() && contents(file) == before, "built-in key was editable");
        }
        require(registry.values.size() == 1 && registry.values.count(4), "shutdown did not release only custom bindings");
        {
            HotkeySettings reloaded(file, add, remove);
            require(equal(reloaded.bindings(), custom) && reloaded.warning().empty(), "persistent settings not restored");
            require(reloaded.apply(defaults).empty(), "restore defaults failed");
            require(reloaded.apply(HotkeyBindings{}).empty(), "clear all failed");
        }
        {
            HotkeySettings cleared(file, add, remove);
            require(equal(cleared.bindings(), HotkeyBindings{}) && registry.values.size() == 1, "cleared keys returned on restart");
        }
        std::ofstream(file) << "0 179\n3 39\n3 32\n";
        {
            HotkeySettings corrupt(file, add, remove);
            require(equal(corrupt.bindings(), defaults) && !corrupt.warning().empty(), "corrupt file did not fall back safely");
            require(corrupt.apply(defaults).empty() && corrupt.warning().empty(), "corrupt settings could not be repaired");
        }
        const auto blocker = root / "not-a-directory";
        std::ofstream(blocker) << "keep";
        {
            HotkeySettings unwritable(blocker / "hotkeys.ini", add, remove);
            require(!unwritable.apply(custom).empty(), "write failure accepted");
            require(equal(unwritable.bindings(), defaults) && registry.values[1].key == VK_LEFT, "write failure did not restore registrations");
            require(contents(blocker) == "keep", "write failure changed existing data");
        }
        std::cout << "OK: validation, fixed media exclusion, capture/cancel, custom/disabled bindings, atomic persistence, restart, conflict and disk-failure rollback\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
