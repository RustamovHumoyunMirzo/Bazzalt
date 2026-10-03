#pragma once
#if defined(_WIN32)
# if defined(BAZZALT_BUILDING_LIBRARY)
#  define BAZZALT_API __declspec(dllexport)
# else
#  define BAZZALT_API __declspec(dllimport)
# endif
#else
# define BAZZALT_API __attribute__((visibility("default")))
#endif
