#pragma once

#include <ParticlePlugin/Initializer/ParticleInitializer.h>
#include <Core/ResourceManager/ResourceHandle.h>

typedef ezTypedResourceHandle<class ezCurve1DResource> ezCurve1DResourceHandle;

class ezParticleInitializerFactory_RandomRotationSpeed : public ezParticleInitializerFactory
{
  EZ_ADD_DYNAMIC_REFLECTION(ezParticleInitializerFactory_RandomRotationSpeed, ezParticleInitializerFactory);

public:

  virtual const ezRTTI* GetInitializerType() const override;
  virtual void CopyInitializerProperties(ezParticleInitializer* pInitializer) const override;

  virtual void Save(ezStreamWriter& stream) const override;
  virtual void Load(ezStreamReader& stream) override;

  ezAngle m_RotationPerSecond;
  float m_fRotationVariance;
};


class EZ_PARTICLEPLUGIN_DLL ezParticleInitializer_RandomRotationSpeed : public ezParticleInitializer
{
  EZ_ADD_DYNAMIC_REFLECTION(ezParticleInitializer_RandomRotationSpeed, ezParticleInitializer);

public:

  ezAngle m_RotationPerSecond;
  float m_fRotationVariance;


  virtual void CreateRequiredStreams() override;

protected:
  virtual void SpawnElements(ezUInt64 uiStartIndex, ezUInt64 uiNumElements) override;

  ezProcessingStream* m_pStreamRotationSpeed;
};