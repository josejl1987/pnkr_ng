#pragma once

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pnkr::renderer {

enum class ResourceState {
    Unloaded,
    Pending,
    Loading,
    Decoded,
    Uploading,
    Transferred,
    Finalizing,
    Complete,
    Failed
};

class ResourceStateMachine {
public:
    ResourceStateMachine() = default;

    ResourceState getCurrentState() const { return m_currentState; }

    bool tryTransition(ResourceState newState) {
        // Fast path: self-transition (noop)
        if (m_currentState == newState) {
            return true;
        }

        // Always allow transition to Failed from any state
        if (newState == ResourceState::Failed) {
            m_currentState = newState;
            return true;
        }

        // Define valid transitions
        // We use a switch for performant state checking
        bool valid = false;
        switch (m_currentState) {
        case ResourceState::Unloaded:
            valid = (newState == ResourceState::Pending);
            break;
        case ResourceState::Pending:
            valid = (newState == ResourceState::Loading);
            break;
        case ResourceState::Loading:
            valid = (newState == ResourceState::Decoded);
            break;
        case ResourceState::Decoded:
            valid = (newState == ResourceState::Uploading);
            break;
        case ResourceState::Uploading:
            // Can transition to Transferred (transfer complete)
            valid = (newState == ResourceState::Transferred);
            break;
        case ResourceState::Transferred:
            // Can transition to Finalizing (graphics work/barriers)
            valid = (newState == ResourceState::Finalizing);
            break;
        case ResourceState::Finalizing:
            valid = (newState == ResourceState::Complete);
            break;
        case ResourceState::Complete:
            // Complete allows transition back to Unloaded (if we implement unloading)
            // or maybe nothing else for now.
            valid = (newState == ResourceState::Unloaded);
            break;
        case ResourceState::Failed:
            // From Failed, we might retry -> Pending, or Unload
            valid = (newState == ResourceState::Pending || newState == ResourceState::Unloaded);
            break;
        }

        if (valid) {
            m_currentState = newState;
            return true;
        }

        return false;
    }

    // Helper to get string representation for debugging
    static constexpr std::string_view stateToString(ResourceState state) {
        switch (state) {
        case ResourceState::Unloaded:   return "Unloaded";
        case ResourceState::Pending:    return "Pending";
        case ResourceState::Loading:    return "Loading";
        case ResourceState::Decoded:    return "Decoded";
        case ResourceState::Uploading:  return "Uploading";
        case ResourceState::Transferred:return "Transferred";
        case ResourceState::Finalizing: return "Finalizing";
        case ResourceState::Complete:   return "Complete";
        case ResourceState::Failed:     return "Failed";
        default:                        return "Unknown";
        }
    }

private:
    ResourceState m_currentState = ResourceState::Unloaded;
};

} // namespace pnkr::renderer
