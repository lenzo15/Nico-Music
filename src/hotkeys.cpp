#include "hotkeys.hpp"
#include <fstream>
#include <stdexcept>
#include <utility>

HotkeyBindings defaultHotkeys() {
    return {{{MOD_CONTROL | MOD_ALT, VK_LEFT}, {MOD_CONTROL | MOD_ALT, VK_RIGHT}, {MOD_CONTROL | MOD_ALT, VK_SPACE}}};
}
bool validHotkey(const HotkeyBinding& b) {
    if (!b.key) return !b.modifiers; // disabled
    if ((b.modifiers & ~(MOD_CONTROL | MOD_ALT | MOD_SHIFT)) || !(b.modifiers & (MOD_CONTROL | MOD_ALT))) return false;
    const bool key = (b.key >= 'A' && b.key <= 'Z') || (b.key >= '0' && b.key <= '9') ||
        (b.key >= VK_F1 && b.key <= VK_F11) || (b.key >= VK_PRIOR && b.key <= VK_DOWN) ||
        b.key == VK_SPACE || b.key == VK_INSERT || b.key == VK_DELETE;
    if (!key) return false;
    if ((b.key == VK_F4 && (b.modifiers & MOD_ALT)) ||
        (b.key == VK_DELETE && (b.modifiers & MOD_CONTROL) && (b.modifiers & MOD_ALT))) return false;
    return true;
}
namespace {
std::string validate(const HotkeyBindings& bindings) {
    for (size_t i = 0; i < bindings.size(); ++i) {
        if (!validHotkey(bindings[i])) return "组合键无效：请使用 Ctrl 或 Alt，搭配字母、数字、F1–F11、空格或导航键";
        for (size_t j = 0; j < i; ++j)
            if (bindings[i].key && bindings[i].key == bindings[j].key && bindings[i].modifiers == bindings[j].modifiers)
                return "不同播放操作不能使用相同的组合键";
    }
    return {};
}
}
HotkeySettings::HotkeySettings(std::filesystem::path path, Register onAdd, Unregister onRemove)
    : file(std::move(path)), add(std::move(onAdd)), remove(std::move(onRemove)) {
    load();
    acquire(active);
}
HotkeySettings::~HotkeySettings() { release(); }
void HotkeySettings::release() {
    for (size_t i = 0; i < acquired.size(); ++i) {
        if (acquired[i]) remove(static_cast<int>(i + 1));
        acquired[i] = false;
    }
}
void HotkeySettings::acquire(const HotkeyBindings& bindings) {
    for (size_t i = 0; i < bindings.size(); ++i)
        acquired[i] = bindings[i].key && add(static_cast<int>(i + 1), bindings[i]);
}
void HotkeySettings::capture(bool enabled) {
    if (suspended == enabled) return;
    suspended = enabled;
    if (enabled) release();
    else acquire(active);
}
void HotkeySettings::load() {
    try {
        if (!std::filesystem::exists(file)) return;
        if (std::filesystem::file_size(file) > 4096) throw std::runtime_error("invalid settings");
        std::ifstream input(file, std::ios::binary);
        HotkeyBindings loaded{};
        for (auto& b : loaded) if (!(input >> b.modifiers >> b.key)) throw std::runtime_error("invalid settings");
        std::string extra;
        if (input >> extra || !validate(loaded).empty()) throw std::runtime_error("invalid settings");
        active = loaded;
    } catch (...) {
        loadWarning = "无法读取热键设置，已使用默认组合键；可在设置中重新保存";
    }
}
void HotkeySettings::save(const HotkeyBindings& bindings) {
    std::filesystem::create_directories(file.parent_path());
    auto temporary = file;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L".tmp";
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        for (const auto& b : bindings) output << b.modifiers << ' ' << b.key << '\n';
        output.close();
        if (!output || !MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("无法保存热键设置，请检查数据目录权限");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
std::string HotkeySettings::apply(const HotkeyBindings& candidate) {
    if (const auto error = validate(candidate); !error.empty()) return error;
    release();
    suspended = false;
    acquire(candidate);
    std::string error;
    constexpr const char* names[] = {"上一首", "下一首", "播放 / 暂停"};
    for (size_t i = 0; i < candidate.size(); ++i) {
        if (candidate[i].key && !acquired[i]) { error = std::string(names[i]) + "组合键被系统或其他程序占用，请更换；原设置未修改"; break; }
    }
    if (error.empty()) {
        try { save(candidate); }
        catch (const std::exception&) { error = "无法保存热键设置，请检查数据目录权限；原设置未修改"; }
    }
    if (error.empty()) { active = candidate; loadWarning.clear(); }
    else { release(); acquire(active); }
    return error;
}
