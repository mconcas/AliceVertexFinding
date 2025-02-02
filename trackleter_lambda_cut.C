#if !defined(__CLING__) || defined(__ROOTCLING__)

#include "iostream"
#include "TChain.h"
#include "TFile.h"
#include "TSystem.h"
#include <TNtuple.h>
#include <TObject.h>

#include "DataFormatsITSMFT/Cluster.h"
#include "DataFormatsITSMFT/ROFRecord.h"
#include "DataFormatsParameters/GRPObject.h"
#include "SimulationDataFormat/MCEventHeader.h"
#include "SimulationDataFormat/MCTruthContainer.h"

#include "ITSBase/GeometryTGeo.h"
#include "ITStracking/IOUtils.h"
#include "ITStracking/Vertexer.h"


using Vertex = o2::dataformats::Vertex<o2::dataformats::TimeStamp<int>>;

/* 
struct VertexingParameters {
  float zCut = 0.002f; //0.002f
  float phiCut = 0.005f; //0.005f
  float pairCut = 0.04f;
  float clusterCut = 0.8f;
  int clusterContributorsCut = 16;
  int phiSpan = -1;
  int zSpan = -1;
  float tanLambdaCut = 0.025;
};
*/

//the aim of this macro is to compute tracklets using a tanLambdaCut value and then writes the number of real trackets found, of fakes tracklets 
//and of the cut value in a file 



void trackleter_lambda_cut(  const int inspEvt = -1,
                             const int numEvents = 1,
                             const std::string inputClustersITS = "o2clus_its.root",
                             const std::string inputGRP = "o2sim_grp.root",
                             const std::string simfilename = "o2sim.root",
                             const std::string paramfilename = "O2geometry.root",
                             const std::string path = "./")
{

  std::string outfile = "lambda_cut_variation.root";
  
  const auto grp = o2::parameters::GRPObject::loadFrom(path + inputGRP);
  const bool isITS = grp->isDetReadOut(o2::detectors::DetID::ITS);
  const bool isContITS = grp->isDetContinuousReadOut(o2::detectors::DetID::ITS);
  std::cout << "ITS is in " << (isContITS ? "CONTINUOS" : "TRIGGERED") << " readout mode" << std::endl;
  TChain itsClusters("o2sim");
  itsClusters.AddFile((path + inputClustersITS).data());

  // Setup Runtime DB
  TFile paramFile((path + paramfilename).data());
  paramFile.Get("FAIRGeom");
  auto gman = o2::its::GeometryTGeo::Instance();
  gman->fillMatrixCache(o2::utils::bit2Mask(o2::TransformType::T2L, o2::TransformType::T2GRot,
                                            o2::TransformType::L2G)); // request cached transforms

  // Get event header
  TChain mcHeaderTree("o2sim");
  mcHeaderTree.AddFile((path + simfilename).data());
  o2::dataformats::MCEventHeader* mcHeader = nullptr;
  if (!mcHeaderTree.GetBranch("MCEventHeader.")) {
    LOG(FATAL) << "Did not find MC event header in the input header file." << FairLogger::endl;
  }
  mcHeaderTree.SetBranchAddress("MCEventHeader.", &mcHeader);

  // get clusters
  std::vector<o2::itsmft::Cluster>* clusters = nullptr;
  itsClusters.SetBranchAddress("ITSCluster", &clusters);

  TChain itsClustersROF("ITSClustersROF");
  itsClustersROF.AddFile((path + inputClustersITS).data());

  if (!itsClustersROF.GetBranch("ITSClustersROF")) {
    LOG(FATAL) << "Did not find ITS clusters branch ITSClustersROF in the input tree" << FairLogger::endl;
  }
  std::vector<o2::itsmft::ROFRecord>* rofs = nullptr;
  itsClustersROF.SetBranchAddress("ITSClustersROF", &rofs);
  itsClustersROF.GetEntry(0);

  //get labels
  o2::dataformats::MCTruthContainer<o2::MCCompLabel>* labels = nullptr;
  itsClusters.SetBranchAddress("ITSClusterMCTruth", &labels);



  TFile* outputfile = new TFile(outfile.data(), "update");
  
  TNtuple * results = nullptr;
  
  outputfile->GetObject("Results", results);


  if(results==nullptr){
    std::cout<<"No object to read in the file : it will be created \n";
    // here it is important to use dynamic allocation so the object does not disappear after the block
    results = new TNtuple("Results", "Results", "RecoMCvalid:FakeTracklets:TotTracklets:Cut");
  }

 if(results==nullptr){
    std::cout<<"Still no object \n";
    exit(0);
  }


  std::uint32_t roFrame = 0;
  const int stopAt = (inspEvt == -1) ? rofs->size() : inspEvt + numEvents;
  o2::its::ROframe frame(-123);
  o2::its::VertexerTraits* traits = nullptr;
  traits = o2::its::createVertexerTraits();
 
 
  o2::its::Vertexer vertexer(traits);
  int counter =0;

////////////////////// setting the cut value
  struct o2::its::VertexingParameters  par;
  par.tanLambdaCut=0.1; // do not use too high values for both
  par.phiCut=0.05;

  

  for (int iRof = (inspEvt == -1) ? 0 : inspEvt; iRof < stopAt; ++iRof) {

    int TGenerated=0;
    int FakeTracklets=0;
    int RecoMCvalidated=0;

    auto rof = (*rofs)[iRof];

    std::cout << "Entry: " << counter << std::endl;
    ++counter;
    itsClusters.GetEntry(rof.getROFEntry().getEvent());
    mcHeaderTree.GetEntry(rof.getROFEntry().getEvent());
    int nclUsed = o2::its::IOUtils::loadROFrameData(rof, frame, clusters, labels);
    vertexer.initialiseVertexer(&frame);

    vertexer.setParameters(par); //to set tanLambdaCut

    vertexer.findTracklets(true); //this is where the tracklets are created and then selected
    // the selected tracklets are stored in mTracklets

    //this is the mTracklets vector, which contains tracklets and the 2 clusters they contain
    // it is a vector of lines
    RecoMCvalidated += vertexer.getLines().size(); //gets all the tracklets
   
    vertexer.findTrivialMCTracklets();
    TGenerated += vertexer.getLines().size();

    vertexer.findTracklets(false);
    FakeTracklets += vertexer.getLines().size(); //only the false ones 

   
    //std::cout<<"Fake tracklets :"<< FakeTracklets<<std::endl;

    float tanLambdaCut = vertexer.getVertParameters().tanLambdaCut;

    if(RecoMCvalidated!=0 && TGenerated!=0 && FakeTracklets!=0){ //do not write entries that are 0
      results->Fill(RecoMCvalidated,FakeTracklets,TGenerated,tanLambdaCut);
    }
    std::cout<<"Tracklets selected :"<<RecoMCvalidated<<"   Total real tracklets :"<<TGenerated<<"  Cut :"<<tanLambdaCut<<std::endl;
  }

   
  results->Write(0,TObject::kOverwrite);
  outputfile->Close();


  //return 0;
}


#endif



    