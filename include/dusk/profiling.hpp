#pragma once

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
