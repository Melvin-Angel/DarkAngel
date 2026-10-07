#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <windows.h>
struct Fixture {
    std::filesystem::path path;
    sqlite3* db = nullptr;
    sqlite3_stmt* statement = nullptr;
    ~Fixture() {
        if (statement) sqlite3_finalize(statement);
        if (db) sqlite3_close_v2(db);
        std::error_code ec;
        if (!path.empty()) std::filesystem::remove(path, ec);
    }
};
int main() {
    Fixture fixture;
    try {
        wchar_t directory[MAX_PATH + 1]{}, filename[MAX_PATH + 1]{};
        if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"DAE", 0, filename))
            throw std::runtime_error("Cannot create exclusive temporary fixture");
        fixture.path = filename;
        const auto u8 = fixture.path.u8string();
        if (sqlite3_open_v2(reinterpret_cast<const char*>(u8.c_str()), &fixture.db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK)
            throw std::runtime_error("Cannot open SQLite fixture");
        sqlite3_busy_timeout(fixture.db, 1000);
        const char* sql = "BEGIN IMMEDIATE; CREATE TABLE probe(value INTEGER NOT NULL); INSERT INTO probe VALUES(42); COMMIT;";
        if (sqlite3_exec(fixture.db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(fixture.db));
        if (sqlite3_prepare_v2(fixture.db, "SELECT value FROM probe", -1, &fixture.statement, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(fixture.db));
        if (sqlite3_step(fixture.statement) != SQLITE_ROW || sqlite3_column_int(fixture.statement, 0) != 42 || sqlite3_step(fixture.statement) != SQLITE_DONE)
            throw std::runtime_error("SQLite committed transaction readback differs");
        const int finalize_result = sqlite3_finalize(fixture.statement);
        fixture.statement = nullptr;
        if (finalize_result != SQLITE_OK) throw std::runtime_error("Finalize failed");
        if (sqlite3_close(fixture.db) != SQLITE_OK) throw std::runtime_error("Close failed");
        fixture.db = nullptr;
        if (!std::filesystem::remove(fixture.path)) throw std::runtime_error("Fixture cleanup failed");
        fixture.path.clear();
        std::cout << "SQLite temporary transaction passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
