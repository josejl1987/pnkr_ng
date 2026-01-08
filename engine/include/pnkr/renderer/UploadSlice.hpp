#pragma once

#include <cstddef>

namespace pnkr::renderer {

struct UploadSlice {
    size_t offset = 0;
    size_t size = 0;
};

}
