#pragma once

#include "pnkr/renderer/RHIResourceManager.hpp"

namespace pnkr::renderer::passes::utils
{

    template<typename T>
    inline bool isHandleValid(const T& handle)
    {
        if constexpr (std::is_same_v<T, TextureHandle>) {
            return handle != INVALID_TEXTURE_HANDLE;
        } else if constexpr (std::is_same_v<T, BufferHandle>) {
            return handle != INVALID_BUFFER_HANDLE;
        } else if constexpr (std::is_same_v<T, TexturePtr>) {
            return handle.isValid();
        } else if constexpr (std::is_same_v<T, BufferPtr>) {
            return handle.isValid();
        } else {
            static_assert(sizeof(T) == 0, "Unsupported handle type");
        }
    }

    template<typename T>
    inline auto toRawHandle(const T& handle)
    {
        if constexpr (std::is_same_v<T, TextureHandle> || std::is_same_v<T, BufferHandle>) {
            return handle;
        } else if constexpr (std::is_same_v<T, TexturePtr> || std::is_same_v<T, BufferPtr>) {
            return handle.handle();
        } else {
            static_assert(sizeof(T) == 0, "Unsupported handle type");
        }
    }

    template<typename HandleT, typename NewHandleT>
    inline void assignHandle(HandleT& target, NewHandleT&& newHandle)
    {
        if constexpr (std::is_same_v<HandleT, TextureHandle> || std::is_same_v<HandleT, BufferHandle>) {
            if constexpr (std::is_same_v<HandleT, TextureHandle>) {
                if (target != INVALID_TEXTURE_HANDLE) { }
            } else {
                if (target != INVALID_BUFFER_HANDLE) { }
            }
            target = newHandle.release();
        } else {
            target = std::forward<NewHandleT>(newHandle);
        }
    }

    template<typename HandleT>
    inline void destroyHandleIfNeeded(RHIRenderer* renderer, HandleT& handle)
    {
        if constexpr (std::is_same_v<HandleT, TextureHandle>) {
            if (handle != INVALID_TEXTURE_HANDLE) {
                renderer->resourceManager()->destroyDeferred(handle);
                handle = INVALID_TEXTURE_HANDLE;
            }
        } else if constexpr (std::is_same_v<HandleT, BufferHandle>) {
            if (handle != INVALID_BUFFER_HANDLE) {
                renderer->resourceManager()->destroyDeferred(handle);
                handle = INVALID_BUFFER_HANDLE;
            }
        } else if constexpr (std::is_same_v<HandleT, TexturePtr>) {
            if (handle.isValid()) {
                auto raw = handle.release();
                if (raw != INVALID_TEXTURE_HANDLE) {
                    renderer->resourceManager()->destroyDeferred(raw);
                }
            }
        } else if constexpr (std::is_same_v<HandleT, BufferPtr>) {
             if (handle.isValid()) {
                auto raw = handle.release();
                if (raw != INVALID_BUFFER_HANDLE) {
                    renderer->resourceManager()->destroyDeferred(raw);
                }
            }
        }
    }

}
