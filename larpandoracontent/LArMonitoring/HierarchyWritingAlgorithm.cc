/**
 *  @file   larpandoracontent/LArMonitoring/HierarchyWritingAlgorithm.cc
 *
 *  @brief  Implementation of the particle visualisation algorithm.
 *
 *  $Log: $
 */

#include "Pandora/AlgorithmHeaders.h"

#include "larpandoracontent/LArMonitoring/HierarchyWritingAlgorithm.h"

using namespace pandora;

namespace lar_content
{

HierarchyWritingAlgorithm::HierarchyWritingAlgorithm() :
    m_writeTree{true},
    m_foldToPrimaries{true},
    m_foldToLeadingShowers{false},
    m_validateEvent{false},
    m_validateMC{false}
{
}

//------------------------------------------------------------------------------------------------------------------------------------------

HierarchyWritingAlgorithm::~HierarchyWritingAlgorithm()
{
    if (m_writeTree)
    {
        PANDORA_MONITORING_API(SaveTree(this->GetPandora(), m_treename.c_str(), m_filename.c_str(), "UPDATE"));
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode HierarchyWritingAlgorithm::Run()
{
    std::cout<<"HitListName " <<m_caloHitListName <<std::endl ;
    const CaloHitList *pCaloHitList(nullptr);
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetList(*this, m_caloHitListName, pCaloHitList));
    const MCParticleList *pMCParticleList(nullptr);
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetCurrentList(*this, pMCParticleList));
    const PfoList *pPfoList(nullptr);
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetList(*this, m_pfoListName, pPfoList));

    LArHierarchyHelper::MCHierarchy mcHierarchy;
    LArHierarchyHelper::FillMCHierarchy(*pMCParticleList, *pCaloHitList, m_foldToPrimaries, m_foldToLeadingShowers, mcHierarchy);
    LArHierarchyHelper::RecoHierarchy recoHierarchy;
    LArHierarchyHelper::FillRecoHierarchy(*pPfoList, m_foldToPrimaries, m_foldToLeadingShowers, recoHierarchy);
    LArHierarchyHelper::MatchInfo matchInfo;
    LArHierarchyHelper::MatchHierarchies(mcHierarchy, recoHierarchy, matchInfo);
    matchInfo.Print(mcHierarchy);
    
    this->ProduceOutputs(matchInfo);



    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

void HierarchyWritingAlgorithm::ProduceOutputs(const LArHierarchyHelper::MatchInfo &matchInfo) const
{
    if (m_writeTree)
    {
    
        const LArHierarchyHelper::MCMatchesVector goodMatchesVector{matchInfo.GetGoodMatches()};
        const LArHierarchyHelper::MCMatchesVector aboveThresholdVector{matchInfo.GetAboveThresholdMatches()};
        const LArHierarchyHelper::MCMatchesVector subThresholdVector{matchInfo.GetSubThresholdMatches()};


        LArHierarchyHelper::MCMatchesVector allMatches;
        for (const LArHierarchyHelper::MCMatches match : goodMatchesVector)
             allMatches.emplace_back(match);
        for (const LArHierarchyHelper::MCMatches match : aboveThresholdVector)
             allMatches.emplace_back(match);
        for (const LArHierarchyHelper::MCMatches match : subThresholdVector)
            allMatches.emplace_back(match);

        for (const LArHierarchyHelper::MCMatches match : allMatches)
        { 
                      
             FloatVector x,z,energy;

             for (const LArHierarchyHelper::MCHierarchy::Node *pNode : match.GetMC())
             {
                 int mcid{pNode->GetParticleId()};
                 if (mcid != abs(211)){
                    continue; }
              }
                     
             
             for (const LArHierarchyHelper::RecoHierarchy::Node *pNode : match.GetRecoMatches())
             {
                  int pid{pNode->GetParticleId()};
                  std::cout << "Particle ID = " << pid << std::endl;

                  const CaloHitList &caloHits(pNode->GetCaloHits());
                
                  for (const CaloHit *pCaloHit : caloHits)
                  { 
                       x.emplace_back(pCaloHit->GetPositionVector().GetX());
                       z.emplace_back(pCaloHit->GetPositionVector().GetZ());
                       energy.emplace_back(pCaloHit->GetInputEnergy());
                    }           
                            PANDORA_MONITORING_API(SetTreeVariable(this->GetPandora(), m_treename.c_str(), "x", &x));
                            PANDORA_MONITORING_API(SetTreeVariable(this->GetPandora(), m_treename.c_str(), "z", &z));
                            PANDORA_MONITORING_API(SetTreeVariable(this->GetPandora(), m_treename.c_str(), "energy", &energy));
                            PANDORA_MONITORING_API(FillTree(this->GetPandora(), m_treename.c_str()));    
               }

                    
            
          }                   
         
      }
  }
  
//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode HierarchyWritingAlgorithm::ReadSettings(const TiXmlHandle xmlHandle)
{
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle, "CaloHitListName", m_caloHitListName));
    if (m_caloHitListName.empty())
        m_caloHitListName = "CaloHitList2D";
    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle, "PfoListName", m_pfoListName));
    if (m_pfoListName.empty())
        m_pfoListName = "RecreatedPfos";


    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle, "WriteTree", m_writeTree));
    if (m_writeTree)
    {
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ReadValue(xmlHandle, "FileName", m_filename));
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, XmlHelper::ReadValue(xmlHandle, "TreeName", m_treename));
    }

    PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle, "FoldToPrimaries", m_foldToPrimaries));
    PANDORA_RETURN_RESULT_IF_AND_IF(
        STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle, "FoldToLeadingShowers", m_foldToLeadingShowers));

    return STATUS_CODE_SUCCESS;
}

} // namespace lar_content
