#pragma once

#include <Foundation/Types/TagSet.h>
#include <RendererCore/Declarations.h>

struct ezReflectionProbeMode
{
  typedef ezUInt8 StorageType;

  enum Enum
  {
    Static,
    Dynamic,

    Default = Static
  };
};
EZ_DECLARE_REFLECTABLE_TYPE(EZ_RENDERERCORE_DLL, ezReflectionProbeMode);

typedef ezGenericId<24, 8> ezReflectionProbeId;
