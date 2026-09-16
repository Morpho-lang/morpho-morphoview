/** @file mvversion.h
 *  @author T J Atherton
 *
 *  @brief MorphoView version and command-protocol numbers
 */

#ifndef mvversion_h
#define mvversion_h

#define MORPHOVIEW_VERSION_MAJOR 0
#define MORPHOVIEW_VERSION_MINOR 7
#define MORPHOVIEW_VERSION_PATCH 0
#define MORPHOVIEW_PROTOCOL 2

#define MORPHOVIEW_STRINGIFY_(x) #x
#define MORPHOVIEW_STRINGIFY(x) MORPHOVIEW_STRINGIFY_(x)

#define MORPHOVIEW_VERSIONSTRING \
    MORPHOVIEW_STRINGIFY(MORPHOVIEW_VERSION_MAJOR) "." \
    MORPHOVIEW_STRINGIFY(MORPHOVIEW_VERSION_MINOR) "." \
    MORPHOVIEW_STRINGIFY(MORPHOVIEW_VERSION_PATCH)

#endif
