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
#include <stdarg.h>
#include <string.h>

#ifdef MDH_WINDOWS
# define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

#if defined(MDH_POSIX) || defined(MDH_OSX)
#	define MDH_LIBDL
#	include <dlfcn.h>
#endif

// ---- PAL ----

static char* PAL_StrDup(char const* str)
{
#if __STDC_VERSION__ >= 202300L
  // C23
  return strdup(str);
#else
  char* result = malloc(strlen(str) + 1);
  strcpy(result, str);
  return result;
#endif
}

#if defined(MDH_LIBDL)

static void PAL_Init(void)
{
  // always clear dlerror() just to be sure
  dlerror();
}

static void* PAL_LoadLibrary(char const* name)
{
  return dlopen(name, RTLD_LAZY | RTLD_GLOBAL);
}

static void* PAL_GetSymbol(void* handle, char const* name)
{
  return dlsym(handle, name);
}

static char* PAL_DupEnv(char const* name)
{
  char* env = getenv(name);
  if (!env) return NULL;
  return PAL_StrDup(env);
}

static void PAL_ReportError(char const* message, ...)
{
  char* error = PAL_StrDup(dlerror());

  va_list args;
  va_start(args, message);

  int strSize = vsnprintf(NULL, 0, message, args);
  char* buffer = malloc(strSize + 1);
  vsnprintf(buffer, strSize + 1, message, args);

  va_end(args);

  fprintf(stderr, "mdh: %s: %s\n", buffer, error);

  free(error);
}

#elif defined(MDH_WINDOWS)

static void PAL_Init(void)
{
  // Windows needs no initialization.
}

static void* PAL_LoadLibrary(char const* name)
{
  return (void*)LoadLibraryA(name);
}

static void* PAL_GetSymbol(void* handle, char const* name)
{
  return (void*)GetProcAddress((HMODULE)handle, name);
}

static char* PAL_DupEnv(char const* name)
{
  DWORD size = GetEnvironmentVariableA(name, NULL, 0);
  if (size == 0) return NULL; // env var was not found
  char* buffer = malloc(size + 1);
  GetEnvironmentVariableA(name, buffer, size + 1);
  return buffer;
}

static void PAL_ReportError(char const* message, ...)
{
  DWORD error = GetLastError();

  va_list args;
  va_start(args, message);

  int strSize = vsnprintf(NULL, 0, message, args);
  char* buffer = malloc(strSize + 1);
  vsnprintf(buffer, strSize + 1, message, args);

  va_end(args);

  BOOL localFree = TRUE;
  LPSTR fmessage = NULL;
  if (!FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL, error, 0, (LPSTR)&fmessage, 0, NULL))
  {
    DWORD error2 = GetLastError();
    strSize = snprintf(NULL, 0, "could not format error core %#lx (%#lx)", error, error2);
    fmessage = malloc(strSize + 1);
    localFree = FALSE;
    snprintf(fmessage, strSize + 1, "could not format error core %#lx (%#lx)", error, error2);
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

typedef int (*mono_main_t)(int argc, char* argv[]);

typedef MonoDomain* (*mono_jit_init_t)(char const* file);
typedef MonoAssembly* (*mono_domain_assembly_open_t)(MonoDomain* domain, char const* name);
typedef void (*mono_assembly_setrootdir_t)(char const* root_dir);
typedef void (*mono_debug_init_t)(MonoDebugFormat format);
typedef void (*mono_debug_domain_create_t)(MonoDomain* domain);
typedef int (*mono_jit_exec_t)(MonoDomain* domain, MonoAssembly* assembly, int argc, char* argv[]);
typedef int (*mono_environment_exitcode_get_t)(void);
typedef void (*mono_jit_cleanup_t)(MonoDomain* domain);

// ---- MAIN ENTRY POINT ----

static void print_usage(char const* name, int includeHelp)
{
  printf("usage: %s <mono dynamic library> <.NET executable> [<arg> ...]\n", name);

  if (includeHelp)
  {
    printf(
      "\n"
      "                                  Mono Dynamic Host\n"
      "\n"
      "  The Mono Dynamic Host enables the use of any Mono shared library to run applications.\n"
      "It's primary use is to be able to run standalone applications using the Mono runtime\n"
      "used by Unity.\n"
      "\n"
      "<mono dynamic library>      The (full or relative) path to the Mono shared library to\n"
      "                            use. For instance, it might be \"mono-2.0-bdwgc.dll\" to use\n"
      "                            a Unity Mono build on Windows.\n"
      "\n"
      "     <.NET Executable>      The .NET executable to invoke as an entrypoint. This doesn't\n"
      "                            actually need to be an EXE, it just needs to have an entrypoint\n"
      "                            defined in .NET metadata.\n"
      "\n"
      "  Depending on the layout of the application and the corelib, it may be necessary to specify\n"
      "the MONO_PATH and MONO_CFG_DIR environment variables. MONO_PATH should be the path to the\n"
      "directory containing the corelibs, and MONO_CFG_DIR should be the \"/etc\" in the normal\n"
      "Mono installation.\n"
      "\n"
      "  Note also that, if mono_main is available, that will be used instead of any other hosting\n"
      "APIs.\n"
    );
  }
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    print_usage(argv[0], 0);
    return 1;
  }
  
  if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
  {
    print_usage(argv[0], 1);
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

  LOAD_SYM_OPT(mono_main);

  if (mono_main)
  {
    // we have mono_main, pass off to that
    return mono_main(argc - 1, argv + 1);
  }
  
  // we don't have mono main, check for help message as necessary first

  if (argc < 3)
  {
    print_usage(argv[0], 0);
    return 1;
  }

  LOAD_SYM(mono_jit_init);
  LOAD_SYM(mono_domain_assembly_open);
  LOAD_SYM_OPT(mono_debug_init);
  LOAD_SYM_OPT(mono_debug_domain_create);
  LOAD_SYM(mono_jit_exec);
  LOAD_SYM_OPT(mono_environment_exitcode_get);
  LOAD_SYM(mono_jit_cleanup);
  
  // Now that we've loaded Mono, lets execute
  
  // lets try to set the Mono root dir if MONO_PATH is set
  char* monoPathEnv = PAL_DupEnv("MONO_PATH");
  if (monoPathEnv)
  {
    LOAD_SYM_OPT(mono_assembly_setrootdir);

    if (mono_assembly_setrootdir)
    {
      mono_assembly_setrootdir(monoPathEnv);
    }
    else
    {
      PAL_ReportError("MONO_PATH was set, but %s did not have symbol %s to configure it", argv[1], "mono_assembly_setrootdir");
    }

    free(monoPathEnv);
  }

  MonoDomain *domain = mono_jit_init(argv[2]);

  if (mono_debug_init && mono_debug_domain_create)
  {
    mono_debug_init(MONO_DEBUG_FORMAT_MONO);
    mono_debug_domain_create(domain);
  }

  MonoAssembly *assembly = mono_domain_assembly_open(domain, argv[2]);
  int result = mono_jit_exec(domain, assembly, argc - 2, argv + 2);

  if (mono_environment_exitcode_get)
  {
    result = mono_environment_exitcode_get();
  }

  mono_jit_cleanup(domain);

  return result;
}
