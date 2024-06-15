#pragma once

typedef struct MonoDomain MonoDomain;
typedef struct MonoAssembly MonoAssembly;
typedef enum {
  MONO_DEBUG_FORMAT_NONE,
  MONO_DEBUG_FORMAT_MONO,
  MONO_DEBUG_FORMAT_DEBUGGER, // <-- Deprecated
} MonoDebugFormat;

// Main Mono entrypoint
typedef int (*mono_main_t)(int argc, char* argv[]);

// dotnet/runtime Mono runtime initialization
typedef int (*monovm_initialize_t)(int propertyCount, char const** propertyKeys, char const** propertyValues);

// Manual hosting APIs
typedef MonoDomain* (*mono_jit_init_t)(char const* file);
typedef MonoAssembly* (*mono_domain_assembly_open_t)(MonoDomain* domain, char const* name);
typedef void (*mono_assembly_setrootdir_t)(char const* root_dir);
typedef void (*mono_debug_init_t)(MonoDebugFormat format);
typedef void (*mono_debug_domain_create_t)(MonoDomain* domain);
typedef int (*mono_jit_exec_t)(MonoDomain* domain, MonoAssembly* assembly, int argc, char* argv[]);
typedef int (*mono_environment_exitcode_get_t)(void);
typedef void (*mono_jit_cleanup_t)(MonoDomain* domain);
