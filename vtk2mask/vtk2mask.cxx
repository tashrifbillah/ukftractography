/**
 * \file vtk2mask.cxx 
 * \brief Main file for VTK to scalar mask conversion
 *
 * This program reads vtk fibers generated by ukftractography
 * and converts the scalar fields in the vtk file to a
 * scalar volume (nrrd or nhdr).
 *
 * \author Christian Baumgartner (c.f.baumgartner@gmail.com)
 */

#include <iostream>
#include <string>
#include "Converter.h"
#include "vtk2maskCLP.h"
#include "vtkReader.h"

int main(int argc, char* argv[]) {

  // PARSE COMMAND LINE OR SLICER INPUT ////////////////////////////////////
  // Defined in GenerateCLP
  PARSE_ARGS ;

  // SANITIY CHECKS ////////////////////////////////////////////////////////

  if (!LabelOfInterest && !LabelFile.empty()) // TODO: How do you check if an integer is set??
  {
    std::cout << "No Label specified. Setting default to Label 1." << std::endl;
    LabelOfInterest = 1;
  }

  if (  OutputVolume.empty() )
  {
    std::cout << "No output volume specified! Set the --OutputVolume option." << std::endl;
    exit(1);
  }

  if (  ReferenceFile.empty() )
  {
    std::cout << "No reference volume specified! Set the --ReferenceFile option." << std::endl;
    exit(1);
  }

  std::vector<Fiber> in_fibers;

  // Read the VTK Fiber
  if (Verbose) std::cout << "** Reading VTK file...\n";
  vtkReader * reader = new vtkReader();
  reader->SetInputPath(FiberFile);
  reader->SetOutputFibers(in_fibers);
  reader->SetReadFieldData(true);
  reader->SetVerbose(Verbose);
  reader->Run();
  delete reader;
  if (Verbose) std::cout << "-Number of fibers in the input: " << in_fibers.size() << std::endl;


  // SOME ADDITIONAL CHECKS ///////////////////////////////////////////////////
  if (in_fibers.size() == 0) {
    std::cout << "The fiber file is empty. Exiting..." << std::endl;
    exit(1);
  }

  if (ScalarName.empty()) {
    if (Verbose)
      std::cout << "-No Scalar given. Will calculate label map." << std::endl;
  } else {
    Fiber::FieldMapType::iterator it;
    in_fibers[0].Fields.find(ScalarName);
    if (it == in_fibers[0].Fields.end()) {
      std::cout << "Error: The fiber file doesnt contain a label called " << ScalarName << std::endl;
      return 1;
    }
  }

  // CREATE CONVERTER AND RUN /////////////////////////////////////////////////
  if (Verbose) std::cout << "** Start converting...\n";
  Converter *conv = new Converter();
  conv->SetInputFibers(in_fibers);
  conv->SetReferenceFile(ReferenceFile);
  conv->SetOutputVolumeFile(OutputVolume);
  conv->SetFieldName(ScalarName);
  if (!StandartDevVolume.empty())
    conv->SetStdDevFile(StandartDevVolume);
  conv->SetVerbose(Verbose);
  if (!LabelFile.empty()) {
    conv->SetLabelFile(LabelFile);
    conv->SetLabelNumber(LabelOfInterest);
  }
  conv->Run();
  delete conv;

  // DONE /////////////////////////////////////////////////////////////////////
  return EXIT_SUCCESS;

}