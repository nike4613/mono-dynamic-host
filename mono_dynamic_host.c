#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
# define MDH_WINDOWS
# ifdef _WIN64
#   define BITS64
#	endif	
#elif __APPLE__
# define MDH_OSX
#elif defined(_POSIX_VERSION)
#	define MDH_POSIX
#else
#	error "Unknown platform"
#endif

#include <stdio.h>

#ifdef MDH_WINDOWS
#	include <windows.h>
#endif

#if defined(MDH_POSIX) || defined(MDH_OSX)
#	define MDH_LIBDL
#	include <dlfcn.h>
#endif

// Mono APIs that we need
typedef struct MonoDomain MonoDomain;
typedef struct MonoAssembly MonoAssembly;

typedef MonoDomain *(*mono_jit_init_t)(char const *file);
typedef MonoAssembly *(*mono_domain_assembly_open_t)(MonoDomain *domain, char const *name);
typedef int (*mono_jit_exec_t)(MonoDomain *domain, MonoAssembly *assembly, int argc, char *argv[]);

int main(int argc, char **argv)
{
  // We always require at least 2 arguments
  if(argc < 3)
  {
    printf("usage: %s <mono dynamic library> <.NET executable> [<arg> ...]\n", argv[0]);
    return 1;
  }
  
  #if defined(MDH_LIBDL)

    // always clear dlerror() just to be sure
    dlerror();
  
    void *monoHnd = dlopen(argv[1], RTLD_LAZY | RTLD_GLOBAL);
    if (!monoHnd)
    {
      fprintf(stderr, "mdh: could not load mono from %s: %s\n", argv[1], dlerror());
      return 1;
    }
  
    #define LOAD_SYM(name) \
      name##_t name = (name##_t)dlsym(monoHnd, #name); \
      if (!name) \
      { \
        fprintf(stderr, "mdh: could not load symbol '%s': %s\n", #name, dlerror()); \
        return 1; \
      }
  
  #elif defined(MDH_WINDOWS)

    HMODULE monoHnd = LoadLibraryA(argv[1]);
    if (!monoHnd)
    {
      DWORD error = GetLastError();
      LPSTR message;
      if (!FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                          NULL, error, 0, (LPSTR)&message, 0, NULL))
      {
        fprintf(stderr, "mdh: could not format error code %#lx: %#lx\r\n", error, GetLastError());
        return 1;
      }
    
      fprintf(stderr, "mdh: could not load mono from %s: %s\r\n", argv[1], message);
      return 1;
    }
  
    #define LOAD_SYM(name) \
      name##_t name = (name##_t)GetProcAddress(monoHnd, #name); \
      if (!name) \
      { \
        DWORD error = GetLastError(); \
        LPSTR message; \
        if (!FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, \
                            NULL, error, 0, (LPSTR)&message, 0, NULL)) \
        { \
          fprintf(stderr, "mdh: could not format error code %#lx: %#lx\r\n", error, GetLastError()); \
          return 1; \
        } \
        \
        fprintf(stderr, "mdh: could not load symbol '%s': %s\r\n", #name, message); \
        return 1; \
      }

  #else
    #error "Unknown dynamic loader"
  #endif
  
  LOAD_SYM(mono_jit_init);
  LOAD_SYM(mono_domain_assembly_open);
  LOAD_SYM(mono_jit_exec);
  
  // Now that we've loaded Mono, lets execute
  
  MonoDomain *domain = mono_jit_init(argv[2]);
  MonoAssembly *assembly = mono_domain_assembly_open(domain, argv[2]);
  return mono_jit_exec(domain, assembly, argc - 2, argv + 2);
}
