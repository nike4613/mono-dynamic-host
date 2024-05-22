#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
# define MDH_WINDOWS
# ifdef _WIN64
#   define BITS64
#	endif	
#elif __APPLE__
# define MDH_OSX
#elif defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
#	define MDH_POSIX
#else
#	error "Unknown platform"
#endif

#include <stdio.h>
#include <stdlib.h>

#ifdef MDH_WINDOWS
# define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

#if defined(MDH_POSIX) || defined(MDH_OSX)
#	define MDH_LIBDL
#	include <dlfcn.h>
#endif

// ---- PAL ----

#if defined(MDH_LIBDL)

void PAL_Init(void)
{
  // always clear dlerror() just to be sure
  dlerror();
}

void* PAL_LoadLibrary(char const* name)
{
  return dlopen(name, RTLD_LAZY | RTLD_GLOBAL);
}

void* PAL_GetSymbol(void* handle, char const* name)
{
  return dlsym(handle, name);
}

void PAL_ReportError(char const* message, ...)
{
  va_list args;
  va_start(args, message);

  int strSize = vsnprintf(NULL, 0, message, args);
  char* buffer = malloc(strSize + 1);
  vsnprintf(buffer, strSize + 1, message, args);

  va_end(args);

  fprintf(stderr, "mdh: %s: %s\n", buffer, dlerror());
}

#elif defined(MDH_WINDOWS)

void PAL_Init(void)
{
  // Windows needs no initialization.
}

void* PAL_LoadLibrary(char const* name)
{
  return (void*)LoadLibraryA(name);
}

void* PAL_GetSymbol(void* handle, char const* name)
{
  return (void*)GetProcAddress((HMODULE)handle, name);
}

void PAL_ReportError(char const* message, ...)
{
  va_list args;
  va_start(args, message);

  int strSize = vsnprintf(NULL, 0, message, args);
  char* buffer = malloc(strSize + 1);
  vsnprintf(buffer, strSize + 1, message, args);

  va_end(args);

  DWORD error = GetLastError();
  BOOL localFree = TRUE;
  LPSTR fmessage;
  if (!FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                      NULL, error, 0, (LPSTR)&fmessage, 0, NULL))
  {
    DWORD error2 = GetLastError();
    strSize = snprintf(NULL, 0, "could not format error core %#lx (%#lx)", error, error2);
    fmessage = malloc(strSize + 1);
    localFree = FALSE;
    snprintf(fmessage, strSize, "could not format error core %#lx (%#lx)", error, error2);
  }

  fprintf(stderr, "mdh: %s: %s", buffer, fmessage);

  if (localFree)
  {
    LocalFree(fmessage);
  }
  else
  {
    free(fmessage);
  }
  free(buffer);
}

#else
#error "Unknown dynamic loader"
#endif

// ---- MONO APIS ----
typedef struct MonoDomain MonoDomain;
typedef struct MonoAssembly MonoAssembly;
typedef enum {
  MONO_DEBUG_FORMAT_NONE,
  MONO_DEBUG_FORMAT_MONO,
  MONO_DEBUG_FORMAT_DEBUGGER, // <-- Deprecated
} MonoDebugFormat;

typedef MonoDomain* (*mono_jit_init_t)(char const* file);
typedef MonoAssembly* (*mono_domain_assembly_open_t)(MonoDomain* domain, char const* name);
typedef void (*mono_debug_init_t)(MonoDebugFormat format);
typedef void (*mono_debug_domain_create_t)(MonoDomain* domain);
typedef int (*mono_jit_exec_t)(MonoDomain* domain, MonoAssembly* assembly, int argc, char* argv[]);

// ---- MAIN ENTRY POINT ----

int main(int argc, char **argv)
{
  // We always require at least 2 arguments
  if(argc < 3)
  {
    printf("usage: %s <mono dynamic library> <.NET executable> [<arg> ...]\n", argv[0]);
    return 1;
  }
  
  PAL_Init();

  void* monoHnd = PAL_LoadLibrary(argv[1]);
  if (!monoHnd)
  {
    PAL_ReportError("could not load mono from %s", argv[1]);
    return 1;
  }

#define LOAD_SYM_OPT(name) \
  name##_t name = (name##_t)PAL_GetSymbol(monoHnd, #name);
#define LOAD_SYM(name) \
  LOAD_SYM_OPT(name) \
  if (!name) \
  { \
    PAL_ReportError("could not load symbol '%s'", #name); \
    return 1; \
  }

  LOAD_SYM(mono_jit_init);
  LOAD_SYM(mono_domain_assembly_open);
  LOAD_SYM_OPT(mono_debug_init);
  LOAD_SYM_OPT(mono_debug_domain_create);
  LOAD_SYM(mono_jit_exec);
  
  // Now that we've loaded Mono, lets execute
  
  MonoDomain *domain = mono_jit_init(argv[2]);

  if (mono_debug_init && mono_debug_domain_create)
  {
    mono_debug_init(MONO_DEBUG_FORMAT_MONO);
    mono_debug_domain_create(domain);
  }

  MonoAssembly *assembly = mono_domain_assembly_open(domain, argv[2]);
  return mono_jit_exec(domain, assembly, argc - 2, argv + 2);
}
