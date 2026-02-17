# C Architectural Patterns

A set of architectural patterns for building modular, composable C libraries. These patterns prioritize explicit ownership, zero internal allocation, and pipeline-style composition.

## 1. Opaque Types

Every component is represented as an **opaque type**: a forward-declared struct in the header, with the full definition hidden in the `.c` file.

**Header:**
```c
typedef struct my_filter my_filter_t;
```

**Source:**
```c
struct my_filter {
    float cutoff;
    float state[2];
};
```

Consumers interact only through function pointers and API functions — never by accessing fields directly. This enforces encapsulation and allows internal layout changes without breaking callers.

## 2. Caller-Owned Allocation

The library never allocates memory. Instead, every component follows a strict three-step protocol:

1. **`alloc_size(config)`** — returns how many bytes the caller must provide
2. **Caller allocates** — using whatever strategy is appropriate for their context
3. **`init(ptr, config, ...)`** — initializes the caller-provided memory

When configuration doesn't affect allocation size, `alloc_size` takes no arguments:

```c
// Header — size depends on config
size_t my_filter_alloc_size(const my_filter_config_t *config);
void   my_filter_init(my_filter_t *f, const my_filter_config_t *config,
                       my_source_t *input);

// Header — fixed size, no config needed
size_t my_buffer_alloc_size(void);
void   my_buffer_init(my_buffer_t *b, const uint8_t *data, size_t len);
```

```c
// Caller: stack allocation
uint8_t mem[my_filter_alloc_size(&config)];
my_filter_t *f = (my_filter_t *)mem;
my_filter_init(f, &config, &input);
```

```c
// Caller: heap allocation
size_t sz = my_filter_alloc_size(&config);
my_filter_t *f = (my_filter_t *)malloc(sz);
my_filter_init(f, &config, &input);
// ... later ...
free(f);
```

### Alignment

The caller is responsible for ensuring the allocated memory meets the component's alignment requirements. `malloc` always returns suitably aligned memory. For stack or embedded allocations, the caller must ensure proper alignment — for example, by casting from an `alignas`-qualified buffer or by using a union with `max_align_t`:

```c
// Stack allocation with explicit alignment
alignas(max_align_t) uint8_t mem[my_filter_alloc_size(&config)];
my_filter_t *f = (my_filter_t *)mem;
```

### Why This Matters

- **Ownership is always unambiguous.** Whoever calls `alloc_size` owns the memory. The library never has to track, free, or worry about lifetime.
- **No hidden allocations.** The library has no `malloc` calls, no allocation failures to handle, no cleanup paths to forget.
- **Context-appropriate strategy.** The same library works on the stack for short-lived CLI tools, on the heap for long-lived plugin objects, or embedded in a parent struct for composite components. The library doesn't need to know or care.
- **Predictable memory usage.** Total memory cost is known at initialization time.

### Configuration Structs

Initialization parameters are grouped into a configuration struct. This avoids long parameter lists, makes defaults explicit, and allows the config to influence allocation size.

```c
typedef struct {
    unsigned int sample_rate;
    unsigned int kernel_size;
    float        initial_cutoff;
} my_filter_config_t;
```

The config is read during `alloc_size` and `init`, but is not retained — the component copies what it needs. The caller can discard or reuse the config struct after `init` returns.

## 3. Streaming Source Interfaces

Components communicate through **source interfaces** — small structs containing a function pointer and an opaque context pointer:

```c
typedef struct {
    unsigned int (*read)(void *ctx, float *dst, unsigned int len);
    void *ctx;
} my_float_source_t;
```

This is a vtable with one method. The `read` function:
- Writes up to `len` values into `dst`
- Returns the number of values written
- Returns `0` to signal end of stream
- May return fewer than `len` values (partial reads)

### Exposing a Source

Each component provides a function to populate a source struct pointing to itself:

```c
void my_filter_source(my_filter_t *f, my_float_source_t *out)
{
    out->read = filter_read;  // static function in .c
    out->ctx = f;
}
```

### Consuming a Source

Components accept source interfaces as dependencies during initialization:

```c
void my_filter_init(my_filter_t *f,
                    const my_filter_config_t *config,
                    my_float_source_t *input);
```

The component stores the source struct (function pointer + context) and calls `input->read(input->ctx, ...)` when it needs data.

### Source Type Variants

Define separate source types for each data type in the system. Common variants:

```c
typedef struct {
    unsigned int (*read)(void *ctx, uint8_t *dst, unsigned int len);
    void *ctx;
} my_byte_source_t;

typedef struct {
    unsigned int (*read)(void *ctx, float *dst, unsigned int len);
    void *ctx;
} my_float_source_t;

typedef struct {
    unsigned int (*read)(void *ctx, my_frame_t *dst, unsigned int len);
    void *ctx;
} my_frame_source_t;
```

This provides type safety at the API boundary — you can't accidentally wire a byte source into a float consumer.

## 4. Pipeline Composition

Because every component both consumes and exposes source interfaces, you build processing pipelines by chaining them:

```c
// Build pipeline: generator → filter → resampler → output
my_generator_t *gen = ...;
my_float_source_t gen_src;
my_generator_source(gen, &gen_src);

my_filter_t *filt = ...;
my_filter_init(filt, &filt_config, &gen_src);
my_float_source_t filt_src;
my_filter_source(filt, &filt_src);

my_resampler_t *res = ...;
my_resampler_init(res, &res_config, &filt_src);
my_float_source_t output;
my_resampler_source(res, &output);

// Pull samples from the end of the chain
float buf[256];
unsigned int n = output.read(output.ctx, buf, 256);
```

Data flows **on demand**: reading from the tail pulls through the entire chain. Each component reads from its upstream source only when it needs more input.

### Pipeline Properties

- **Lazy evaluation.** No work happens until someone reads from the output.
- **Backpressure.** Each stage only processes as much as its downstream consumer requests.
- **Composable.** Any source-compatible component can be inserted, removed, or replaced without changing the rest of the chain.
- **No central scheduler.** The pipeline is driven by the consumer, not a framework.

## 5. Embedded Child Allocation

When a component needs to own a child object whose size is configuration-dependent, use a **flexible array member** as trailing storage:

```c
struct my_synth {
    /* Synth fields */
    float sample_rate;
    my_decoder_t *decoder;  /* Points into storage[] */

    /* Trailing storage for child allocations */
    alignas(max_align_t) uint8_t storage[];
};
```

The parent's `alloc_size` includes space for the child:

```c
size_t my_synth_alloc_size(const my_synth_config_t *config)
{
    return sizeof(my_synth_t) + my_decoder_alloc_size();
}
```

And `init` places the child in the trailing storage:

```c
void my_synth_init(my_synth_t *s, ...)
{
    s->decoder = (my_decoder_t *)s->storage;
    my_decoder_init(s->decoder, ...);
}
```

This keeps the caller-owned-allocation guarantee intact — the parent's single allocation covers everything. The caller still sees one `alloc_size` / one `init` / one `free`.

### Multiple Children

For multiple children, partition the storage sequentially:

```c
size_t my_resampler_alloc_size(const my_resampler_config_t *config)
{
    size_t size = sizeof(my_resampler_t);
    size += N * sizeof(float);        /* input buffer */
    size += n_sinc * sizeof(float);   /* sinc table */
    size += n_sinc * sizeof(float);   /* delta table */
    size += N * sizeof(float);        /* window weights */
    return size;
}

void my_resampler_init(my_resampler_t *r, ...)
{
    uint8_t *ptr = r->storage;

    r->input_buf = (float *)ptr;
    ptr += N * sizeof(float);

    r->sinc = (float *)ptr;
    ptr += n_sinc * sizeof(float);

    /* ... */
}
```

When the storage contains types with alignment requirements (e.g., `float`), declare the flexible array member with appropriate alignment:

```c
alignas(float) uint8_t storage[];
```

## 6. Code Organization

### File Structure

Each component occupies one header and one source file. The source file is organized top to bottom:

1. **Includes**
2. **Constants** (`#define`)
3. **Internal struct definition** (the opaque type's real layout)
4. **Static helper functions**
5. **Source interface implementation** (the `read` callback)
6. **Public API** (`alloc_size`, `init`, `source`, etc.)

### Header Structure

```c
#ifndef MY_COMPONENT_H
#define MY_COMPONENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Type declarations, config structs, API functions */

#ifdef __cplusplus
}
#endif

#endif
```

Every header includes `extern "C"` guards so the library can be consumed from C++ without additional wrapping.

### Naming Conventions

- Types: `prefix_name_t` (e.g., `my_filter_t`, `my_filter_config_t`)
- Functions: `prefix_name_verb` (e.g., `my_filter_init`, `my_filter_source`)
- Source types: `prefix_T_source_t` where T is the element type
- Alloc/init pair: `prefix_name_alloc_size`, `prefix_name_init`
- Source exposure: `prefix_name_source` or `prefix_name_as_T_source`
