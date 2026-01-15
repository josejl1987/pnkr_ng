#include <doctest/doctest.h>
#include "pnkr/assets/TextureLoader.hpp"
#include "pnkr/rhi/rhi_types.hpp"

// Mock or ensure these paths exist for the test
// Mock or ensure these paths exist for the test
// Real asset loading requires actual files.


TEST_CASE("TextureLoader Basic Tests") {
    
    SUBCASE("Load non-existent file returns nullptr") {
        auto asset = pnkr::assets::loadTextureFromDisk("non_existent_file.png");
        CHECK(asset == nullptr);
    }

    // TODO: We need a reliable test asset.
    // Ideally we should generate a small PNG or KTX in the test or commit one.
    // For now, we verify it fails gracefully on invalid files.

}
