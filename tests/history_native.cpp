#include "../src/library/library_history.hpp"
#include <iostream>

void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class F> void fails(F action) {
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    require(failed, "expected failure did not occur");
}
int main() {
    const auto root = fs::temp_directory_path() / (L"NicoMusic-history-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    try {
        SetEnvironmentVariableW(L"NICOMUSIC_HOME", root.c_str());
        Station station;
        LibraryHistory history;
        station.add("https://example.test/a.mp3 A");
        station.add("https://example.test/b.mp3 B");
        const auto initial = station.entries();
        const std::vector<Track> imported{{"https://example.test/b.mp3", "duplicate B", {}}, {"https://example.test/c.mp3", "C", {}}};
        auto order = station.commitCollection(imported);
        require(order == std::vector<int>({1, 2}) && station.count() == 3, "import deduplication failed");
        const auto importId = history.record(initial, station.entries());
        require(!importId.empty(), "import was not recorded");
        auto metadata = station.entries(); metadata[2].name = "Resolved C"; metadata[2].thumbnail = "https://example.test/c.jpg";
        station.replaceLibrary(metadata);
        history.apply(station, importId, true);
        require(station.count() == 2 && station.entries()[1].name == "B", "undo removed pre-existing duplicate");
        fails([&] { history.apply(station, importId, true); });
        history.apply(station, importId, false);
        require(station.count() == 3 && station.entries()[2].name == "Resolved C" && station.entries()[2].thumbnail == "https://example.test/c.jpg", "redo lost metadata/order");
        const auto beforeDelete = station.entries();
        station.eraseUrls({beforeDelete[0].url, beforeDelete[2].url});
        const auto deleteId = history.record(beforeDelete, station.entries());
        require(station.count() == 1, "batch delete failed");
        history.apply(station, deleteId, true);
        require(station.count() == 3 && station.entries()[0].name == "A" && station.entries()[2].name == "Resolved C", "batch undo failed");
        history.apply(station, deleteId, false);
        require(station.count() == 1 && Station().count() == 1, "redo was not persisted");
        // A blocked replace must leave both memory and the history cursor intact.
        const auto file = libraryFile(), backup = root / "backup.tsv";
        fs::rename(file, backup); fs::create_directory(file);
        fails([&] { history.apply(station, deleteId, true); });
        require(station.count() == 1, "failed save changed memory");
        fs::remove(file); fs::rename(backup, file);
        history.apply(station, deleteId, true);
        require(station.count() == 3, "failed save consumed undo");
        require(history.record(station.entries(), station.entries()).empty(), "no-op generated history");
        history.clear();
        fails([&] { history.apply(station, deleteId, false); });
        std::cout << "OK: native import/delete/undo/redo, duplicate safety, metadata/order, durable writes, disk-failure rollback and session reset\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
