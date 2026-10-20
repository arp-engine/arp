#pragma once

#include "core/api.h"
#include "core/io/file_request.h"
#include "core/io/file_result.h"

namespace arp::core::io {

class ARP_API BlockingFileReader final {
public:
    BlockingFileReader() = default;
    BlockingFileReader(const BlockingFileReader&) = delete;
    BlockingFileReader& operator=(const BlockingFileReader&) = delete;

    FileReadResult read(const FileReadRequest& request) const;
};

} // namespace arp::core::io
