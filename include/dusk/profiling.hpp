#ifndef DUSK_PROFILING_HPP
#define DUSK_PROFILING_HPP

// Profiling zone macros for game headers. Tracy is not part of the mod SDK
// surface: game builds (which link TracyClient and see its include dir) get
// real Tracy zones, while consumers without Tracy — mods built against the
// slim SDK — get no-ops. Zones only affect inline function bodies, never
// struct layout. TUs that use the Tracy API directly (FrameMark, plots, ...)
// include tracy/Tracy.hpp themselves; mods that want profiling bring their
// own Tracy.
#if defined(__has_include)
#if __has_include(<tracy/Tracy.hpp>)
#include <tracy/Tracy.hpp>
#define DUSK_ZONE ZoneScoped
#define DUSK_ZONE_N(name) ZoneScopedN(name)
#endif
#endif

#ifndef DUSK_ZONE
#define DUSK_ZONE
#define DUSK_ZONE_N(name)
#endif

#endif  // DUSK_PROFILING_HPP
