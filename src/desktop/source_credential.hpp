#pragma once
#include <windows.h>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace sourceCredential {
constexpr size_t maxBytes = 16384;
enum class Action { status, save, clear };
enum class Result { saved = 20, empty, invalid, failed, unavailable };

// The credential goes through a private stdin pipe, never through argv, mpv, files or logs.
inline Result run(const std::filesystem::path& helper, Action action, std::string input = {}) {
    struct Wipe {
        std::string& value;
        ~Wipe() { SecureZeroMemory(value.data(), value.size()); }
    } wipe{input};
    if (input.size() > maxBytes || input.find_first_of("\r\n\0", 0, 3) != std::string::npos)
        return Result::invalid;
    struct Close { void operator()(HANDLE handle) const { CloseHandle(handle); } };
    using Handle = std::unique_ptr<void, Close>;
    SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
    HANDLE readEnd{}, writeEnd{};
    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, static_cast<DWORD>(maxBytes * 2))) return Result::failed;
    Handle reader(readEnd), writer(writeEnd);
    if (!SetHandleInformation(writer.get(), HANDLE_FLAG_INHERIT, 0)) return Result::failed;
    const auto sink = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (sink == INVALID_HANDLE_VALUE) return Result::failed;
    Handle output(sink);

    SIZE_T bytes{};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    if (!bytes) return Result::failed;
    std::vector<unsigned char> buffer(bytes);
    const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) return Result::failed;
    struct DeleteAttributes { void operator()(LPPROC_THREAD_ATTRIBUTE_LIST list) const { DeleteProcThreadAttributeList(list); } };
    const std::unique_ptr<std::remove_pointer_t<LPPROC_THREAD_ATTRIBUTE_LIST>, DeleteAttributes> attributeGuard(attributes);
    HANDLE inherited[] = {reader.get(), output.get()};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                   sizeof(inherited), nullptr, nullptr)) return Result::failed;

    std::wstring command = action == Action::status ? L"source-helper credential-status" :
                           action == Action::save ? L"source-helper credential-save" : L"source-helper credential-clear";
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = reader.get();
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = output.get();
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                        nullptr, nullptr, &startup.StartupInfo, &process)) return Result::unavailable;
    Handle child(process.hProcess);
    CloseHandle(process.hThread);
    reader.reset();
    DWORD written{};
    const bool sent = input.empty() || (WriteFile(writer.get(), input.data(), static_cast<DWORD>(input.size()), &written, nullptr) &&
                                        written == input.size());
    SecureZeroMemory(input.data(), input.size());
    writer.reset(); // Signal EOF even when the payload contains no newline.
    if (WaitForSingleObject(child.get(), 5000) != WAIT_OBJECT_0) {
        TerminateProcess(child.get(), 1);
        return Result::failed;
    }
    DWORD code{};
    if (!GetExitCodeProcess(child.get(), &code)) return Result::failed;
    switch (code) {
    case 20: return sent ? Result::saved : Result::failed;
    case 21: return Result::empty;
    case 22: return Result::invalid;
    case 23: return Result::failed;
    default: return Result::unavailable; // Older plugin helpers do not recognize the private protocol.
    }
}
}
