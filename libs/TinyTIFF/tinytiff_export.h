#ifndef TINYTIFF_EXPORT_H
#define TINYTIFF_EXPORT_H

#if defined(_WIN32)
#  if defined(TINYTIFF_BUILD_SHARED)
#    if defined(TINYTIFF_EXPORTS)
#      define TINYTIFF_EXPORT __declspec(dllexport)
#    else
#      define TINYTIFF_EXPORT __declspec(dllimport)
#    endif
#  else
#    define TINYTIFF_EXPORT
#  endif
#else
#  define TINYTIFF_EXPORT
#endif

#endif // TINYTIFF_EXPORT_H
