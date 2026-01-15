#include <doctest/doctest.h>
#include "pnkr/renderer/ResourceStateMachine.hpp"

using namespace pnkr::renderer;

TEST_CASE("ResourceStateMachine Transitions") {
    ResourceStateMachine fsm;

    SUBCASE("Initial State is Unloaded") {
        CHECK(fsm.getCurrentState() == ResourceState::Unloaded);
    }

    SUBCASE("Valid Happy Path") {
        CHECK(fsm.tryTransition(ResourceState::Pending));
        CHECK(fsm.getCurrentState() == ResourceState::Pending);

        CHECK(fsm.tryTransition(ResourceState::Loading));
        CHECK(fsm.getCurrentState() == ResourceState::Loading);

        CHECK(fsm.tryTransition(ResourceState::Decoded));
        CHECK(fsm.getCurrentState() == ResourceState::Decoded);

        CHECK(fsm.tryTransition(ResourceState::Uploading));
        CHECK(fsm.getCurrentState() == ResourceState::Uploading);

        CHECK(fsm.tryTransition(ResourceState::Transferred));
        CHECK(fsm.getCurrentState() == ResourceState::Transferred);

        CHECK(fsm.tryTransition(ResourceState::Finalizing));
        CHECK(fsm.getCurrentState() == ResourceState::Finalizing);

        CHECK(fsm.tryTransition(ResourceState::Complete));
        CHECK(fsm.getCurrentState() == ResourceState::Complete);
    }

    SUBCASE("Invalid Transitions") {
        // Can't jump from Unloaded to Complete
        CHECK_FALSE(fsm.tryTransition(ResourceState::Complete));
        CHECK(fsm.getCurrentState() == ResourceState::Unloaded); // Should stay

        // Move to Pneidng
        fsm.tryTransition(ResourceState::Pending);
        // Can't go back to Unloaded directly (unless implemented logic changes, currently strict forward)
        CHECK_FALSE(fsm.tryTransition(ResourceState::Unloaded)); 
    }

    SUBCASE("Failure States") {
        // Can fail from anywhere
        CHECK(fsm.tryTransition(ResourceState::Failed));
        CHECK(fsm.getCurrentState() == ResourceState::Failed);

        // Can recover from Failed to Pending
        CHECK(fsm.tryTransition(ResourceState::Pending));
        CHECK(fsm.getCurrentState() == ResourceState::Pending);
    }
    
    SUBCASE("Self Transition") {
        CHECK(fsm.tryTransition(ResourceState::Unloaded));
        CHECK(fsm.getCurrentState() == ResourceState::Unloaded);
    }
}
