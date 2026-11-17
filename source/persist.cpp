#include "persist.h"

#include "save.h"

namespace dsr {

SaveReport saveWithBackup(IFileSystem& fs, const std::string& livePath, const std::string& backupDir,
                          const std::string& backupPrefix, const std::string& stamp,
                          const std::vector<uint8_t>& original, const std::vector<uint8_t>& edited, int64_t base) {
    SaveReport r;

    // 1. The file on the console must still be the one that was loaded, or the backup and the edit
    //    would both be based on stale data.
    std::vector<uint8_t> live;
    if (!fs.readFile(livePath, live) || live != original) {
        r.outcome = SaveOutcome::StaleLive;
        return r;
    }

    // 2. Make a backup under a name that is not in use yet. Never replace an existing file.
    std::string path;
    bool made = false;
    for (int n = 0; n < 1000 && !made; n++) {
        path = backupDir + "/" + backupPrefix + "." + stamp + (n ? "_" + std::to_string(n) : std::string()) + ".bak";
        CreateResult c = fs.createNew(path, original);
        if (c == CreateResult::Exists) continue;
        if (c == CreateResult::Error) {
            fs.removeFile(path); // our own half-written file
            r.outcome = SaveOutcome::BackupFailed;
            return r;
        }
        made = true;
    }
    if (!made) {
        r.outcome = SaveOutcome::BackupFailed;
        return r;
    }

    // 3. The backup only counts once it reads back identical to the original.
    std::vector<uint8_t> check;
    if (!fs.readFile(path, check) || check != original) {
        fs.removeFile(path);
        r.outcome = SaveOutcome::BackupFailed;
        return r;
    }
    r.backupPath = path;

    // 4. From here on the live file may differ from what was loaded.
    WriteResult w = fs.overwriteInPlace(livePath, edited);
    if (w == WriteResult::OpenFailed) {
        r.outcome = SaveOutcome::OpenFailed;
        return r;
    }
    r.liveMayHaveChanged = true;
    if (w != WriteResult::Ok) {
        r.outcome = SaveOutcome::WriteFailed;
        return r;
    }
    if (!fs.commit()) {
        r.outcome = SaveOutcome::CommitFailed;
        return r;
    }

    // 5. Read back; check the edited character's own region (not the file's default one).
    std::vector<uint8_t> after;
    if (!fs.readFile(livePath, after) || after != edited || !Save::checksumValid(after, base)) {
        r.outcome = SaveOutcome::VerifyFailed;
        return r;
    }
    r.outcome = SaveOutcome::Verified;
    r.liveMayHaveChanged = false; // the new file is verified and is the new baseline
    return r;
}

} // namespace dsr
