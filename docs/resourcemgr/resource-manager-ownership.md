# Resource manager ownership

Anoptic is C and uses arena allocators. Resource ownership follows the arena that owns the data; there are no C++ lifetime semantics here.

`anostr_t` is the existing ownership model. A value may borrow immutable backing storage, and `anostr_keep()` promotes it into a caller-owned destination heap. Inline values need no allocation; long values are copied into the destination heap. Resource promotion follows the same rule: take a resource handle and make the resource owned by the destination subsystem.

Resource handles travel through a lock-free Vyukov queue. Its hot cursors are cache-padded like the logger's head and tail. The queue moves handles between subsystem owners; it does not make generic RAM the permanent owner of the resource.

The renderer is a primary consumer of this operation. It has its own CPU memory and its own GPU memory. It takes a resource handle, conditions or copies the resource into renderer-owned memory, and uploads or retains the resulting representation in GPU memory. Those allocations belong to the renderer, not to a generic resource-manager lifetime domain.
