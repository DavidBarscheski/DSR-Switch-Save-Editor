// Platform-independent save transaction: verify the live file, make a verified backup that can never
// overwrite an earlier one, write, commit, read back. All file access goes through IFileSystem so the
// failure paths can be tested on a PC with a fake file system (tests/test_core.cpp).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dsr {

enum class CreateResult { Ok, Exists, Error };
enum class WriteResult { Ok, OpenFailed, Error }; // OpenFailed: the file was not touched

class IFileSystem {
public:
    virtual ~IFileSystem() {}
    virtual bool readFile(const std::string& path, std::vector<uint8_t>& out) = 0;
    // Creates a NEW file with this content (write, flush and close each checked).
    // Must return Exists, and change nothing, if the path is already there.
    virtual CreateResult createNew(const std::string& path, const std::vector<uint8_t>& data) = 0;
    // Overwrites an existing file in place without truncating it first (write, flush, close checked).
    virtual WriteResult overwriteInPlace(const std::string& path, const std::vector<uint8_t>& data) = 0;
    virtual bool commit() = 0;
    virtual bool removeFile(const std::string& path) = 0; // only used to clean up our own failed backup
};

enum class SaveOutcome {
    Verified,      // written, committed, read back identical, checksum valid
    StaleLive,     // the file on the console no longer matches what was loaded: nothing written
    BackupFailed,  // no verified backup could be made: nothing written
    OpenFailed,    // the live file could not be opened for writing: nothing written
    WriteFailed,   // the write failed part way: the live file may be damaged
    CommitFailed,  // the commit failed: the live file may or may not have kept the changes
    VerifyFailed,  // the read-back differs from what was written, or the checksum is wrong
};

struct SaveReport {
    SaveOutcome outcome = SaveOutcome::BackupFailed;
    std::string backupPath;           // set once a verified backup exists
    bool liveMayHaveChanged = false;  // true after any outcome in which the file on the console might differ
};

// `original` is the file as loaded, `edited` the finished buffer (finalize()), `base` the edited
// character's region. `stamp` only makes the backup name readable; collisions are handled by a counter.
SaveReport saveWithBackup(IFileSystem& fs, const std::string& livePath, const std::string& backupDir,
                          const std::string& backupPrefix, const std::string& stamp,
                          const std::vector<uint8_t>& original, const std::vector<uint8_t>& edited, int64_t base);

} // namespace dsr
