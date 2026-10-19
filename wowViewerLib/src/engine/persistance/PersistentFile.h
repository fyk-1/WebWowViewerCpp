#ifndef AWEBWOWVIEWERCPP_PERSISTENTFILE_H
#define AWEBWOWVIEWERCPP_PERSISTENTFILE_H

#include <string>
#include <memory>
#include "../../include/sharedFile.h"
#include "../../include/fileRank.h"
enum class FileStatus {
    FSNotLoaded,
    FSLoaded,
    FSRejected
};

class PersistentFile {
public:
    PersistentFile(const std::string &fileName) {m_fileName = fileName;};
    PersistentFile(int fileDataId) {m_fileDataId = fileDataId;};

    virtual void process(HFileContent fileContent) = 0;
    FileStatus getStatus() {
        return fsStatus;
    };
    void setRejected() {
        fsStatus = FileStatus::FSRejected;
    };

    int getFileDataId() {return m_fileDataId;};
    const std::string &getFileName() {return m_fileName;};

    std::string getFileNameOrDataId() {
        return m_fileDataId == 0 ? m_fileName : "Fdid "+ std::to_string(m_fileDataId);
    }

    // The file's place in the loader queue (fileRank.h), shared with its request so the loader reads it without the file.
    const std::shared_ptr<FileRank> &rank() const {return m_rank;}
protected:
    FileStatus fsStatus = FileStatus::FSNotLoaded;
private:
    std::string m_fileName = "";
    int m_fileDataId = 0;
    std::shared_ptr<FileRank> m_rank = std::make_shared<FileRank>();
};

#endif //AWEBWOWVIEWERCPP_PERSISTENTFILE_H
