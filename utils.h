#ifndef __UTILS_H__
#define __UTILS_H__

// Enable debug logging
//#define DEBUG 1

#ifdef DEBUG
#define LOGD(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define LOGD(fmtn, ...)
#endif

#endif