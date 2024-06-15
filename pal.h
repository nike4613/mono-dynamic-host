#pragma once
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
# include <windows.h>
#endif

#if defined(MDH_POSIX) || defined(MDH_OSX)
# define MDH_LIBDL
# include <dlfcn.h>
# include <libgen.h> // dirname
# include <sys/types.h> // stat
# include <sys/stat.h> // stat
# include <unistd.h> // stat
#endif

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

static char* PAL_Dirname(char const* path)
{
  // on *nix, we'll use dirname, but havet o do some funky stuff to get it to behave
  char* dupPath = PAL_StrDup(path);
  char* dir = dirname(dupPath);

  size_t len = strlen(dir);
  int addSlash = 0;
  if (len && dir[len - 1] != '/')
  {
    addSlash = 1;
  }

  char* result = malloc(len + addSlash + 1);
  result[0] = 0;
  strcpy(result, dir);
  if (addSlash)
  {
    result[len] = '/';
    result[len + 1] = 0;
  }
  // note: dir cannot be freed; it may be in the same alloc as dupPath though
  free(dupPath);
  return result;
}

static int PAL_FileExists(char const* path)
{
  struct stat statbuf;

  if (stat(path, &statbuf) == -1) return 0;
  return (statbuf.st_mode & S_IFMT) == S_IFREG;
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

static char* PAL_Dirname(char const* path)
{
  // on Windows, we'll use _splitpath_s

  size_t pathLen = strlen(path);
  char driveBuffer[4] = { 0 };
  char* dirBuffer = malloc(pathLen + 1);

  errno_t result = _splitpath_s(path,
    driveBuffer, sizeof(driveBuffer) / sizeof(driveBuffer[0]),
    dirBuffer, pathLen,
    NULL, 0,  // filename
    NULL, 0); // extension

  if (result != 0)
  {
    PAL_ReportError("PAL_Dirname(%s): _splitpath_s() returned %d", path, result);
    exit(1);
  }

  // now we have the split, lets merge the bits we actually care about back together
  char* resultBuffer = malloc(pathLen + 1);
  resultBuffer[0] = 0;
  strcat(resultBuffer, driveBuffer);
  strcat(resultBuffer, dirBuffer);
  free(dirBuffer);

  // result includes trailing slash
  return resultBuffer;
}

static int PAL_FileExists(char const* path)
{
  DWORD attr = GetFileAttributesA(path);

  // file exists, and is not a directory
  return attr != INVALID_FILE_ATTRIBUTES
    && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

#else
#error "Unknown platform"
#endif