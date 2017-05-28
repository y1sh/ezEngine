#pragma once

#include <RendererCore/Lights/DirectionalLightComponent.h>
#include <RendererCore/Lights/PointLightComponent.h>
#include <RendererCore/Lights/SpotLightComponent.h>

#include <RendererCore/../../../Data/Base/Shaders/Common/LightData.h>
EZ_DEFINE_AS_POD_TYPE(ezPerClusterData);

#include <Foundation/Math/Float16.h>
#include <Foundation/SimdMath/SimdConversion.h>
#include <Foundation/SimdMath/SimdVec4i.h>

namespace
{
  ///\todo Make this configurable.
  static float s_fMinLightDistance = 5.0f;
  static float s_fMaxLightDistance = 500.0f;

  static float s_fDepthSliceScale = (NUM_CLUSTERS_Z - 1) / (ezMath::Log2(s_fMaxLightDistance) - ezMath::Log2(s_fMinLightDistance));
  static float s_fDepthSliceBias = -s_fDepthSliceScale * ezMath::Log2(s_fMinLightDistance) + 1.0f;

  EZ_ALWAYS_INLINE float GetDepthFromSliceIndex(ezUInt32 uiSliceIndex)
  {
    return ezMath::Pow(2.0f, (uiSliceIndex - s_fDepthSliceBias + 1.0f) / s_fDepthSliceScale);
  }

  EZ_ALWAYS_INLINE ezUInt32 GetSliceIndexFromDepth(float fLinearDepth)
  {
    return ezMath::Clamp((ezInt32)(ezMath::Log2(fLinearDepth) * s_fDepthSliceScale + s_fDepthSliceBias), 0, NUM_CLUSTERS_Z - 1);
  }

  EZ_ALWAYS_INLINE ezUInt32 GetClusterIndexFromCoord(ezUInt32 x, ezUInt32 y, ezUInt32 z)
  {
    return z * NUM_CLUSTERS_XY + y * NUM_CLUSTERS_X + x;
  }

  // in order: tlf, trf, blf, brf, tln, trn, bln, brn
  EZ_FORCE_INLINE void GetClusterCornerPoints(const ezCamera& camera, float fZf, float fZn, float fTanFovX, float fTanFovY,
    ezInt32 x, ezInt32 y, ezInt32 z, ezVec3* out_pCorners)
  {
    const ezVec3& pos = camera.GetPosition();
    const ezVec3& dirForward = camera.GetDirForwards();
    const ezVec3& dirRight = camera.GetDirRight();
    const ezVec3& dirUp = camera.GetDirUp();

    float fStepXf = (fZf * fTanFovX) / NUM_CLUSTERS_X;
    float fStepYf = (fZf * fTanFovY) / NUM_CLUSTERS_Y;

    float fXf = (x - (NUM_CLUSTERS_X / 2)) * fStepXf;
    float fYf = (y - (NUM_CLUSTERS_Y / 2)) * fStepYf;

    out_pCorners[0] = pos + dirForward * fZf + dirRight * fXf - dirUp * fYf;
    out_pCorners[1] = out_pCorners[0] + dirRight * fStepXf;
    out_pCorners[2] = out_pCorners[0] - dirUp * fStepYf;
    out_pCorners[3] = out_pCorners[2] + dirRight * fStepXf;

    float fStepXn = (fZn * fTanFovX) / NUM_CLUSTERS_X;
    float fStepYn = (fZn * fTanFovY) / NUM_CLUSTERS_Y;
    float fXn = (x - (NUM_CLUSTERS_X / 2)) * fStepXn;
    float fYn = (y - (NUM_CLUSTERS_Y / 2)) * fStepYn;

    out_pCorners[4] = pos + dirForward * fZn + dirRight * fXn - dirUp * fYn;
    out_pCorners[5] = out_pCorners[4] + dirRight * fStepXn;
    out_pCorners[6] = out_pCorners[4] - dirUp * fStepYn;
    out_pCorners[7] = out_pCorners[6] + dirRight * fStepXn;
  }

  EZ_ALWAYS_INLINE ezUInt32 Float3ToRGB10(ezVec3 value)
  {
    ezVec3 unsignedValue = value * 0.5f + ezVec3(0.5f);

    ezUInt32 r = ezMath::Clamp(static_cast<ezUInt32>(unsignedValue.x * 1023.0f + 0.5f), 0u, 1023u);
    ezUInt32 g = ezMath::Clamp(static_cast<ezUInt32>(unsignedValue.y * 1023.0f + 0.5f), 0u, 1023u);
    ezUInt32 b = ezMath::Clamp(static_cast<ezUInt32>(unsignedValue.z * 1023.0f + 0.5f), 0u, 1023u);

    return r | (g << 10) | (b << 20);
  }

  EZ_ALWAYS_INLINE ezUInt32 Float2ToRG16F(ezVec2 value)
  {
    ezUInt32 r = ezFloat16(value.x).GetRawData();
    ezUInt32 g = ezFloat16(value.y).GetRawData();

    return r | (g << 16);
  }

  void FillClusterBoundingSpheres(const ezCamera& camera, float fAspectRatio, ezArrayPtr<ezSimdBSphere> clusterBoundingSpheres)
  {
    float fTanFovX = ezMath::Tan(camera.GetFovX(fAspectRatio) * 0.5f);
    float fTanFovY = ezMath::Tan(camera.GetFovY(fAspectRatio) * 0.5f);
    ezSimdVec4f fov = ezSimdVec4f(fTanFovX, fTanFovY, fTanFovX, fTanFovY);

    ezSimdVec4f pos = ezSimdConversion::ToVec3(camera.GetPosition());
    ezSimdVec4f dirForward = ezSimdConversion::ToVec3(camera.GetDirForwards());
    ezSimdVec4f dirRight = ezSimdConversion::ToVec3(camera.GetDirRight());
    ezSimdVec4f dirUp = ezSimdConversion::ToVec3(camera.GetDirUp());

    ezSimdVec4f numClusters = ezSimdVec4f(NUM_CLUSTERS_X, NUM_CLUSTERS_Y, NUM_CLUSTERS_X, NUM_CLUSTERS_Y);
    ezSimdVec4f halfNumClusters = numClusters * 0.5f;
    ezSimdVec4f stepScale = fov.CompDiv(halfNumClusters);

    ezSimdVec4f fZn = ezSimdVec4f::ZeroVector();
    ezSimdVec4f cc[8];

    for (ezInt32 z = 0; z < NUM_CLUSTERS_Z; z++)
    {
      ezSimdVec4f fZf = ezSimdVec4f(GetDepthFromSliceIndex(z));
      ezSimdVec4f zff_znn = fZf.GetCombined<ezSwizzle::XXXX>(fZn);
      ezSimdVec4f steps = zff_znn.CompMul(stepScale);

      ezSimdVec4f depthF = pos + dirForward * fZf.x();
      ezSimdVec4f depthN = pos + dirForward * fZn.x();

      for (ezInt32 y = 0; y < NUM_CLUSTERS_Y; y++)
      {
        for (ezInt32 x = 0; x < NUM_CLUSTERS_X; x++)
        {
          ezSimdVec4f xyxy = ezSimdVec4i(x, y, x, y).ToFloat();
          ezSimdVec4f xfyf = (xyxy - halfNumClusters).CompMul(steps);

          cc[0] = depthF + dirRight * xfyf.x() - dirUp * xfyf.y();
          cc[1] = cc[0] + dirRight * steps.x();
          cc[2] = cc[0] - dirUp * steps.y();
          cc[3] = cc[2] + dirRight * steps.x();

          cc[4] = depthN + dirRight * xfyf.z() - dirUp * xfyf.w();
          cc[5] = cc[4] + dirRight * steps.z();
          cc[6] = cc[4] - dirUp * steps.w();
          cc[7] = cc[6] + dirRight * steps.z();

          ezSimdBSphere s; s.SetFromPoints(cc, 8);

          clusterBoundingSpheres[GetClusterIndexFromCoord(x, y, z)] = s;
        }
      }

      fZn = fZf;
    }
  }

  EZ_ALWAYS_INLINE void FillLightData(ezPerLightData& perLightData, const ezLightRenderData* pLightRenderData, ezUInt32 uiType)
  {
    ezMemoryUtils::ZeroFill(&perLightData);

    ezColorLinearUB lightColor = pLightRenderData->m_LightColor;
    lightColor.a = uiType;

    perLightData.colorAndType = *reinterpret_cast<ezUInt32*>(&lightColor.r);
    perLightData.intensity = pLightRenderData->m_fIntensity;
    perLightData.shadowDataOffset = pLightRenderData->m_uiShadowDataOffset;
  }

  void FillPointLightData(ezPerLightData& perLightData, const ezPointLightRenderData* pPointLightRenderData)
  {
    FillLightData(perLightData, pPointLightRenderData, LIGHT_TYPE_POINT);

    perLightData.position = pPointLightRenderData->m_GlobalTransform.m_vPosition;
    perLightData.invSqrAttRadius = 1.0f / (pPointLightRenderData->m_fRange * pPointLightRenderData->m_fRange);
  }

  void FillSpotLightData(ezPerLightData& perLightData, const ezSpotLightRenderData* pSpotLightRenderData)
  {
    FillLightData(perLightData, pSpotLightRenderData, LIGHT_TYPE_SPOT);

    perLightData.direction = Float3ToRGB10(-pSpotLightRenderData->m_GlobalTransform.m_Rotation.GetColumn(0));
    perLightData.position = pSpotLightRenderData->m_GlobalTransform.m_vPosition;
    perLightData.invSqrAttRadius = 1.0f / (pSpotLightRenderData->m_fRange * pSpotLightRenderData->m_fRange);

    const float fCosInner = ezMath::Cos(pSpotLightRenderData->m_InnerSpotAngle * 0.5f);
    const float fCosOuter = ezMath::Cos(pSpotLightRenderData->m_OuterSpotAngle * 0.5f);
    const float fSpotParamScale = 1.0f / ezMath::Max(0.001f, (fCosInner - fCosOuter));
    const float fSpotParamOffset = -fCosOuter * fSpotParamScale;
    perLightData.spotParams = Float2ToRG16F(ezVec2(fSpotParamScale, fSpotParamOffset));
  }

  void FillDirLightData(ezPerLightData& perLightData, const ezDirectionalLightRenderData* pDirLightRenderData)
  {
    FillLightData(perLightData, pDirLightRenderData, LIGHT_TYPE_DIR);

    perLightData.direction = Float3ToRGB10(-pDirLightRenderData->m_GlobalTransform.m_Rotation.GetColumn(0));
  }

  template <typename Cluster>
  void RasterizeBox(const ezBoundingBox& box, const ezSimdMat4f& viewProjectionMatrix, const ezVec3& cameraPosition,
    ezUInt32 uiBlockIndex, ezUInt32 uiMask, ezArrayPtr<Cluster> clusters)
  {
    ezVec3 vCorners[8];
    box.GetCorners(vCorners);

    ezSimdBBox screenSpaceBBox;
    screenSpaceBBox.SetInvalid();

    for (ezUInt32 i = 0; i < 8; ++i)
    {
      ezSimdVec4f screenSpaceCorner = viewProjectionMatrix.TransformPosition(ezSimdConversion::ToVec4(vCorners[i].GetAsVec4(1.0)));
      ezSimdFloat w = screenSpaceCorner.w();
      screenSpaceCorner /= w;
      screenSpaceCorner.SetW(w);

      screenSpaceBBox.ExpandToInclude(screenSpaceCorner);
    }

    ezSimdVec4f scale;
    ezSimdVec4f bias;

    if (ezProjectionDepthRange::Default == ezProjectionDepthRange::ZeroToOne)
    {
      scale = ezSimdVec4f(0.5f, 0.5f, 1.0f, 1.0f);
      bias = ezSimdVec4f(0.5f, 0.5f, 0.0f, 0.0f);
    }
    else
    {
      scale = ezSimdVec4f(0.5f);
      bias = ezSimdVec4f(0.5f);
    }

    screenSpaceBBox.m_Min = ezSimdVec4f::MulAdd(screenSpaceBBox.m_Min, scale, bias);
    screenSpaceBBox.m_Max = ezSimdVec4f::MulAdd(screenSpaceBBox.m_Max, scale, bias);

    if (box.Contains(cameraPosition))
    {
      screenSpaceBBox.m_Min = ezSimdVec4f(0.0f).GetCombined<ezSwizzle::XYZW>(screenSpaceBBox.m_Min);
      screenSpaceBBox.m_Max = ezSimdVec4f(1.0f).GetCombined<ezSwizzle::XYZW>(screenSpaceBBox.m_Max);
    }

    ezUInt32 xMin = ezMath::Clamp((ezInt32)((float)screenSpaceBBox.m_Min.x() * NUM_CLUSTERS_X), 0, NUM_CLUSTERS_X - 1);
    ezUInt32 xMax = ezMath::Clamp((ezInt32)((float)screenSpaceBBox.m_Max.x() * NUM_CLUSTERS_X), 0, NUM_CLUSTERS_X - 1);

    ezUInt32 yMin = ezMath::Clamp((ezInt32)((1.0f - (float)screenSpaceBBox.m_Max.y()) * NUM_CLUSTERS_Y), 0, NUM_CLUSTERS_Y - 1);
    ezUInt32 yMax = ezMath::Clamp((ezInt32)((1.0f - (float)screenSpaceBBox.m_Min.y()) * NUM_CLUSTERS_Y), 0, NUM_CLUSTERS_Y - 1);

    ezUInt32 zMin = GetSliceIndexFromDepth(screenSpaceBBox.m_Min.w());
    ezUInt32 zMax = GetSliceIndexFromDepth(screenSpaceBBox.m_Max.w());

    for (ezUInt32 z = zMin; z <= zMax; ++z)
    {
      for (ezUInt32 y = yMin; y <= yMax; ++y)
      {
        for (ezUInt32 x = xMin; x <= xMax; ++x)
        {
          ezUInt32 uiClusterIndex = GetClusterIndexFromCoord(x, y, z);
          clusters[uiClusterIndex].m_BitMask[uiBlockIndex] |= uiMask;
        }
      }
    }
  }

  template <typename Cluster, typename IntersectionFunc>
  EZ_ALWAYS_INLINE void FillCluster(const ezSimdVec4f& vMin, const ezSimdVec4f& vMax, ezUInt32 uiBlockIndex, ezUInt32 uiMask,
    Cluster* clusters, IntersectionFunc func)
  {
    ezSimdVec4f scale = ezSimdVec4f(0.5f * NUM_CLUSTERS_X, -0.5f * NUM_CLUSTERS_Y, 1.0f, 1.0f);
    ezSimdVec4f bias = ezSimdVec4f(0.5f * NUM_CLUSTERS_X, 0.5f * NUM_CLUSTERS_Y, 0.0f, 0.0f);

    ezSimdVec4f mi = ezSimdVec4f::MulAdd(vMin, scale, bias);
    ezSimdVec4f ma = ezSimdVec4f::MulAdd(vMax, scale, bias);

    ezSimdVec4i minXY_maxXY = ezSimdVec4i::Truncate(mi.GetCombined<ezSwizzle::XYXY>(ma));

    ezSimdVec4i maxClusterIndex = ezSimdVec4i(NUM_CLUSTERS_X, NUM_CLUSTERS_Y, NUM_CLUSTERS_X, NUM_CLUSTERS_Y);
    minXY_maxXY = minXY_maxXY.CompMin(maxClusterIndex - ezSimdVec4i(1));
    minXY_maxXY = minXY_maxXY.CompMax(ezSimdVec4i::ZeroVector());

    ezUInt32 xMin = minXY_maxXY.x();
    ezUInt32 yMin = minXY_maxXY.w();

    ezUInt32 xMax = minXY_maxXY.z();
    ezUInt32 yMax = minXY_maxXY.y();

    ezUInt32 zMin = GetSliceIndexFromDepth(vMin.z());
    ezUInt32 zMax = GetSliceIndexFromDepth(vMax.z());

    for (ezUInt32 z = zMin; z <= zMax; ++z)
    {
      for (ezUInt32 y = yMin; y <= yMax; ++y)
      {
        for (ezUInt32 x = xMin; x <= xMax; ++x)
        {
          ezUInt32 uiClusterIndex = GetClusterIndexFromCoord(x, y, z);
          if (func(uiClusterIndex))
          {
            clusters[uiClusterIndex].m_BitMask[uiBlockIndex] |= uiMask;
          }
        }
      }
    }
  }

  template <typename Cluster>
  void RasterizePointLight(const ezSimdBSphere& pointLightSphere, ezUInt32 uiLightIndex, const ezCamera& camera,
    Cluster* clusters, ezSimdBSphere* clusterBoundingSpheres)
  {
    ezSimdVec4f dirToLight = pointLightSphere.m_CenterAndRadius - ezSimdConversion::ToVec3(camera.GetCenterPosition());
    ezSimdFloat fDistanceToCenter = dirToLight.Dot<3>(ezSimdConversion::ToVec3(camera.GetCenterDirForwards()));

    ezSimdFloat fRadius = pointLightSphere.GetRadius();
    ezSimdFloat fZMin = fDistanceToCenter - fRadius;
    ezSimdFloat fZMax = fDistanceToCenter + fRadius;

    ///\todo Can be optimized by finding proper xy bounds. Needs to work even if the camera is inside the bounding sphere.
    ezSimdVec4f vMin(-1, -1, fZMin);
    ezSimdVec4f vMax(1, 1, fZMax);

    const ezUInt32 uiBlockIndex = uiLightIndex / 32;
    const ezUInt32 uiMask = 1 << (uiLightIndex - uiBlockIndex * 32);

    FillCluster(vMin, vMax, uiBlockIndex, uiMask, clusters, [&](ezUInt32 uiClusterIndex)
    {
      return pointLightSphere.Overlaps(clusterBoundingSpheres[uiClusterIndex]);
    });
  }

  struct BoundingCone
  {
    ezSimdBSphere m_BoundingSphere;
    ezSimdVec4f m_PositionAndRange;
    ezSimdVec4f m_ForwardDir;
    ezSimdVec4f m_SinCosAngle;
  };

  template <typename Cluster>
  void RasterizeSpotLight(const BoundingCone& spotLightCone, ezUInt32 uiLightIndex, const ezCamera& camera,
    Cluster* clusters, ezSimdBSphere* clusterBoundingSpheres)
  {
    ezSimdVec4f position = spotLightCone.m_PositionAndRange;
    ezSimdFloat range = spotLightCone.m_PositionAndRange.w();
    ezSimdVec4f forwardDir = spotLightCone.m_ForwardDir;
    ezSimdFloat sinAngle = spotLightCone.m_SinCosAngle.x();
    ezSimdFloat cosAngle = spotLightCone.m_SinCosAngle.y();

    // First calculate a bounding sphere around the cone to get min and max bounds
    ezSimdVec4f bSphereCenter;
    ezSimdFloat bSphereRadius;
    if (sinAngle > 0.707107f) // sin(45)
    {
      bSphereCenter = position + forwardDir * cosAngle * range;
      bSphereRadius = sinAngle * range;
    }
    else
    {
      bSphereRadius = range / (cosAngle + cosAngle);
      bSphereCenter = position + forwardDir * bSphereRadius;
    }

    ezSimdVec4f dirToLight = bSphereCenter - ezSimdConversion::ToVec3(camera.GetCenterPosition());
    ezSimdFloat fDistanceToCenter = dirToLight.Dot<3>(ezSimdConversion::ToVec3(camera.GetCenterDirForwards()));

    ezSimdFloat fZMin = fDistanceToCenter - bSphereRadius;
    ezSimdFloat fZMax = fDistanceToCenter + bSphereRadius;

    ///\todo Can be optimized by finding proper xy bounds. Needs to work even if the camera is inside the bounding sphere.
    ezSimdVec4f vMin(-1, -1, fZMin);
    ezSimdVec4f vMax(1, 1, fZMax);

    const ezUInt32 uiBlockIndex = uiLightIndex / 32;
    const ezUInt32 uiMask = 1 << (uiLightIndex - uiBlockIndex * 32);

    FillCluster(vMin, vMax, uiBlockIndex, uiMask, clusters, [&](ezUInt32 uiClusterIndex)
    {
      ezSimdBSphere clusterSphere = clusterBoundingSpheres[uiClusterIndex];
      ezSimdFloat clusterRadius = clusterSphere.GetRadius();

      ezSimdVec4f toConePos = clusterSphere.m_CenterAndRadius - position;
      ezSimdFloat projected = forwardDir.Dot<3>(toConePos);
      ezSimdFloat distToConeSq = toConePos.Dot<3>(toConePos);
      ezSimdFloat distClosestP = cosAngle * (distToConeSq - projected * projected).GetSqrt() - projected * sinAngle;

      bool angleCull = distClosestP > clusterRadius;
      bool frontCull = projected > clusterRadius + range;
      bool backCull = projected < -clusterRadius;

      return !(angleCull || frontCull || backCull);
    });
  }

  template <typename Cluster>
  void RasterizeDirLight(const ezDirectionalLightRenderData* pDirLightRenderData, ezUInt32 uiLightIndex, ezArrayPtr<Cluster> clusters)
  {
    const ezUInt32 uiBlockIndex = uiLightIndex / 32;
    const ezUInt32 uiMask = 1 << (uiLightIndex - uiBlockIndex * 32);

    for (ezUInt32 i = 0; i < clusters.GetCount(); ++i)
    {
      clusters[i].m_BitMask[uiBlockIndex] |= uiMask;
    }
  }
}
