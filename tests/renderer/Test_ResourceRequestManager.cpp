#include "doctest/doctest.h"
#include "pnkr/renderer/ResourceRequestManager.hpp"
#include "pnkr/renderer/AsyncLoaderTypes.hpp"

using namespace pnkr::renderer;

TEST_CASE("ResourceRequestManager") {
    ResourceRequestManager manager;

    SUBCASE("File Request Queue") {
        CHECK(manager.getPendingFileCount() == 0);
        CHECK_FALSE(manager.hasPendingFileRequests());

        LoadRequest req1{};
        req1.path = "test1.png";
        manager.addFileRequest(req1);

        CHECK(manager.getPendingFileCount() == 1);
        CHECK(manager.hasPendingFileRequests());

        LoadRequest req2{};
        req2.path = "test2.png";
        manager.addFileRequest(req2);

        CHECK(manager.getPendingFileCount() == 2);

        auto popped1 = manager.popFileRequest();
        CHECK(popped1.path == "test1.png");
        CHECK(manager.getPendingFileCount() == 1);
        
        auto popped2 = manager.popFileRequest();
        CHECK(popped2.path == "test2.png");
        CHECK(manager.getPendingFileCount() == 0);
        CHECK_FALSE(manager.hasPendingFileRequests());
    }

    SUBCASE("Upload Queue Priorities") {
        CHECK(manager.getUploadQueueSize() == 0);
        CHECK(manager.getHighPriorityQueueSize() == 0);

        UploadRequest lowPrio{};
        lowPrio.req.path = "low.png";
        manager.enqueueUpload(std::move(lowPrio), false);

        CHECK(manager.getUploadQueueSize() == 1);
        CHECK(manager.getHighPriorityQueueSize() == 0);

        UploadRequest highPrio{};
        highPrio.req.path = "high.png";
        manager.enqueueUpload(std::move(highPrio), true);

        CHECK(manager.getUploadQueueSize() == 1);
        CHECK(manager.getHighPriorityQueueSize() == 1);

        // Dequeue should prefer high priority
        auto reqOpt1 = manager.dequeueUpload();
        REQUIRE(reqOpt1.has_value());
        CHECK(reqOpt1->req.path == "high.png");
        CHECK(manager.getHighPriorityQueueSize() == 0);
        CHECK(manager.getUploadQueueSize() == 1);

        auto reqOpt2 = manager.dequeueUpload();
        REQUIRE(reqOpt2.has_value());
        CHECK(reqOpt2->req.path == "low.png");
        CHECK(manager.getUploadQueueSize() == 0);
    }
    
    SUBCASE("Finalization Queue") {
        UploadRequest req{};
        req.req.path = "done.png";
        manager.enqueueFinalization(std::move(req));
        
        auto doneOpt = manager.dequeueFinalization();
        REQUIRE(doneOpt.has_value());
        CHECK(doneOpt->req.path == "done.png");
        
        auto emptyOpt = manager.dequeueFinalization();
        CHECK_FALSE(emptyOpt.has_value());
    }
}
