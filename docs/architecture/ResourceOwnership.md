# Resource Ownership Guidelines

## Overview

PNKR uses a hybrid ownership model to manage GPU resources efficiently. This document outlines the best practices for using strong ownership handles (`TexturePtr`, `BufferPtr`) versus weak reference handles (`TextureHandle`, `BufferHandle`).

---

## Handle Types

### Strong Ownership (`SmartHandle<T, Tag>`)
- **Types**: `TexturePtr`, `BufferPtr`, `MeshPtr`, `PipelinePtr`.
- **Behavior**: Uses intrusive reference counting. The resource is destroyed (deferred by frame lag) when the last `SmartHandle` is destroyed.
- **Usage**: Use for long-lived resources where you want to ensure the resource stays alive as long as your object exists.

### Weak Reference (`core::Handle<Tag>`)
- **Types**: `TextureHandle`, `BufferHandle`, `MeshHandle`, `PipelineHandle`.
- **Behavior**: A lightweight 32-bit (20-bit index, 12-bit generation) identifier. Does NOT affect reference counts.
- **Usage**: Use for transient usage, frame-scoped references, or shader bindings where the lifetime is managed externally.

---

## Ownership Rules

### When to Use Strong Ownership
- **Asset storage**: `AssetManager` caches store `TexturePtr` to keep loaded assets alive.
- **Model geometry**: `ModelDOD` owns its vertex and index buffers.
- **Internal systems**: `IndirectRenderer` owns its persistent render targets (MSAA color, depth).
- **Allocators**: `LinearBufferAllocator` owns the backing GPU pages.

### When to Use Weak References
- **Material definitions**: Materials store `TextureHandle` (bindless indices) to refer to textures owned by the `AssetManager`.
- **Render pass parameters**: Pass descriptors use handles for inputs and outputs.
- **Descriptor sets**: GPU bindings use handles/indices, not strong pointers.
- **Transient scene queries**: Finding a texture by path via `AssetManager::getTexture()`.

---

## API Patterns

### Loading vs. Querying
```cpp
// 1. Loading (Ownership transfer)
TexturePtr myStrongTexture = assetMgr.loadTexture("brick.ktx2");

// 2. Querying (Weak reference)
TextureHandle h = assetMgr.getTexture("brick.ktx2");
if (h.isValid()) {
    // Use handle for binding...
}
```

### Destruction
- Resources are **implicitly destroyed** when the last `SmartHandle` dies.
- You can **explicitly unload** an asset by removing it from the owner's storage (e.g., `AssetManager::unloadTexture()`).
- Always check `isValid()` or use `RHIResourceManager::validate(handle)` when working with weak handles that might have been destroyed.
