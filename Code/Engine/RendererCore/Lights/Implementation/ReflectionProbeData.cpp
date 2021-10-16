#include <RendererCore/RendererCorePCH.h>

#include <RendererCore/Lights/Implementation/ReflectionProbeData.h>

// clang-format off
EZ_BEGIN_STATIC_REFLECTED_ENUM(ezReflectionProbeMode, 1)
  EZ_BITFLAGS_CONSTANTS(ezReflectionProbeMode::Static, ezReflectionProbeMode::Dynamic)
EZ_END_STATIC_REFLECTED_ENUM;


EZ_STATICLINK_FILE(RendererCore, RendererCore_Lights_Implementation_ReflectionProbeData);
