/**
 *  @file   LArContent/include/LArTwoDReco/LArSeedFinding/ClusterCharacterisationAlgorithm.h
 * 
 *  @brief  Header file for the cluster characterisation algorithm class.
 * 
 *  $Log: $
 */
#ifndef LAR_CLUSTER_CHARACTERISATION_ALGORITHM_H
#define LAR_CLUSTER_CHARACTERISATION_ALGORITHM_H 1

#include "Pandora/Algorithm.h"

namespace lar_content
{

/**
 *  @brief  ClusterCharacterisationAlgorithm class
 */
class ClusterCharacterisationAlgorithm : public pandora::Algorithm
{
public:
    /**
     *  @brief  Factory class for instantiating algorithm
     */
    class Factory : public pandora::AlgorithmFactory
    {
    public:
        pandora::Algorithm *CreateAlgorithm() const;
    };

    /**
     *  @brief  Default constructor
     */
    ClusterCharacterisationAlgorithm();

private:
    pandora::StatusCode Run();

    /**
     *  @brief  Whether cluster is identified as a clear track
     *
     *  @param  pCluster address of the relevant cluster
     * 
     *  @return boolean
     */
    bool IsClearTrack(const pandora::Cluster *const pCluster) const;

    pandora::StatusCode ReadSettings(const pandora::TiXmlHandle xmlHandle);

    pandora::StringVector   m_inputClusterListNames;        ///< The names of the input cluster lists
};

//------------------------------------------------------------------------------------------------------------------------------------------

inline pandora::Algorithm *ClusterCharacterisationAlgorithm::Factory::CreateAlgorithm() const
{
    return new ClusterCharacterisationAlgorithm();
}

} // namespace lar_content

#endif // #ifndef LAR_CLUSTER_CHARACTERISATION_ALGORITHM_H
