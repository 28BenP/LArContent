/**
 *  @file   larpandoracontent/LArTwoDReco/LArCosmicRay/CosmicRayTrackRefinementBaseAlgorithm.cc
 *
 *  @brief  Implementation of the cosmic ray track refinement base class.
 *
 *  $Log: $
 */
#include "Pandora/AlgorithmHeaders.h"

#include "larpandoracontent/LArTwoDReco/LArCosmicRay/CosmicRayTrackRefinementBaseAlgorithm.h"


#include "larpandoracontent/LArHelpers/LArGeometryHelper.h"
#include "larpandoracontent/LArHelpers/LArClusterHelper.h"

using namespace pandora;

namespace lar_content
{

template<typename T>    
CosmicRayTrackRefinementBaseAlgorithm<T>::CosmicRayTrackRefinementBaseAlgorithm() :
    m_minCaloHits(50),
    m_maxCurviness(0.3),
    m_microSlidingFitWindow(20), // was developed with 10
    m_macroSlidingFitWindow(1000),
    m_stableRegionClusterFraction(0.05),
    m_mergePointMinCosAngleDeviation(0.999), // was developed with 0.995
    m_distanceFromLine(0.35f),
    m_minHitFractionForHitRemoval(0.05f),
    m_maxDistanceFromMainTrack(0.75f),
    m_maxHitDistanceFromCluster(4.f),
    m_maxHitSeparationForConnectedCluster(4.f),
    m_maxTrackGaps(3),
    m_lineSegmentLength(3.f)    
{
}

//------------------------------------------------------------------------------------------------------------------------------------------    
    
template<typename T>
StatusCode CosmicRayTrackRefinementBaseAlgorithm<T>::Run()
{
    PandoraMonitoringApi::SetEveDisplayParameters(this->GetPandora(), true, DETECTOR_VIEW_DEFAULT, -1.f, 1.f, 1.f);
    //std::cout << "IF TRACK IN EM SHOWER REMEMBER YOU CHANGED THE DIRECTION!" << std::endl;
    
    const ClusterList *pClusterList(nullptr);
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentList(*this, pClusterList));

    const CaloHitList *pCaloHitList(nullptr);
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentList(*this, pCaloHitList));

    ClusterVector clusterVector;
    TwoDSlidingFitResultMap microSlidingFitResultMap, macroSlidingFitResultMap;
    SlidingFitResultMapPair slidingFitResultMapPair({&microSlidingFitResultMap, &macroSlidingFitResultMap});
    
    //std::cout << "\033[31m" <<"Initialise clusters..." << "\033[0m" <<std::endl;
    this->InitialiseContainers(pClusterList, clusterVector, slidingFitResultMapPair);

    unsigned int loopIterations(0);
    while(loopIterations < 10)
    {
        ++loopIterations;

        std::cout << "\033[31m" <<"Finding best association..." << "\033[0m" <<std::endl;
        
        ClusterAssociationVector clusterAssociationVector;
        this->FindBestClusterAssociation(clusterVector, slidingFitResultMapPair, clusterAssociationVector);
        
        if (clusterAssociationVector.empty())
        {
            std::cout << "\033[31m" << "Couldn't find an association" << "\033[0m" << std::endl;
            break;
        }

        // ATTN: Considering clusterAssociation so remove from 'to consider' clusters i.e. the clusterVector
        this->RemoveClusterAssociationFromClusterVector(clusterAssociationVector.front(), clusterVector);

        std::cout << "\033[31m" << "Refining endpoint... " << clusterAssociationVector.size() << " association(s)" << "\033[0m" <<std::endl;
        
        for (T &clusterAssociation : clusterAssociationVector)
        {
            ClusterToCaloHitListMap clusterToCaloHitListMap;
            this->GetExtrapolatedCaloHits(clusterAssociation, pClusterList, clusterToCaloHitListMap);

            /////////////////////
            
            std::cout << "after hits collected" << std::endl;
            CaloHitVector extrapolatedCaloHitVector;
            for (const auto &entry : clusterToCaloHitListMap)
                extrapolatedCaloHitVector.insert(extrapolatedCaloHitVector.begin(), entry.second.begin(), entry.second.end());
            
            for (auto &entry : extrapolatedCaloHitVector)
            {
                const CartesianVector &hitPosition(entry->GetPositionVector());
                PandoraMonitoringApi::AddMarkerToVisualization(this->GetPandora(), &hitPosition, "EXTRAPOLATED HIT", GREEN, 2);
            }
            
            //////////////////////////////

            std::cout << "BEFORE IS NEAR BOUNDARY" << std::endl;

            if (!this->IsExtrapolatedEndpointNearBoundary(clusterAssociation, 5.f))
            {
                std::cout << "ENDPOINT NOT NEAR THE BOUNDARY" << std::endl;                
                PandoraMonitoringApi::ViewEvent(this->GetPandora());
                continue;
            }

            //std::cout << "before is continuous" << std::endl;

            if (!this->IsTrackContinuous(clusterAssociation, clusterToCaloHitListMap, m_maxTrackGaps, m_lineSegmentLength))
            {
                std::cout << "GAP IN HIT VECTOR" << std::endl;
                PandoraMonitoringApi::ViewEvent(this->GetPandora());
                continue;
            }

            PandoraMonitoringApi::ViewEvent(this->GetPandora());

            std::cout << "before create main track" << std::endl;

            this->CreateMainTrack(clusterAssociation, clusterToCaloHitListMap, pClusterList, clusterVector, slidingFitResultMapPair, clusterAssociationVector);
        }
    }

    return STATUS_CODE_SUCCESS;
}

    

//------------------------------------------------------------------------------------------------------------------------------------------     
template<typename T>       
bool CosmicRayTrackRefinementBaseAlgorithm<T>::GetClusterMergingCoordinates(const TwoDSlidingFitResult &clusterMicroFitResult, const TwoDSlidingFitResult &clusterMacroFitResult,
    const TwoDSlidingFitResult &associatedMacroFitResult, const bool isUpstream, CartesianVector &clusterMergePosition, CartesianVector &clusterMergeDirection) const
{
    CartesianVector clusterAverageDirection(0.f, 0.f, 0.f), associatedAverageDirection(0.f, 0.f, 0.f);
    clusterMacroFitResult.GetGlobalDirection(clusterMacroFitResult.GetLayerFitResultMap().begin()->second.GetGradient(), clusterAverageDirection);
    associatedMacroFitResult.GetGlobalDirection(associatedMacroFitResult.GetLayerFitResultMap().begin()->second.GetGradient(), associatedAverageDirection);

    const LayerFitResultMap &clusterMicroLayerFitResultMap(clusterMicroFitResult.GetLayerFitResultMap());
    const int startLayer(isUpstream ? clusterMicroFitResult.GetMaxLayer() : clusterMicroFitResult.GetMinLayer());
    const int endLayer(isUpstream ? clusterMicroFitResult.GetMinLayer() : clusterMicroFitResult.GetMaxLayer());
    const int loopTerminationLayer(endLayer + (isUpstream ? -1 : 1));
    const int step(isUpstream ? -1 : 1);

    // ATTN: Search for stable region for which the local layer gradient agrees well with associated cluster global gradient
    unsigned int gradientStabilityWindow(std::ceil(clusterMicroLayerFitResultMap.size() *  m_stableRegionClusterFraction));
    unsigned int goodLayerCount(0);

    for (int i = startLayer; i != loopTerminationLayer; i += step)
    {
        const auto microIter(clusterMicroLayerFitResultMap.find(i));

        if (microIter == clusterMicroLayerFitResultMap.end())
            continue;

        CartesianVector microDirection(0.f, 0.f, 0.f);
        clusterMicroFitResult.GetGlobalDirection(microIter->second.GetGradient(), microDirection);

        const float cosDirectionOpeningAngle(microDirection.GetCosOpeningAngle(associatedAverageDirection));
        if (cosDirectionOpeningAngle > m_mergePointMinCosAngleDeviation)
        {
            // ATTN: Set merge point and direction as that at the first layer in the stable region
            if (goodLayerCount == 0)
            {
                // ATTN: Cluster direction vectors must point to one another
                //clusterMergeDirection = clusterAverageDirection * (isUpstream ? 1.f : -1.f);
                clusterMergeDirection = clusterAverageDirection;
                clusterMicroFitResult.GetGlobalFitPosition(microIter->second.GetL(), clusterMergePosition);
            }

            ++goodLayerCount;
        }
        else
        {
            goodLayerCount = 0;
        }

        if (goodLayerCount > gradientStabilityWindow)
            break;

        // ATTN: Abort merging process have not found a stable region
        if (i == endLayer)
            return false;
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------------------------
/*
template<typename T>    
void CosmicRayTrackRefinementBaseAlgorithm<T>::GetExtrapolatedCaloHits(const ClusterAssociation &clusterAssociation, const ClusterList *const pClusterList, ClusterToCaloHitListMap &clusterToCaloHitListMap) const
{
    const CartesianVector &upstreamPoint(clusterAssociation.GetUpstreamMergePoint()), &downstreamPoint(clusterAssociation.GetDownstreamMergePoint());
    const float minX(std::min(upstreamPoint.GetX(), downstreamPoint.GetX())), maxX(std::max(upstreamPoint.GetX(), downstreamPoint.GetX()));
        
    for (const Cluster *const pCluster : *pClusterList)
    {
        const OrderedCaloHitList &orderedCaloHitList(pCluster->GetOrderedCaloHitList());
        for (const OrderedCaloHitList::value_type &mapEntry : orderedCaloHitList)
        {
            for (const CaloHit *const pCaloHit : *mapEntry.second)
            {
                const CartesianVector &hitPosition(pCaloHit->GetPositionVector());
                if ((hitPosition.GetX() < minX) || (hitPosition.GetX() > maxX) || (hitPosition.GetZ() < upstreamPoint.GetZ()) || (hitPosition.GetZ() > downstreamPoint.GetZ()))
                    continue;

                const float distanceFromLine(clusterAssociation.GetConnectingLineDirection().GetCrossProduct(hitPosition - upstreamPoint).GetMagnitude());
                if (distanceFromLine > m_distanceFromLine)
                    continue;

                clusterToCaloHitListMap[pCluster].push_back(pCaloHit);
            }
        }
    }
}
*/
//------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>    
bool CosmicRayTrackRefinementBaseAlgorithm<T>::IsTrackContinuous(const ClusterAssociation &clusterAssociation, const ClusterToCaloHitListMap &clusterToCaloHitListMap,
    const unsigned int maxTrackGaps, const float lineSegmentLength) const
{
    // ATTN: Collect extrapolated calo hits and sort by projected distance from the upstreamMergePoint
    CaloHitVector extrapolatedCaloHitVector;
    for (const auto &entry : clusterToCaloHitListMap)
        extrapolatedCaloHitVector.insert(extrapolatedCaloHitVector.begin(), entry.second.begin(), entry.second.end()); 

    std::sort(extrapolatedCaloHitVector.begin(), extrapolatedCaloHitVector.end(), SortByDistanceAlongLine(clusterAssociation.GetUpstreamMergePoint(),
        clusterAssociation.GetConnectingLineDirection()));

    CartesianPointVector trackSegmentBoundaries;
    this->GetTrackSegmentBoundaries(clusterAssociation, trackSegmentBoundaries, lineSegmentLength);

    if (trackSegmentBoundaries.size() < 2)
    {
        std::cout << "TrackInEMShowerAlgorithm: Less than two track segment boundaries" << std::endl;
        throw STATUS_CODE_NOT_ALLOWED;
    }

    unsigned int segmentsWithoutHits(0);
    CaloHitVector::const_iterator caloHitIter(extrapolatedCaloHitVector.begin());
        
    for (unsigned int i = 0; i < (trackSegmentBoundaries.size() - 1); ++i)
    {
        if (caloHitIter == extrapolatedCaloHitVector.end())
        {
            ++segmentsWithoutHits;

            if (segmentsWithoutHits > maxTrackGaps)
                return false;

            continue;
        }

        unsigned int hitsInSegment(0);
        while (this->IsInLineSegment(trackSegmentBoundaries.at(i), trackSegmentBoundaries.at(i + 1), (*caloHitIter)->GetPositionVector()))
        {
            ++hitsInSegment;
            ++caloHitIter;

            if (caloHitIter == extrapolatedCaloHitVector.end())
                break;
        }

        segmentsWithoutHits = hitsInSegment ? 0 : segmentsWithoutHits + 1;

        if (segmentsWithoutHits > maxTrackGaps)
            return false;
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------------------------

template<typename T>    
void CosmicRayTrackRefinementBaseAlgorithm<T>::GetTrackSegmentBoundaries(const ClusterAssociation &clusterAssociation, CartesianPointVector &trackSegmentBoundaries,
    const float lineSegmentLength) const
{
    if (lineSegmentLength < std::numeric_limits<float>::epsilon())
    {
        std::cout << "TrackInEMShowerAlgorithm: Line segment length must be positive and nonzero" << std::endl;
        throw STATUS_CODE_INVALID_PARAMETER;
    }

    // ATTN: To handle final segment merge track remainder with preceding segment and if track remainder was more than half of the segment length split into two
    const CartesianVector &trackDirection(clusterAssociation.GetConnectingLineDirection());
    const float trackLength((clusterAssociation.GetDownstreamMergePoint() - clusterAssociation.GetUpstreamMergePoint()).GetMagnitude());
    const int fullSegments(std::floor(trackLength / lineSegmentLength));

    if (fullSegments == 0)
        trackSegmentBoundaries = {clusterAssociation.GetUpstreamMergePoint(), clusterAssociation.GetDownstreamMergePoint()};
    
    const float lengthOfTrackRemainder(trackLength - (fullSegments * lineSegmentLength));
    const bool splitFinalSegment(lengthOfTrackRemainder > lineSegmentLength * 0.5f);
    const int numberOfBoundaries(fullSegments + (splitFinalSegment ? 2 : 1));

    for (int i = 0; i < numberOfBoundaries; ++i)
    {
        if (i == 0)
        {
            trackSegmentBoundaries.push_back(clusterAssociation.GetUpstreamMergePoint());
        }
        else if (i < fullSegments)
        {
            trackSegmentBoundaries.push_back(trackSegmentBoundaries.back() + (trackDirection * lineSegmentLength));
        }
        else
        {
            if (splitFinalSegment)
            {
                trackSegmentBoundaries.push_back(trackSegmentBoundaries.back() + (trackDirection * (lineSegmentLength + lengthOfTrackRemainder) * 0.5f));
            }
            else
            {
                trackSegmentBoundaries.push_back(trackSegmentBoundaries.back() + (trackDirection * (lineSegmentLength + lengthOfTrackRemainder)));
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

template<typename T>    
bool CosmicRayTrackRefinementBaseAlgorithm<T>::IsInLineSegment(const CartesianVector &lowerBoundary, const CartesianVector &upperBoundary, const CartesianVector &point) const
{
    const float segmentBoundaryGradient = (-1.f) * (upperBoundary.GetX() - lowerBoundary.GetX()) / (upperBoundary.GetZ() - lowerBoundary.GetZ());
    const float xPointOnUpperLine((point.GetZ() - upperBoundary.GetZ()) / segmentBoundaryGradient + upperBoundary.GetX());
    const float xPointOnLowerLine((point.GetZ() - lowerBoundary.GetZ()) / segmentBoundaryGradient + lowerBoundary.GetX());

    if ((point.GetX() > xPointOnUpperLine) && (point.GetX() > xPointOnLowerLine))
        return false;

    if ((point.GetX() < xPointOnUpperLine) && (point.GetX() < xPointOnLowerLine))
        return false;

    return true;
}

//------------------------------------------------------------------------------------------------------------------------------------------

template<typename T>    
const Cluster *CosmicRayTrackRefinementBaseAlgorithm<T>::RemoveOffAxisHitsFromTrack(const Cluster *const pCluster, const CartesianVector &splitPosition,
    const bool isUpstreamEnd, const ClusterToCaloHitListMap &clusterToCaloHitListMap, ClusterList &remnantClusterList, TwoDSlidingFitResultMap &microSlidingFitResultMap,
    TwoDSlidingFitResultMap &macroSlidingFitResultMap) const
{
    float rL(0.f), rT(0.f);
    const TwoDSlidingFitResult &microFitResult(microSlidingFitResultMap.at(pCluster));
    microFitResult.GetLocalPosition(splitPosition, rL, rT);

    const TwoDSlidingFitResult &macroFitResult(macroSlidingFitResultMap.at(pCluster));
    CartesianVector averageDirection(0.f, 0.f, 0.f);
    macroFitResult.GetGlobalDirection(macroFitResult.GetLayerFitResultMap().begin()->second.GetGradient(), averageDirection);

    const bool isVertical(std::fabs(averageDirection.GetX()) < std::numeric_limits<float>::epsilon());
    const float clusterGradient(isVertical ? 0.f : averageDirection.GetZ() / averageDirection.GetX());
    const float clusterIntercept(isVertical ? splitPosition.GetX() : splitPosition.GetZ() - (clusterGradient * splitPosition.GetX()));

    // Fragmentation initialisation
    std::string originalListName, fragmentListName;
    const ClusterList originalClusterList(1, pCluster);
    PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::InitializeFragmentation(*this, originalClusterList, originalListName, fragmentListName));

    const Cluster *pMainTrackCluster(nullptr), *pAboveCluster(nullptr), *pBelowCluster(nullptr);

    const OrderedCaloHitList &orderedCaloHitList(pCluster->GetOrderedCaloHitList());
    for (const OrderedCaloHitList::value_type &mapEntry : orderedCaloHitList)
    {
        for (const CaloHit *const pCaloHit : *mapEntry.second)
        {
            const CartesianVector &hitPosition(pCaloHit->GetPositionVector());
            float thisL(0.f), thisT(0.f);

            microFitResult.GetLocalPosition(pCaloHit->GetPositionVector(), thisL, thisT);

            bool isAnExtrapolatedHit(false);
            const auto extrapolatedCaloHitIter(clusterToCaloHitListMap.find(pCluster));
            
            if (extrapolatedCaloHitIter != clusterToCaloHitListMap.end())
                isAnExtrapolatedHit = std::find(extrapolatedCaloHitIter->second.begin(), extrapolatedCaloHitIter->second.end(), pCaloHit) != extrapolatedCaloHitIter->second.end();
            
            const bool isAbove(((clusterGradient * hitPosition.GetX()) + clusterIntercept) < (isVertical ? hitPosition.GetX() : hitPosition.GetZ()));
            const bool isToRemove(!isAnExtrapolatedHit && (((thisL < rL) && isUpstreamEnd) || ((thisL > rL) && !isUpstreamEnd)));

            const Cluster *&pClusterToModify(isToRemove ? (isAbove ? pAboveCluster : pBelowCluster) : pMainTrackCluster);

            if (pClusterToModify)
            {
                PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::AddToCluster(*this, pClusterToModify, pCaloHit));
            }
            else
            {
                PandoraContentApi::Cluster::Parameters parameters;
                parameters.m_caloHitList.push_back(pCaloHit);
                PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::Cluster::Create(*this, parameters, pClusterToModify));

                if (pClusterToModify != pMainTrackCluster)
                    remnantClusterList.push_back(pClusterToModify);
            }
        }
    }

    // End fragmentation
    if (pAboveCluster || pBelowCluster)
    {
        PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::EndFragmentation(*this, fragmentListName, originalListName));
        return pMainTrackCluster;
    }
    else
    {
        PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::EndFragmentation(*this, originalListName, fragmentListName));
        return pCluster;
    }
}


//------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>    
void CosmicRayTrackRefinementBaseAlgorithm<T>::AddHitsToMainTrack(const Cluster *const pMainTrackCluster, const Cluster *const pShowerCluster, const CaloHitList &caloHitsToMerge,
    const ClusterAssociation &clusterAssociation, ClusterList &remnantClusterList) const
{
    // To ignore crossing CR muon or test beam tracks
    if ((static_cast<float>(caloHitsToMerge.size()) / static_cast<float>(pShowerCluster->GetNCaloHits())) < m_minHitFractionForHitRemoval)
        return;

    if (pShowerCluster->GetNCaloHits() == caloHitsToMerge.size())
    {
        PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::MergeAndDeleteClusters(*this, pMainTrackCluster, pShowerCluster));
        return;
    }

    // Fragmentation initialisation
    std::string originalListName, fragmentListName;
    const ClusterList originalClusterList(1, pShowerCluster);
    PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::InitializeFragmentation(*this, originalClusterList, originalListName, fragmentListName));

    const Cluster *pAboveCluster(nullptr), *pBelowCluster(nullptr);

    const bool isVertical(std::fabs(clusterAssociation.GetConnectingLineDirection().GetX()) < std::numeric_limits<float>::epsilon());
    const float connectingLineGradient(isVertical ? 0.f : clusterAssociation.GetConnectingLineDirection().GetZ() / clusterAssociation.GetConnectingLineDirection().GetX());
    const float connectingLineIntercept(isVertical ? clusterAssociation.GetUpstreamMergePoint().GetX() :
        clusterAssociation.GetUpstreamMergePoint().GetZ() - (connectingLineGradient * clusterAssociation.GetUpstreamMergePoint().GetX()));

    const OrderedCaloHitList orderedCaloHitList(pShowerCluster->GetOrderedCaloHitList());
    for (const OrderedCaloHitList::value_type &mapEntry : orderedCaloHitList)
    {
        for (const CaloHit *const pCaloHit : *mapEntry.second)
        {
            const bool isAnExtrapolatedHit(std::find(caloHitsToMerge.begin(), caloHitsToMerge.end(), pCaloHit) != caloHitsToMerge.end());
            if (isAnExtrapolatedHit)
            {
                PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::RemoveFromCluster(*this, pShowerCluster, pCaloHit));
                PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::AddToCluster(*this, pMainTrackCluster, pCaloHit));
            }
            else
            {
                const CartesianVector &hitPosition(pCaloHit->GetPositionVector());
                const bool isAbove(((connectingLineGradient * hitPosition.GetX()) + connectingLineIntercept) < (isVertical ? hitPosition.GetX() : hitPosition.GetZ()));
                const Cluster *&pClusterToModify(isAbove ? pAboveCluster : pBelowCluster);

                if (pClusterToModify)
                {
                    PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::AddToCluster(*this, pClusterToModify, pCaloHit));
                }
                else
                {
                    PandoraContentApi::Cluster::Parameters parameters;
                    parameters.m_caloHitList.push_back(pCaloHit);
                    PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::Cluster::Create(*this, parameters, pClusterToModify));

                    remnantClusterList.push_back(pClusterToModify);
                }
            }
        }
    }

    PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::EndFragmentation(*this, fragmentListName, originalListName));
}


//------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>    
void CosmicRayTrackRefinementBaseAlgorithm<T>::ProcessRemnantClusters(const ClusterList &remnantClusterList, const Cluster *const pMainTrackCluster, const ClusterList *const pClusterList, ClusterList &createdClusters) const
{
    ClusterList fragmentedClusters;
    for (const Cluster *const pRemnantCluster : remnantClusterList)
    {
        if (this->IsClusterRemnantDisconnected(pRemnantCluster)) //ISOBEL: USE CLUSTER HELPER FUNCTION INSTEAD.
        {
            this->FragmentRemnantCluster(pRemnantCluster, fragmentedClusters);
        }
        else
        {
            fragmentedClusters.push_back(pRemnantCluster);
        }
    }

    for (const Cluster *const pFragmentedCluster : fragmentedClusters)
    {
        if ((pFragmentedCluster->GetNCaloHits() == 1) && (this->AddToNearestCluster(pFragmentedCluster, pMainTrackCluster, pClusterList)))
            continue;
            
        createdClusters.push_back(pFragmentedCluster);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>    
bool CosmicRayTrackRefinementBaseAlgorithm<T>::AddToNearestCluster(const Cluster *const pClusterToMerge, const Cluster *const pMainTrackCluster, const ClusterList *const pClusterList) const
{
    const Cluster *pClosestCluster(nullptr);
    float closestDistance(std::numeric_limits<float>::max());

    for (const Cluster *const pCluster : *pClusterList)
    {
        if (pCluster == pClusterToMerge)
            continue;

        const float separationDistance(LArClusterHelper::GetClosestDistance(pClusterToMerge, pCluster));

        if (separationDistance < closestDistance)
        {
            if ((pCluster == pMainTrackCluster) && (separationDistance > m_maxDistanceFromMainTrack))
                continue;

            pClosestCluster = pCluster;
            closestDistance = separationDistance;
        }
    }

    if (closestDistance < m_maxHitDistanceFromCluster)
    {
        PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::MergeAndDeleteClusters(*this, pClosestCluster, pClusterToMerge));
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>    
bool CosmicRayTrackRefinementBaseAlgorithm<T>::IsClusterRemnantDisconnected(const Cluster *const pRemnantCluster) const
{
    if (pRemnantCluster->GetNCaloHits() == 1)
        return false;

    const OrderedCaloHitList &orderedCaloHitList(pRemnantCluster->GetOrderedCaloHitList());
    const CaloHit *pPreviousCaloHit(orderedCaloHitList.begin()->second->front());

    for (const OrderedCaloHitList::value_type &mapEntry : orderedCaloHitList)
    {
        for (const CaloHit *const pCaloHit : *mapEntry.second)
        {
            if (pCaloHit == pPreviousCaloHit)
                continue;

            const float separationDistanceSquared(pCaloHit->GetPositionVector().GetDistanceSquared(pPreviousCaloHit->GetPositionVector()));

            if (separationDistanceSquared > (m_maxHitSeparationForConnectedCluster * m_maxHitSeparationForConnectedCluster))
                return true;

            pPreviousCaloHit = pCaloHit;
        }
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>    
void CosmicRayTrackRefinementBaseAlgorithm<T>::FragmentRemnantCluster(const Cluster *const pRemnantCluster, ClusterList &fragmentedClusterList) const
{
    // Fragmentation initialisation
    std::string originalListName, fragmentListName;
    const ClusterList originalClusterList(1, pRemnantCluster);
    PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::InitializeFragmentation(*this, originalClusterList, originalListName, fragmentListName));

    ClusterList createdClusters;

    const OrderedCaloHitList &orderedCaloHitList(pRemnantCluster->GetOrderedCaloHitList());
    for (const OrderedCaloHitList::value_type &mapEntry : orderedCaloHitList)
    {
        for (const CaloHit *const pCaloHit : *mapEntry.second)
        {
            const Cluster *pClosestCluster(nullptr);

            if (!createdClusters.empty())
            {
                float closestDistance(std::numeric_limits<float>::max());

                for (const Cluster *const pCreatedCluster : createdClusters)
                {
                    const float separationDistance(LArClusterHelper::GetClosestDistance(pCaloHit->GetPositionVector(), pCreatedCluster));
                    if ((separationDistance < closestDistance) && (separationDistance < m_maxHitDistanceFromCluster))
                    {
                        closestDistance = separationDistance;
                        pClosestCluster = pCreatedCluster;
                    }
                }
            }

            if (pClosestCluster)
            {
                PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::AddToCluster(*this, pClosestCluster, pCaloHit));
            }
            else
            {
                PandoraContentApi::Cluster::Parameters parameters;
                parameters.m_caloHitList.push_back(pCaloHit);
                PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::Cluster::Create(*this, parameters, pClosestCluster));
                createdClusters.push_back(pClosestCluster);
            }
        }
    }

    // End fragmentation
    if (createdClusters.empty())
    {
        PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::EndFragmentation(*this, originalListName, fragmentListName));
        fragmentedClusterList.push_back(pRemnantCluster);
    }
    else
    {
        PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::EndFragmentation(*this, fragmentListName, originalListName));
        fragmentedClusterList.insert(fragmentedClusterList.begin(), createdClusters.begin(), createdClusters.end());
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>    
void CosmicRayTrackRefinementBaseAlgorithm<T>::InitialiseContainers(const ClusterList *pClusterList, ClusterVector &clusterVector, SlidingFitResultMapPair &slidingFitResultMapPair) const
{

    //unsigned int m_minCaloHits2(50);
    
    for (const Cluster *const pCluster : *pClusterList)
    {
        if (pCluster->GetNCaloHits() < m_minCaloHits)
            continue;
    
        const float slidingFitPitch(LArGeometryHelper::GetWireZPitch(this->GetPandora()));

         try
         {
             const TwoDSlidingFitResult microSlidingFitResult(pCluster, m_microSlidingFitWindow, slidingFitPitch);
             const TwoDSlidingFitResult macroSlidingFitResult(pCluster, m_macroSlidingFitWindow, slidingFitPitch);

             if ((pCluster->GetNCaloHits() < 300))
             {

                 CartesianVector clusterAverageDirection(0.f, 0.f, 0.f);
                 macroSlidingFitResult.GetGlobalDirection(macroSlidingFitResult.GetLayerFitResultMap().begin()->second.GetGradient(), clusterAverageDirection);

                 //ClusterList chosenClusters({pCluster});
                 //std::cout << "MIN: " << this->GetAverageDeviationFromLine(pCluster, clusterAverageDirection, macroSlidingFitResult.GetGlobalMinLayerPosition()) << std::endl;
                 //std::cout << "MAX: " << this->GetAverageDeviationFromLine(pCluster, clusterAverageDirection, macroSlidingFitResult.GetGlobalMaxLayerPosition()) << std::endl;
                 //PandoraMonitoringApi::VisualizeClusters(this->GetPandora(), &chosenClusters, "CHOSEN", VIOLET);
                 //PandoraMonitoringApi::ViewEvent(this->GetPandora());
                
                 if ((this->GetAverageDeviationFromLine(pCluster, clusterAverageDirection, macroSlidingFitResult.GetGlobalMinLayerPosition()) > m_maxCurviness) &&
                     (this->GetAverageDeviationFromLine(pCluster, clusterAverageDirection, macroSlidingFitResult.GetGlobalMaxLayerPosition()) > m_maxCurviness))
                 {
                     continue;
                 }
             }
                 
             slidingFitResultMapPair.first->insert(TwoDSlidingFitResultMap::value_type(pCluster, microSlidingFitResult));
             slidingFitResultMapPair.second->insert(TwoDSlidingFitResultMap::value_type(pCluster, macroSlidingFitResult));
             clusterVector.push_back(pCluster);
         }
         catch (const StatusCodeException &) {}
    }

    std::sort(clusterVector.begin(), clusterVector.end(), LArClusterHelper::SortByNHits);
    
    //////////
    /*
    ClusterList chosenClusters(clusterVector.begin(), clusterVector.end());
    PandoraMonitoringApi::VisualizeClusters(this->GetPandora(), &chosenClusters, "CHOSEN", VIOLET);
    PandoraMonitoringApi::ViewEvent(this->GetPandora());
    */
}

//------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>    
void CosmicRayTrackRefinementBaseAlgorithm<T>::UpdateContainers(const ClusterList &clustersToAdd, const ClusterList &clustersToDelete, ClusterVector &clusterVector, SlidingFitResultMapPair &slidingFitResultMapPair) const
{
    //ATTN: VERY IMPORTANT TO DELETE FROM THE CONTAINERS FIRST
    for (const Cluster *const pClusterToDelete : clustersToDelete)
        this->RemoveClusterFromContainers(pClusterToDelete, clusterVector, slidingFitResultMapPair);

    this->InitialiseContainers(&clustersToAdd, clusterVector, slidingFitResultMapPair);
}


//------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>    
void CosmicRayTrackRefinementBaseAlgorithm<T>::RemoveClusterFromContainers(const Cluster *const pClusterToRemove, ClusterVector &clusterVector, SlidingFitResultMapPair &slidingFitResultMapPair) const
{
    const TwoDSlidingFitResultMap::const_iterator microFitToDelete(slidingFitResultMapPair.first->find(pClusterToRemove));
    if (microFitToDelete != slidingFitResultMapPair.first->end())
        slidingFitResultMapPair.first->erase(microFitToDelete);

    const TwoDSlidingFitResultMap::const_iterator macroFitToDelete(slidingFitResultMapPair.second->find(pClusterToRemove));
    if (macroFitToDelete != slidingFitResultMapPair.second->end())
        slidingFitResultMapPair.second->erase(macroFitToDelete);        

    const ClusterVector::const_iterator clusterToDelete(std::find(clusterVector.begin(), clusterVector.end(), pClusterToRemove));
    if (clusterToDelete != clusterVector.end())
        clusterVector.erase(clusterToDelete);
}


//------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>   
float CosmicRayTrackRefinementBaseAlgorithm<T>::GetAverageDeviationFromLine(const Cluster *const pCluster, const CartesianVector &line, const CartesianVector &startPoint) const
{
    float distanceFromLine(0.f);

    const OrderedCaloHitList &orderedCaloHitList(pCluster->GetOrderedCaloHitList());
    for (const OrderedCaloHitList::value_type &mapEntry : orderedCaloHitList)
    {
        for (const CaloHit *const pCaloHit : *mapEntry.second)
         {
             const CartesianVector &hitPosition(pCaloHit->GetPositionVector());

             distanceFromLine += (line.GetCrossProduct(hitPosition - startPoint).GetMagnitude());
         }
    }

    return (distanceFromLine / pCluster->GetNCaloHits());
}

//------------------------------------------------------------------------------------------------------------------------------------------

template<typename T>    
StatusCode CosmicRayTrackRefinementBaseAlgorithm<T>::ReadSettings(const TiXmlHandle xmlHandle)
{

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinCaloHits", m_minCaloHits));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxCurviness", m_maxCurviness));    
    
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MicroSlidingFitWindow", m_microSlidingFitWindow));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MacroSlidingFitWindow", m_macroSlidingFitWindow));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "StableRegionClusterFraction", m_stableRegionClusterFraction));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MergePointMinCosAngleDeviation", m_mergePointMinCosAngleDeviation));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "DistanceFromLine", m_distanceFromLine));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MinHitFractionForHitRemoval", m_minHitFractionForHitRemoval));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxDistanceFromMainTrack", m_maxDistanceFromMainTrack));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxHitDistanceFromCluster", m_maxHitDistanceFromCluster));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxHitSeparationForConnectedCluster", m_maxHitSeparationForConnectedCluster));
    
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "MaxTrackGaps", m_maxTrackGaps));

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
        "LineSegmentLength", m_lineSegmentLength));

    if (m_lineSegmentLength < std::numeric_limits<float>::epsilon())
    {
        std::cout << "TrackInEMShowerAlgorithm: Line segment length must be positive and nonzero" << std::endl;
        throw STATUS_CODE_INVALID_PARAMETER;
    }    
    
    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

template<typename T>    
bool CosmicRayTrackRefinementBaseAlgorithm<T>::SortByDistanceAlongLine::operator() (const pandora::CaloHit *const pLhs, const pandora::CaloHit *const pRhs)
{
    const CartesianVector lhsDistanceVector(pLhs->GetPositionVector() - m_startPoint);
    const CartesianVector rhsDistanceVector(pRhs->GetPositionVector() - m_startPoint);

    const float lhsProjectedDistance(lhsDistanceVector.GetDotProduct(m_lineDirection));
    const float rhsProjectedDistance(rhsDistanceVector.GetDotProduct(m_lineDirection));

    if (std::fabs(lhsProjectedDistance - rhsProjectedDistance) > std::numeric_limits<float>::epsilon())
        return (lhsProjectedDistance < rhsProjectedDistance);

    // To deal with tiebreaks
    return LArClusterHelper::SortHitsByPulseHeight(pLhs, pRhs);
}


//------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------------------------

template class CosmicRayTrackRefinementBaseAlgorithm<ClusterEndpointAssociation>;

    

} // namespace lar_content
