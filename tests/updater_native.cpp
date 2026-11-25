// Optional harness: configure with -DNICOMUSIC_UPDATE_TESTS=ON.
#include "../src/updater.hpp"
#include "../src/update_install.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

std::string ascii(const wchar_t* value) {
    const std::wstring text(value);
    std::string result;
    for (wchar_t c : text) {
        if (c > 127 || c < 32) throw std::runtime_error("ASCII argument required");
        result.push_back(static_cast<char>(c));
    }
    return result;
}
int wmain(int argc, wchar_t** argv) {
    try {
        UpdateDownload sample{"https://github.com/lenzo15/Nico-Music/releases/download/v1.0.0/NicoMusic-windows-x64.zip",
                              "NicoMusic-windows-x64.zip", std::string(64, 'a'), 100};
        if (!Updater::validAsset(sample)) throw std::runtime_error("valid asset rejected");
        for (const auto& url : {"http://github.com/lenzo15/Nico-Music/releases/download/v1/a.zip",
                                "https://example.com/a.zip", "https://github.com/another/repo/releases/download/v1/a.zip",
                                "https://github.com/lenzo15/Nico-Music/releases/download/../../other/NicoMusic-windows-x64.zip",
                                "https://github.com/lenzo15/Nico-Music/releases/download/%2e%2e/NicoMusic-windows-x64.zip"}) {
            auto bad = sample; bad.url = url;
            if (Updater::validAsset(bad)) throw std::runtime_error("unsafe URL accepted");
        }
        for (const auto& name : {"../a.zip", "a.exe", "a\\b.zip", "..zip", "a.zip\n"}) {
            auto bad = sample; bad.name = name;
            if (Updater::validAsset(bad)) throw std::runtime_error("unsafe filename accepted");
        }
        if (!Updater::installable(sample, "0.9.99") || Updater::installable(sample, "1.0.0") || Updater::installable(sample, "1.0.1"))
            throw std::runtime_error("install version gate");
        auto unsignedZip = sample; unsignedZip.sha256.clear();
        auto source = sample; source.name = "source.zip"; source.url.replace(source.url.rfind('/')+1, std::string::npos, source.name);
        if (Updater::installable(unsignedZip, "0.1.0") || Updater::installable(source, "0.1.0")) throw std::runtime_error("install asset gate");
        auto bad = sample; bad.size = 0;
        if (Updater::validAsset(bad)) throw std::runtime_error("zero size accepted");
        bad = sample; bad.sha256 = "invalid";
        if (Updater::validAsset(bad)) throw std::runtime_error("invalid digest accepted");
        if (argc == 1) { std::cout << "OK: native asset validation\n"; return 0; }
        // Test-only handoff: the fixture is private and contains no installed player.
        if (std::wstring(argv[1]) == L"--fixture-install" && argc == 4) {
            std::filesystem::path root(argv[2]);
            if (!std::filesystem::is_regular_file(root / L"fixture-only")) throw std::runtime_error("missing fixture marker");
            UpdateInstall job(root, ascii(argv[3]));
            std::filesystem::copy_file(root / L"fixture.zip", job.package());
            job.start();
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(40);
            while (!job.readyToExit()) {
                if (std::chrono::steady_clock::now() > until) throw std::runtime_error("helper timeout");
                Sleep(25);
            }
            return 0; // unlock the fixture and let the helper apply the package
        }
        Updater updater;
        const bool check = std::wstring(argv[1]) == L"--check";
        const bool cancelProgress = argc > 7 && std::wstring(argv[7]) == L"--cancel-progress";
        const bool cancel = cancelProgress || (argc > 7 && std::wstring(argv[7]) == L"--cancel");
        if (check) updater.check();
        else {
            if (argc < 7) throw std::runtime_error("--download URL NAME SIZE SHA256 DESTINATION [--cancel]");
            UpdateDownload asset{ascii(argv[2]), ascii(argv[3]), ascii(argv[5]), std::stoull(argv[4])};
            if (asset.sha256 == "-") asset.sha256.clear();
            updater.download(asset, argv[6]);
            if (cancel && !cancelProgress) updater.cancel();
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
        while (updater.busy()) {
            if (cancelProgress && updater.status(true)->received > 0) updater.cancel();
            if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("updater timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto state = updater.status(true);
        if (check) {
            if (state->phase != "checked" || state->httpStatus != 200) throw std::runtime_error(state->error + " HTTP " + std::to_string(state->httpStatus));
            std::cout << state->body;
            return 0;
        }
        std::cout << state->phase << " " << state->received << " " << state->error << "\n";
        return state->phase == (cancel ? "cancelled" : "downloaded") ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 2; }
}
