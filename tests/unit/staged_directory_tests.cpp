#include "core/io/file_io.h"
#include "core/io/staged_directory.h"

#include <cassert>
#include <chrono>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int main()
{
    namespace fs = std::filesystem;
    using arp::core::io::StagedDirectory;
    const auto root = fs::temp_directory_path()
        / ("arp-staged-directory-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(root));
    fs::path abandoned;
    {
        StagedDirectory transaction;
        assert(transaction.begin(root / "aborted"));
        abandoned = transaction.path();
        std::ofstream(abandoned / "payload") << "candidate";
        assert(!transaction.begin(root / "another"));
        assert(!fs::exists(root / "aborted"));
    }
    assert(!fs::exists(abandoned) && !fs::exists(root / "aborted"));
    {
        StagedDirectory transaction;
        assert(transaction.begin(root / "published"));
        std::ofstream(transaction.path() / "payload") << "complete";
        assert(transaction.publish());
        assert(!transaction.publish());
        assert(arp::core::readTextFile(root / "published/payload").text == "complete");
        StagedDirectory conflict;
        assert(!conflict.begin(root / "published"));
        assert(conflict.diagnostic() == "ARP-DIRECTORY-DESTINATION-EXISTS");
    }
    {
        StagedDirectory transaction;
        assert(transaction.begin(root / "race"));
        abandoned = transaction.path();
        std::ofstream(abandoned / "payload") << "candidate";
        assert(fs::create_directory(root / "race"));
        std::ofstream(root / "race/payload") << "external";
        assert(!transaction.publish());
        assert(
            transaction.diagnostic().find("ARP-DIRECTORY-EXCLUSIVE-PUBLISH-FAILED native=") == 0u);
        assert(arp::core::readTextFile(root / "race/payload").text == "external");
    }
    assert(!fs::exists(abandoned));
#ifdef _WIN32
    {
        StagedDirectory transaction;
        assert(transaction.begin(root / "locked"));
        std::ofstream(transaction.path() / "payload") << "complete";
        const auto directory = CreateFileW(transaction.path().c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
        assert(directory != INVALID_HANDLE_VALUE);
        assert(!transaction.publish());
        assert(
            transaction.diagnostic() == "ARP-DIRECTORY-EXCLUSIVE-PUBLISH-FAILED native=system:32");
        assert(!fs::exists(root / "locked") && fs::exists(transaction.path() / "payload"));
        assert(CloseHandle(directory));
        // Explicit recovery after releasing a known lock, never an automatic retry.
        assert(transaction.publish() && transaction.diagnostic().empty());
        assert(arp::core::readTextFile(root / "locked/payload").text == "complete");
    }
#endif
    {
        const auto occupied = root / "occupied.arp-staging";
        assert(fs::create_directory(occupied));
        std::ofstream(occupied / "payload") << "external";
        {
            StagedDirectory transaction;
            assert(!transaction.begin(root / "occupied"));
            assert(transaction.diagnostic() == "ARP-DIRECTORY-STAGING-CREATE");
        }
        assert(arp::core::readTextFile(occupied / "payload").text == "external");
        std::ofstream(root / "file-target") << "external file";
        StagedDirectory transaction;
        assert(!transaction.begin(root / "file-target"));
        assert(arp::core::readTextFile(root / "file-target").text == "external file");
    }
    {
        StagedDirectory transaction;
        assert(!transaction.begin(root / "missing/child"));
        assert(!transaction.begin(root / ".."));
    }
    fs::remove_all(root);
}
