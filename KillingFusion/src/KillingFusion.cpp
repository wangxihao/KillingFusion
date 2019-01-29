//
// Created by Saurabh Khanduja on 22.10.18.
//

#include "KillingFusion.h"
#include "SDF.h"
#include "Timer.h"
using namespace std;

KillingFusion::KillingFusion(DatasetReader datasetReader)
    : m_datasetReader(datasetReader),
      m_canonicalSdf(nullptr)
{
  // Create a canonical SDF
  int w = m_datasetReader.getDepthWidth();
  int h = m_datasetReader.getDepthHeight();
  float minDepth = m_datasetReader.getMinimumDepthThreshold();
  float maxDepth = m_datasetReader.getMaximumDepthThreshold();
  std::pair<Eigen::Vector3f, Eigen::Vector3f> frameBound = computeBounds(w, h, minDepth, maxDepth);
  m_canonicalSdf = new SDF(VoxelSize,
                           frameBound.first,
                           frameBound.second,
                           UnknownClipDistance);
  cout << m_canonicalSdf->getGridSize().transpose() << endl;
  m_startFrame = 5;
  m_endFrame = 100;
  m_stride = 1;
  m_currFrameIndex = m_startFrame;
  m_prev2CanDisplacementField = nullptr;
}

KillingFusion::~KillingFusion()
{
  // ToDo - Use Unique Ptr
  if (m_canonicalSdf != nullptr)
    delete m_canonicalSdf;
  if (m_prev2CanDisplacementField != nullptr)
    delete m_prev2CanDisplacementField;
}

void KillingFusion::process()
{
  // Set the sequence of image to process
  // int startFrame = 0;
  int startFrame = 4;
  int endFrame = 100;
  // int endFrame = m_datasetReader.getNumImageFiles();

  // Displacement Field for the previous and current frame.
  DisplacementField *prev2CanDisplacementField, *curr2CanDisplacementField;

  // Set prevSdf to SDF of first frame
  const SDF *prevSdf = computeSDF(startFrame);
  prev2CanDisplacementField = createZeroDisplacementField(*prevSdf);
  m_canonicalSdf->fuse(prevSdf);

  // For each file in DatasetReader
  Timer totalTimer, timer;
  cout << "Iter   Compute SDF    KillingOptimize    Fuse SDF\n";
  // For each image file from DatasetReader
  for (int i = startFrame + 1; i < endFrame; ++i)
  {
    // Convert current frame to SDF - currSdf
    timer.reset();
    SDF *currSdf = computeSDF(i);
    double sdfTime = timer.elapsed();

    // Future Task - Implement SDF-2-SDF to register currSDF to prevSDF
    // Future Task - ToDo - DisplacementField should have same shape as their SDF.
    curr2CanDisplacementField = prev2CanDisplacementField;

    // Compute Deformation Field for current frame SDF to merge with m_canonicalSdf
    timer.reset();
    computeDisplacementField(currSdf, m_canonicalSdf, curr2CanDisplacementField);
    double killingTime = timer.elapsed();

    // Merge the m_currSdf to m_canonicalSdf using m_currSdf displacement field.
    timer.reset();
    m_canonicalSdf->fuse(currSdf, curr2CanDisplacementField);
    double fuseTime = timer.elapsed();

    // Delete m_prevSdf and assign m_currSdf to m_prevSdf
    delete prevSdf;
    prevSdf = currSdf;
    prev2CanDisplacementField = curr2CanDisplacementField;
    printf("%03d\t%0.6fs\t%0.6fs\t%0.6fs\n", i, sdfTime, killingTime, fuseTime);
  }
  cout << "Total time spent " << totalTimer.elapsed() << endl;
  m_canonicalSdf->dumpToBinFile("output.bin",
                                UnknownClipDistance, 1.0f);
}

vector<SimpleMesh *> KillingFusion::processNextFrame()
{
  vector<SimpleMesh *> meshes;

  SimpleMesh *canonicalMesh = nullptr, *currentSdfMesh = nullptr, *currentFrameRegisteredSdfMesh = nullptr;

  if (m_currFrameIndex == m_startFrame)
  {
    m_canonicalSdf = computeSDF(m_startFrame);
    m_prev2CanDisplacementField = createZeroDisplacementField(*m_canonicalSdf);
    currentSdfMesh = m_canonicalSdf->getMesh();
    currentFrameRegisteredSdfMesh = m_canonicalSdf->getMesh(*m_prev2CanDisplacementField);
    m_currFrameIndex += m_stride;
  }
  else if (m_currFrameIndex < m_endFrame)
  {
    Timer totalTimer, timer;
    totalTimer.reset();
    cout << "Iter   Compute SDF    KillingOptimize    Fuse SDF    Total Time\n";
    timer.reset();
    // Convert current frame to SDF - currSdf
    SDF *currSdf = computeSDF(m_currFrameIndex);
    double sdfTime = timer.elapsed();

    // Future Task - Implement SDF-2-SDF to register currSDF to prevSDF
    // Future Task - ToDo - DisplacementField should have same shape as their SDF.
    DisplacementField *curr2CanDisplacementField;
    if (UseZeroDisplacementFieldForNextFrame)
    {
      delete m_prev2CanDisplacementField;
      curr2CanDisplacementField = createZeroDisplacementField(*currSdf);
    }
    else
      curr2CanDisplacementField = m_prev2CanDisplacementField;

    timer.reset();
    // Compute Deformation Field for current frame SDF to merge with m_canonicalSdf
    computeDisplacementField(currSdf, m_canonicalSdf, curr2CanDisplacementField);
    double killingTime = timer.elapsed();

    timer.reset();
    // Merge the m_currSdf to m_canonicalSdf using m_currSdf displacement field.
    currentSdfMesh = currSdf->getMesh();
    currSdf->update(curr2CanDisplacementField);
    m_canonicalSdf->fuse(currSdf);
    currentFrameRegisteredSdfMesh = currSdf->getMesh();
    double fuseTime = timer.elapsed();

    // ToDo - Save Live Canonical SDF registered towards CurrentFrame
    m_prev2CanDisplacementField = curr2CanDisplacementField;
    delete currSdf;
    double totalTime = totalTimer.elapsed();
    printf("%03d\t%0.6fs\t%0.6fs\t%0.6fs\t%0.6fs\n", m_currFrameIndex, sdfTime, killingTime, fuseTime, totalTime);
    m_currFrameIndex += m_stride;
  }
  canonicalMesh = m_canonicalSdf->getMesh();
  meshes.push_back(currentSdfMesh);
  meshes.push_back(currentFrameRegisteredSdfMesh);
  meshes.push_back(canonicalMesh);
  return meshes;
}

void KillingFusion::processTest(int testType)
{
  // Set prevSdf to SDF of first frame
  vector<SDF> adjacentVoxelSDFs = SDF::getDataEnergyTestSample(VoxelSize, UnknownClipDistance);
  DisplacementField *next2CanDisplacementField = createZeroDisplacementField(adjacentVoxelSDFs[1]);
  if (testType == 1)
  { // Test if fuse works when merging one SDF to itself
    adjacentVoxelSDFs[0].dumpToBinFile("testType-1-outputSphere0.bin", UnknownClipDistance, 1.0f);
    adjacentVoxelSDFs[1].dumpToBinFile("testType-1-outputSphere1.bin", UnknownClipDistance, 1.0f);
    adjacentVoxelSDFs[0].fuse(&(adjacentVoxelSDFs[0]));
    adjacentVoxelSDFs[0].dumpToBinFile("testType-1-outputSphere0MergedTo0.bin", UnknownClipDistance, 1.0f);
  }
  else if (testType == 2)
  { // Test if fuse works when merging one SDF to another
    adjacentVoxelSDFs[0].dumpToBinFile("testType-1-outputSphere0.bin", UnknownClipDistance, 1.0f);
    adjacentVoxelSDFs[1].dumpToBinFile("testType-1-outputSphere1.bin", UnknownClipDistance, 1.0f);
    adjacentVoxelSDFs[0].fuse(&(adjacentVoxelSDFs[1]));
    adjacentVoxelSDFs[0].dumpToBinFile("testType-1-outputSphere1MergedTo0.bin", UnknownClipDistance, 1.0f);
  }
  else
  {
    computeDisplacementField(&(adjacentVoxelSDFs[1]), &(adjacentVoxelSDFs[0]), next2CanDisplacementField);
    adjacentVoxelSDFs[0].fuse(&(adjacentVoxelSDFs[1]), next2CanDisplacementField);
    SDF srcCopy(adjacentVoxelSDFs[1]);
    srcCopy.fuse(&(adjacentVoxelSDFs[1]), next2CanDisplacementField);
    srcCopy.dumpToBinFile("testType-2-outputSphere1Deformed.bin", UnknownClipDistance, 1.0f);
    adjacentVoxelSDFs[0].dumpToBinFile("testType-2-outputSphere1MergedTo0UsingKilling.bin", UnknownClipDistance, 1.0f);
  }

  delete next2CanDisplacementField;
}

SDF *KillingFusion::computeSDF(int frameIndex)
{
  // ToDo: SDF class should compute itself
  // This will cause issue. SDF of Different Frames will be of different size.
  // This will cause deformation field to be of different size.
  // You then cannot simply set curr2PrevDisplacementField = prev2CanDisplacementField
  int w = m_datasetReader.getDepthWidth();
  int h = m_datasetReader.getDepthHeight();
  float minDepth = m_datasetReader.getMinimumDepthThreshold();
  float maxDepth = m_datasetReader.getMaximumDepthThreshold();
  std::pair<Eigen::Vector3f, Eigen::Vector3f> frameBound = computeBounds(w, h, minDepth, maxDepth);
  SDF *sdf = new SDF(VoxelSize,
                     frameBound.first,
                     frameBound.second,
                     UnknownClipDistance);
  std::vector<cv::Mat> cdoImages = m_datasetReader.getImages(frameIndex);
  sdf->integrateDepthFrame(cdoImages.at(1),
                           Eigen::Matrix4f::Identity(),
                           m_datasetReader.getDepthIntrinsicMatrix(),
                           minDepth,
                           maxDepth);
  return sdf;
}

DisplacementField *KillingFusion::createZeroDisplacementField(const SDF &sdf)
{
  return new DisplacementField(sdf.getGridSize(),
                               VoxelSize);
}

void KillingFusion::computeDisplacementField(const SDF *src,
                                             const SDF *dest,
                                             DisplacementField *srcToDest)
{
  // ToDo: Use Cuda.
  // Process at each voxel location
  Eigen::Vector3i srcGridSize = src->getGridSize();

  if (UpdateAllVoxelsInEachIter)
  {
    // Make one update for each voxel at a time.
    for (size_t iter = 0; iter < KILLING_MAX_ITERATIONS; iter++)
    {
      DisplacementField *currIterDeformation = nullptr;
      if (UsePreviousIterationDeformationField)
        currIterDeformation = createZeroDisplacementField(*src);
#ifndef DISABLE_OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
      for (int z = 0; z < srcGridSize(2); z++)
      {
        for (int y = 0; y < srcGridSize(1); y++)
        {
          for (int x = 0; x < srcGridSize(0); x++)
          {
            // Actual 3D Point on Desination Grid, where to optimize for.
            const Eigen::Vector3i spatialIndex(x, y, z);

            // Check if srcGridLocation is near the Surface.
            float srcSdfDistance = src->getDistance(spatialIndex, srcToDest);
            if (srcSdfDistance > MaxSurfaceVoxelDistance || srcSdfDistance < -MaxSurfaceVoxelDistance)
              continue;

#ifdef MY_DEBUG
            float origSrcSdfDistance = srcSdfDistance;
            cout << x << "," << y << ", " << z << endl;
            cout << "OrigDist|       Src Dist        |   Dest dist   |                  Delta Change              | New Displacement \n";
            const Eigen::IOFormat fmt(4, 0, "\t", " ", "", "", "", "");
            float destSdfDistance = dest->getDistanceAtIndex(spatialIndex);
#endif

            // Optimize All Energies between Source Grid and Desination Grid
            Eigen::Vector3f gradient = computeEnergyGradient(src, dest, srcToDest, spatialIndex);
            Eigen::Vector3f displacementUpdate = -alpha * gradient / VoxelSize;

            // Trust Region Strategy - Valid only when Data Energy is used.
            if (!EnergyTypeUsed[0] && !EnergyTypeUsed[1] && !EnergyTypeUsed[2])
            {
              float _alpha = alpha;
              bool lossDecreased = false;
              float destSdfDistance = dest->getDistanceAtIndex(spatialIndex);
              float prevSrcSdfDistance = src->getDistance(spatialIndex, srcToDest);
              do
              {
                srcSdfDistance = src->getDistance(spatialIndex.cast<float>() + srcToDest->getDisplacementAt(spatialIndex) + displacementUpdate + Eigen::Vector3f(0.5, 0.5, 0.5));
                float sdfDistanceConverged = fabs(srcSdfDistance - destSdfDistance) - fabs(prevSrcSdfDistance - destSdfDistance);
                if (sdfDistanceConverged > 0)
                {
                  _alpha /= 1.5;
#ifdef MY_DEBUG
                  cout << "Changed alpha to " << _alpha << endl;
#endif
                  displacementUpdate = -_alpha * gradient / VoxelSize;
                }
                else
                {
                  lossDecreased = true;
                }
              } while (!lossDecreased && _alpha > 1e-7);
              if (_alpha < 1e-7)
                break;
            }

            if (UsePreviousIterationDeformationField)
              currIterDeformation->update(spatialIndex, displacementUpdate);
            else
              srcToDest->update(spatialIndex, displacementUpdate);

#ifdef MY_DEBUG
            srcSdfDistance = src->getDistance(spatialIndex, srcToDest);
            cout << origSrcSdfDistance << "\t|\t" << srcSdfDistance << "\t|\t" << destSdfDistance << "\t|\t"
                 << displacementUpdate.transpose().format(fmt) << "\t|\t" << srcToDest->getDisplacementAt(spatialIndex).transpose().format(fmt) << "\n";
#endif

            // perform check on deformation field to see if it has diverged. Ideally shouldn't happen
            if (!srcToDest->getDisplacementAt(spatialIndex).array().isFinite().all())
            {
              std::cout << "Error: deformation field has diverged: " << srcToDest->getDisplacementAt(spatialIndex) << " at: " << spatialIndex << std::endl;
              throw - 1;
            }

#ifdef MY_DEBUG
            cout << "OrigDist|       Src Dist        |   Dest dist   |                  Delta Change              | New Displacement \n";
            cout << origSrcSdfDistance << "\t|\t" << srcSdfDistance << "\t|\t" << destSdfDistance << "\t|\t"
                 << displacementUpdate.transpose().format(fmt) << "\t|\t" << srcToDest->getDisplacementAt(spatialIndex).transpose().format(fmt) << "\n";
            cout << x << "," << y << ", " << z << endl;
            cout << endl;
            char c;
            cin >> c; // wait for user to read the inputs.
#endif
          }
        }
      }
      if (UsePreviousIterationDeformationField)
      {
        *srcToDest = *srcToDest + *currIterDeformation;
        delete currIterDeformation;
      }
    }
  }
  else
  {
#ifndef MY_DEBUG
#pragma omp parallel for schedule(dynamic)
#endif
    for (int z = 0; z < srcGridSize(2); z++)
    {
      for (int y = 0; y < srcGridSize(1); y++)
      {
        for (int x = 0; x < srcGridSize(0); x++)
        {
          // Actual 3D Point on Desination Grid, where to optimize for.
          const Eigen::Vector3i spatialIndex(x, y, z);

          // Check if srcGridLocation is near the Surface.
          float srcSdfDistance = src->getDistance(spatialIndex, srcToDest);
          if (srcSdfDistance > MaxSurfaceVoxelDistance || srcSdfDistance < -MaxSurfaceVoxelDistance)
            continue;

          // Optimize Killing Energy between Source Grid and Desination Grid
          Eigen::Vector3f gradient;
          int iter = 0;
          do
          {
            gradient = computeEnergyGradient(src, dest, srcToDest, spatialIndex);
            srcToDest->update(spatialIndex, -alpha * gradient / VoxelSize);

            if (gradient.norm() <= threshold)
              break;

            // perform check on deformation field to see if it has diverged. Ideally shouldn't happen
            if (!srcToDest->getDisplacementAt(spatialIndex).array().isFinite().all())
            {
              std::cout << "Error: deformation field has diverged: " << srcToDest->getDisplacementAt(spatialIndex) << " at: " << spatialIndex << std::endl;
              throw - 1;
            }

            iter += 1;
          } while (gradient.norm() > threshold && iter < KILLING_MAX_ITERATIONS);
        }
      }
    }
  }
}

Eigen::Vector3f KillingFusion::computeEnergyGradient(const SDF *src,
                                                     const SDF *dest,
                                                     const DisplacementField *srcDisplacementField,
                                                     const Eigen::Vector3i &spatialIndex)
{
  Eigen::Vector3f data_grad(0, 0, 0), levelset_grad(0, 0, 0), killing_grad(0, 0, 0);

  if (EnergyTypeUsed[0])
  {
    data_grad = computeDataEnergyGradient(src, dest, srcDisplacementField, spatialIndex);
    // if (data_grad.norm() > 1)
    //   data_grad.normalize();
  }
  if (EnergyTypeUsed[1])
  {
    levelset_grad = computeLevelSetEnergyGradient(src, srcDisplacementField, spatialIndex) * omegaLevelSet;
  }
  if (EnergyTypeUsed[2])
  {
    killing_grad = computeKillingEnergyGradient(srcDisplacementField, spatialIndex) * omegaKilling;
  }

  return data_grad + killing_grad + levelset_grad;
}

Eigen::Vector3f KillingFusion::computeDataEnergyGradient(const SDF *src,
                                                         const SDF *dest,
                                                         const DisplacementField *srcDisplacementField,
                                                         const Eigen::Vector3i &spatialIndex)
{
  Eigen::Vector3f srcPointDistanceGradient = src->computeDistanceGradient(spatialIndex, srcDisplacementField);
  if(srcPointDistanceGradient.norm() > 0)
    srcPointDistanceGradient.normalize(); // only direction is required
  float srcPointDistance = src->getDistance(spatialIndex, srcDisplacementField);
  float destPointDistance = dest->getDistanceAtIndex(spatialIndex);
  return (srcPointDistance - destPointDistance) * srcPointDistanceGradient.array();
}

Eigen::Vector3f KillingFusion::computeKillingEnergyGradient(const DisplacementField *srcDisplacementField,
                                                            const Eigen::Vector3i &spatialIndex)
{
  Eigen::Vector3f killingGrad = srcDisplacementField->computeKillingEnergyGradient(spatialIndex);
  return killingGrad;
}

Eigen::Vector3f KillingFusion::computeLevelSetEnergyGradient(const SDF *src,
                                                             const DisplacementField *srcDisplacementField,
                                                             const Eigen::Vector3i &spatialIndex)
{
  // Compute first distance gradient.and hessian
  Eigen::Vector3f grad = src->computeDistanceGradient(spatialIndex, srcDisplacementField);

  // Compute Hessian
  Eigen::Matrix3f hessian = src->computeDistanceHessian(spatialIndex, srcDisplacementField);

  return hessian * grad * (grad.norm() - 1) / (grad.norm() + epsilon);
}

std::pair<Eigen::Vector3f, Eigen::Vector3f> KillingFusion::computeBounds(int w, int h, float minDepth, float maxDepth)
{
  // Create frustum for the camera
  Eigen::MatrixXf cornerPoints;
  cornerPoints.resize(3, 8);
  cornerPoints << 0, 0, w - 1, w - 1, 0, 0, w - 1, w - 1,
      0, h - 1, h - 1, 0, 0, h - 1, h - 1, 0,
      1, 1, 1, 1, 1, 1, 1, 1;

  Eigen::Matrix<float, 1, 8> cornersDepth;
  cornersDepth << minDepth, minDepth, minDepth, minDepth,
      maxDepth, maxDepth, maxDepth, maxDepth;

  // Compute depthIntrinsicMatrix
  Eigen::Matrix3f depthIntrinsicMatrix = m_datasetReader.getDepthIntrinsicMatrix();
  Eigen::Matrix3f depthIntrinsicMatrixInv = depthIntrinsicMatrix.inverse();

  // Compute the corner location in the Camera Coordinate System(CCS)
  Eigen::MatrixXf imagePoints = depthIntrinsicMatrixInv * cornerPoints;

  // Compute the frustum in the Camera Coordinate System(CCS)
  imagePoints.conservativeResize(4, 8); // Resize from 3x8 to 4x8
  imagePoints.topLeftCorner(2, 4) =
      imagePoints.topLeftCorner(2, 4) * minDepth; // Multiply front four corners with minDepth
  imagePoints.topRightCorner(2, 4) =
      imagePoints.topRightCorner(2, 4) * maxDepth; // Multiply back four corners with maxDepth
  imagePoints.row(3) = imagePoints.row(2);         // Move ones below
  imagePoints.row(2) = cornersDepth.transpose();   // Replace 3rd row with min/max depth values

  // Compute world location of this frustum
  // Since world is same as camera, is Eigen::Matrix4f::Identity()
  Eigen::Matrix4f camera_to_world_pose = Eigen::Matrix4f::Identity();
  Eigen::Matrix<float, 4, 8> frustumWorldPoints = camera_to_world_pose * imagePoints;
  // Compute the bounds of parallelogram containing the frustum
  Eigen::Vector4f minXYZ = frustumWorldPoints.rowwise().minCoeff().array();
  Eigen::Vector4f maxXYZ = frustumWorldPoints.rowwise().maxCoeff().array();

  Eigen::Vector3f min3dLoc(minXYZ(0), minXYZ(1), minXYZ(2));
  Eigen::Vector3f max3dLoc(maxXYZ(0), maxXYZ(1), maxXYZ(2));

  return pair<Eigen::Vector3f, Eigen::Vector3f>(min3dLoc, max3dLoc);
}
