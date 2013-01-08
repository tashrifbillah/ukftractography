/**
 * \file dwi_normalize.cc
 * \brief Implements functions of dwi_normalize.h
 * \author Yinpeng Li (mousquetaires@unc.edu)
*/

#include "dwi_normalize.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <utility>
#include <vector>
#include <cmath>
#include <cassert>

// Reimplemented functinality of math.h
#include "math_utilities.h"

void dwiNormalize(const Nrrd *raw, Nrrd *&normalized)
{

  assert(DATA_DIMENSION == 4) ;

  if (raw->dim != static_cast<unsigned int>(DATA_DIMENSION)) {
    std::cout << "The dimension of the nrrd data must be " << DATA_DIMENSION << "!" << std::endl ;
    exit(1) ;
  }

  //Check the world coordinate frame
  if (
    raw->space != nrrdSpaceRightAnteriorSuperior &&
    raw->space != nrrdSpaceLeftAnteriorSuperior &&
    raw->space != nrrdSpaceLeftPosteriorSuperior
  ) {
    std::cout << "Can only handle RAS, LAS and LPS world coordinate frames!" << std::endl ;
    exit(1) ;
  }

  if (nrrdConvert(normalized, raw, nrrdTypeFloat)) {	//NOTICE that in the current version of teem, all the key/value pairs are lost after the conversion
    std::cout << "Error during nrrd data type conversion!" << std::endl ;
    char *txt = biffGetDone(NRRD) ;
    std::cout << txt << std::endl ;
    delete txt ;
    exit(1) ;
  }

  nrrdKeyValueClear(normalized) ;	//Force to erase the key/value pairs

  //Process the key/value pairs. Identify the non-zero gradients, namely the non-zero B values
  std::vector<std::pair<char *, char *> > keyValuePairsOfRaw ;
  std::vector<bool> nonZeroGradientFlag ;
  for (unsigned int i = 0; i < nrrdKeyValueSize(raw); i++) {
    char *key ;
    char *value ;
    nrrdKeyValueIndex(raw, &key, &value, i) ;
    std::string keyStr(key) ;
    if (keyStr.length() > 14 && !keyStr.substr(0, 14).compare("DWMRI_gradient")) {
      float gx, gy, gz ;
      if (3 != sscanf(value, "%f %f %f", &gx, &gy, &gz)) {
        std::cout << "The gradients must have 3 components!" << std::endl ;
        exit(1) ;
      }
      nonZeroGradientFlag.push_back(sqrt(gx * gx + gy * gy + gz * gz) > 0) ;
    }
    keyValuePairsOfRaw.push_back(std::make_pair(key, value));
  }

  int numNonZeroGradients = 0 ;
  int numZeroGradients = 0 ;
  for (unsigned int i = 0; i < nonZeroGradientFlag.size(); i++) {
    if (nonZeroGradientFlag[i])
      numNonZeroGradients++ ;
    else
      numZeroGradients++ ;
  }
  if (numNonZeroGradients == 0) {
    std::cout << "No valid gradients in the data!" << std::endl ;
    exit(1) ;
  }
  if (numZeroGradients == 0) {
    std::cout << "No zero gradients!" << std::endl ;
    exit(1) ;
  }

  std::cout << "Number of non-zero gradients: " << numNonZeroGradients << std::endl ;
  std::cout << "Number of zero gradients: " << numZeroGradients << std::endl ;

  //Find the list type axis, namely the gradient axis
  int listAxis = -1 ;
  for (int i = 0; i < DATA_DIMENSION; i++) {
    if (normalized->axis[i].kind == nrrdKindList || normalized->axis[i].kind == nrrdKindVector || normalized->axis[i].kind == nrrdKindPoint) {
      if (listAxis != -1) {
        std::cout << "Too many list axes in the data!" << std::endl ;
        exit(1) ;
      }
      listAxis = i ;
    } else if (normalized->axis[i].kind != nrrdKindDomain && normalized->axis[i].kind != nrrdKindSpace) {
      std::cout << "Unrecognizable axis kind: axis " << i << " is of kind " << normalized->axis[i].kind << std::endl ;
      exit(1) ;
    }
  }
  if (listAxis == -1) {
    std::cout << "Can not find the list axis!" << std::endl ;
    exit(1) ;
  }

  assert(nonZeroGradientFlag.size() == normalized->axis[listAxis].size) ;

  Nrrd *temp = NULL ;

  //Compute the permutation
  std::vector<unsigned int> permutation(DATA_DIMENSION) ;
  unsigned int permuteCounter = 0 ;
  permutation[0] = static_cast<unsigned int>(listAxis) ;		//Shift the list axis to the fastest axis
  for (int i = 1; i < DATA_DIMENSION; i++) {
    if (permuteCounter == static_cast<unsigned int>(listAxis))
      permuteCounter++ ;
    permutation[i] = permuteCounter++ ;
  }

  std::cout << "Permuting the axis order to:" ;
  for (int i = 0; i < DATA_DIMENSION; i++)
    std::cout << " " << permutation[i] ;
  std::cout << std::endl ;

  //Perform the permutation
  temp = nrrdNew() ;
  if (nrrdAxesPermute(temp, normalized, &permutation[0])) {
    std::cout << "Failed while permuting the data!" << std::endl ;
    char *txt = biffGetDone(NRRD) ;
    std::cout << txt << std::endl ;
    delete txt ;
    exit(1) ;
  }
  nrrdNuke(normalized) ;
  normalized = temp ;
  temp = NULL ;



  //Perform the cropping
  std::vector<size_t> newSize_Min(DATA_DIMENSION, 0) ;
  std::vector<size_t> newSize_Max(DATA_DIMENSION) ;
  newSize_Max[0] = static_cast<size_t>(numNonZeroGradients - 1) ;
  for (int i = 1; i < DATA_DIMENSION; i++) {
    newSize_Max[i] = normalized->axis[i].size - 1 ;
  }
  std::cout << "Resizing the data to:" ;
  for (int i = 0; i < DATA_DIMENSION; i++) {
    std::cout << " " << newSize_Max[i] + 1 ;
  }
  std::cout << std::endl ;

  temp = nrrdNew() ;

  if (nrrdCrop(temp, normalized, &newSize_Min[0], &newSize_Max[0])) {
    std::cout << "Error while resizing the data!" << std::endl ;
    char *txt = biffGetDone(NRRD) ;
    std::cout << txt << std::endl ;
    delete txt ;
    exit(-1) ;
  }


  //Average the baseline image
  std::cout << "Computing the baseline image" << std::endl ;
  std::vector<float> baseline(temp->axis[1].size * temp->axis[2].size * temp->axis[3].size, 0) ;
  const float *sourceData = static_cast<const float *>(normalized->data) ;
  for (size_t k = 0; k < normalized->axis[3].size; k++) {
    for (size_t j = 0; j < normalized->axis[2].size; j++) {
      for (size_t i = 0; i < normalized->axis[1].size; i++) {
        for (size_t h = 0; h < normalized->axis[0].size; h++) {
          if (!nonZeroGradientFlag[h]) {
            baseline[k * normalized->axis[2].size * normalized->axis[1].size + j * normalized->axis[1].size + i] +=
              sourceData[k * normalized->axis[2].size * normalized->axis[1].size * normalized->axis[0].size + j * normalized->axis[1].size * normalized->axis[0].size + i * normalized->axis[0].size + h] ;
          }
        }
        baseline[k * normalized->axis[2].size * normalized->axis[1].size + j * normalized->axis[1].size + i] /= numZeroGradients ;
      }
    }
  }


  //Perform the normalization, divide the signal by baseline image
  std::cout << "Dividing the signal by baseline image" << std::endl ;
  float *destData = static_cast<float *>(temp->data) ;
  for (size_t k = 0; k < temp->axis[3].size; k++) {
    for (size_t j = 0; j < temp->axis[2].size; j++) {
      for (size_t i = 0; i < temp->axis[1].size; i++) {
        int correspondingSourceGradient = 0 ;
        for (size_t h = 0; h < temp->axis[0].size; h++) {
          while (!nonZeroGradientFlag[correspondingSourceGradient])
            correspondingSourceGradient++ ;
          destData[k * temp->axis[2].size * temp->axis[1].size * temp->axis[0].size + j * temp->axis[1].size * temp->axis[0].size + i * temp->axis[0].size + h] =
          (baseline[k * temp->axis[2].size * temp->axis[1].size + j * temp->axis[1].size + i] != 0) ?
          sourceData[k * normalized->axis[2].size * normalized->axis[1].size * normalized->axis[0].size + j * normalized->axis[1].size * normalized->axis[0].size + i * normalized->axis[0].size + correspondingSourceGradient] /
          baseline[k * temp->axis[2].size * temp->axis[1].size + j * temp->axis[1].size + i] :
          1e-10; // prevent log(0) errors in UnpackTensors( ... )
          //NOTICE that when the baseline image is 0 at this voxel, the signal will also be 0
          //This fixes the bug in the Python module

          correspondingSourceGradient++ ;

        }
      }
    }
  }
  nrrdNuke(normalized) ;
  normalized = temp ;
  temp = NULL ;

  if (normalized->content != NULL)
    delete normalized->content, normalized->content = NULL ;	//Get rid of the content field

  //Add the key/value pairs back to the normalized data
  int totalGradientCounter = 0 ;
  int nonZeroGradientCounter = 0 ;
  for (unsigned int i = 0; i < keyValuePairsOfRaw.size(); i++) {
    std::string keyStr(keyValuePairsOfRaw[i].first) ;
    if (keyStr.length() > 14 && !keyStr.substr(0, 14).compare("DWMRI_gradient")) {	//Treat gradient data specially
      if (nonZeroGradientFlag[totalGradientCounter]) {
        std::stringstream ss ;
        ss << "DWMRI_gradient_" << std::setfill('0') << std::setw(4) << nonZeroGradientCounter++ ;
        if (nrrdKeyValueAdd(normalized, ss.str().c_str(), keyValuePairsOfRaw[i].second)) {
          std::cout << "Error while adding key/value pairs!" << std::endl ;
          exit(1) ;
        }
      }
      totalGradientCounter++ ;      //No output for 0 gradients to the normalized data
    } else {
      if (nrrdKeyValueAdd(normalized, keyValuePairsOfRaw[i].first, keyValuePairsOfRaw[i].second)) {
        std::cout << "Error while adding key/value pairs!" << std::endl ;
        char *txt = biffGetDone(NRRD) ;
        std::cout << txt << std::endl ;
        delete txt ;
        exit(1) ;
      }
    }
    delete keyValuePairsOfRaw[i].first ;	//Free the memory
    delete keyValuePairsOfRaw[i].second ;
  }


  //ATTENTION: Slicer3 employs an RAS coordinate frame
  //So the ijk->world matrix and measurement frame used in the program have to be transformed into the RAS
  //coordinate frame in order to make the output tracts lie in the right position when rendered by Slicer
  if (normalized->space != nrrdSpaceRightAnteriorSuperior) {

    std::cout << "Converting the world coordinate system to RAS" << std::endl ;

    const bool usesLAS = (normalized->space == nrrdSpaceLeftAnteriorSuperior) ;
    const bool usesLPS = (normalized->space == nrrdSpaceLeftPosteriorSuperior) ;

    for (int i = 1; i < DATA_DIMENSION; i++) {
      if (usesLAS || usesLPS) {
        normalized->axis[i].spaceDirection[0] = -normalized->axis[i].spaceDirection[0] ;
        normalized->measurementFrame[i - 1][0] = -normalized->measurementFrame[i - 1][0] ;
      }
      if (usesLPS) {
        normalized->axis[i].spaceDirection[1] = -normalized->axis[i].spaceDirection[1] ;
        normalized->measurementFrame[i - 1][1] = -normalized->measurementFrame[i - 1][1] ;
      }
    }

    if (usesLAS || usesLPS) {
      normalized->spaceOrigin[0] = -normalized->spaceOrigin[0] ;
    }
    if (usesLPS) {
      normalized->spaceOrigin[1] = -normalized->spaceOrigin[1] ;
    }

    normalized->space = nrrdSpaceRightAnteriorSuperior ;
  }


  std::cout << "Data normalization finished!" << std::endl << std::endl ;

}
