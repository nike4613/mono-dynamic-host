#include <stdlib.h>
#include "pal.h"
#include "mono.h"

static int mdh_verbose;

#define LVL_DEBUG 1
#define LVL_VERBOSE 2
#define LVL_NOISY 3

static void load_global_vars(void)
{
  char* verbose = PAL_DupEnv("MDH_VERBOSE");
  if (verbose)
  {
    mdh_verbose = atoi(verbose);
    free(verbose);
  }
}

static void mdh_log(int level, char const* format, ...)
{
  if (level > mdh_verbose) return;

  va_list args;
  va_start(args, format);

  int strSize = vsnprintf(NULL, 0, format, args);
  char* buffer = malloc(strSize + 1);
  vsnprintf(buffer, strSize + 1, format, args);

  va_end(args);

  printf("mdh[%d]: %s\n", level, buffer);
  free(buffer);
}

#define LOG(level, message) do { if (level <= mdh_verbose) { mdh_log(level, message); } } while (0)
#define LOG_DBG(message) LOG(LVL_DEBUG, message);
#define LOG_VRB(message) LOG(LVL_VERBOSE, message);
#define LOG_NSY(message) LOG(LVL_NOISY, message);
#define LOGF(level, format, ...) do { if (level <= mdh_verbose) { mdh_log(level, format, __VA_ARGS__); } } while (0)
#define LOGF_DBG(format, ...) LOGF(LVL_DEBUG, format, __VA_ARGS__);
#define LOGF_VRB(format, ...) LOGF(LVL_VERBOSE, format, __VA_ARGS__);
#define LOGF_NSY(format, ...) LOGF(LVL_NOISY, format, __VA_ARGS__);

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
      "  If the target Mono library is named 'coreclr' (and so is a .NET MonoVM variety), you may\n"
      "need to specify MDH_TRUSTED_PLATFORM_ASSEMBLIES or TRUSTED_PLATFORM_ASSEMBLIES, according to\n"
      "the .NET hosting documentation. The simplest value is just the full path to the primary corlib.\n"
    );
  }
}

static char* probe_corlib_path(char const* monoDllDirname, char const* corlibName)
{
  size_t dirnameLen = strlen(monoDllDirname);
  size_t corlibNameLen = strlen(corlibName);

  // dirname always includes trailing slash, so we can just shove together the bits of the filename
  char* testPath = malloc(dirnameLen + corlibNameLen + 4 + 1);
  strcpy(testPath, monoDllDirname);
  strcpy(testPath + dirnameLen, corlibName);
  strcpy(testPath + dirnameLen + corlibNameLen, ".dll");

  LOGF_VRB("probing %s", testPath);
  if (PAL_FileExists(testPath))
  {
    // if the file exists, return the constructed path
    return testPath;
  }
  else
  {
    // otherwise, free it and return null
    free(testPath);
    return NULL;
  }
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

typedef enum
{
  MONOVM_Success,
  MONOVM_NotMonoVM,
  MONOVM_Error,
} MonoVMInitResult;

static MonoVMInitResult try_init_monovm(char const* monoDll, void* monoHnd)
{
  LOAD_SYM_OPT(monovm_initialize);
  if (!monovm_initialize) return MONOVM_NotMonoVM;

  LOG_DBG("provided Mono is .NET MonoVM");

  // this is MonoVM, we need to give it TRUSTED_PLATFORM_ASSEMBLIES to be able to load its corlib.
  // We'll look in some env vars first, then probe next to the Mono binary itself.

  char* dirname = NULL;
  char* corlib = NULL;

  // first try MDH-specific env var
  corlib = PAL_DupEnv("MDH_TRUSTED_PLATFORM_ASSEMBLIES");
  if (!corlib)
  {
    // then try a general env var
    corlib = PAL_DupEnv("TRUSTED_PLATFORM_ASSEMBLIES");
  }
  // if we haven't gotten an env var, probe for corlibs
  if (!corlib)
  {
    dirname = PAL_Dirname(monoDll);
    LOGF_VRB("got dirname of Mono dll: %s", dirname);
    corlib = probe_corlib_path(dirname, "System.Private.CoreLib");
  }
  if (!corlib)
  {
    corlib = probe_corlib_path(dirname, "mscorlib");
  }

  free(dirname);

  if (corlib)
  {
    LOGF_DBG("found TRUSTED_PLATFORM_ASSEMBLIES value: %s", corlib);
  }
  else
  {
    LOG_DBG("corlib probing failed; no TRUSTED_PLATFORM_ASSEMBLIES");
  }

  // only pass in the prop if we found a corlib during probing
  int numProperties = corlib ? 1 : 0;

  const char* propertyKeys[] = {
    "TRUSTED_PLATFORM_ASSEMBLIES"
  };

  const char* propertyValues[] = {
    corlib
  };

  int result = monovm_initialize(numProperties, propertyKeys, propertyValues);
  free(corlib);
  if (result != 0)
  {
    PAL_ReportError("monovm_initialize() returned %#lx", result);
    return MONOVM_Error;
  }

  return MONOVM_Success;
}

int main(int argc, char **argv)
{
  load_global_vars();

  if (mdh_verbose >= LVL_NOISY)
  {
    // print out arguments
    LOGF_NSY("argc = %d", argc);

    for (int i = 0; i < argc; i++)
    {
      LOGF_NSY("argv[%d]=%s", i, argv[i]);
    }

    LOG_NSY("----------------");
  }

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

  // before trying to run mono_main, try to call monovm_initialize
  if (try_init_monovm(argv[1], monoHnd) == MONOVM_Error)
  {
    // this Mono is MonoVM, but it returned an error from monovm_initialize. Exit.
    return 1;
  }

  LOAD_SYM_OPT(mono_main);
  if (mono_main)
  {
    LOG_DBG("found mono_main");
    // we have mono_main, pass off to that
    return mono_main(argc - 1, argv + 1);
  }
  
  // we don't have mono main, check for help message as necessary first
  LOG_DBG("could not find mono_main");

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
