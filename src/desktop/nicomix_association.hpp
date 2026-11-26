#pragma once

#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <string>
#include <vector>

namespace nicomix {
// HKCU only. The shell's protected UserChoice takes precedence and is never written.
inline bool registerAssociation(const std::wstring& exe) {
    const std::wstring classes = L"Software\\Classes\\";
    const std::wstring type = L"NicoMusic.NicoMix";
    bool changed = false;
    auto put = [&](const std::wstring& key, const wchar_t* name, DWORD kind, const std::wstring& value) {
        const auto path = classes + key;
        DWORD bytes = kind == REG_NONE ? 0 : static_cast<DWORD>((value.size()+1)*sizeof(wchar_t));
        std::vector<wchar_t> old(value.size()+1);
        DWORD size = static_cast<DWORD>(old.size()*sizeof(wchar_t)), found{};
        if (RegGetValueW(HKEY_CURRENT_USER, path.c_str(), name, RRF_RT_ANY | RRF_NOEXPAND,
                         &found, old.data(), &size) == ERROR_SUCCESS && kind == found && size == bytes &&
            (!bytes || std::equal(old.begin(), old.end(), value.c_str()))) return true;
        if (RegSetKeyValueW(HKEY_CURRENT_USER, path.c_str(), name, kind, value.c_str(), bytes) != ERROR_SUCCESS)
            return false;
        changed = true;
        return true;
    };
    const std::wstring quoted = L"\"" + exe + L"\"";
    if (!put(type, nullptr, REG_SZ, L"NicoMix 分享歌单") ||
        !put(type + L"\\DefaultIcon", nullptr, REG_SZ, quoted + L",-103") ||
        !put(type + L"\\shell\\open\\command", nullptr, REG_SZ, quoted + L" \"%1\"") ||
        !put(L".nicomix\\OpenWithProgids", type.c_str(), REG_NONE, L"")) return false;

    // Supply a fallback only for an unclaimed extension, including machine-wide handlers.
    wchar_t first[2]{};
    DWORD bytes = sizeof(first);
    auto result = RegGetValueW(HKEY_CLASSES_ROOT, L".nicomix", nullptr, RRF_RT_REG_SZ, nullptr, first, &bytes);
    if (result == ERROR_FILE_NOT_FOUND || (result == ERROR_SUCCESS && !first[0])) {
        if (!put(L".nicomix", nullptr, REG_SZ, type)) return false;
    }
    if (changed) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}
} // namespace nicomix
