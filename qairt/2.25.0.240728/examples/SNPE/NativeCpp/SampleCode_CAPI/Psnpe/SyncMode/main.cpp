//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================
//
// This file contains an example application that loads and executes a neural
// network using the SNPE C API and saves the layer output to a file.
// Inputs to and outputs from the network are conveyed in binary form as single
// precision floating point values.
//
#include <cstring>
#include <iostream>

/* Windows Modification
* Replace <getopt.h> to <GetOpt.hpp> and refactor the "Process command line arguments" part
*/
#ifdef _WIN32
#include "GetOpt.hpp"
using namespace WinOpt;
#else
#include <unistd.h>
#endif

#include <fstream>
#include <cstdlib>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

#include "BuildPSNPE.hpp"
#include "ProcessUserBuffer.hpp"
#include "ProcessInputList.hpp"
#include "ProcessDataType.hpp"
#include "Util.hpp"

#include "DlSystem/DlError.h"
#include "DlSystem/RuntimeList.h"
#include "DlSystem/DlEnums.h"
#include "DlSystem/IUserBuffer.h"
#include "DlSystem/IBufferAttributes.h"
#include "SNPE/PSNPE.h"
#include "SNPE/SNPEUtil.h"
#include "DlContainer/DlContainer.h"

#define FAILURE 1
#define SUCCESS 0

struct Configure{
    //necessary arguments
    std::string dlcPath;
    std::string inputListPath;
    //Optional arguments with default values
    std::string outputDir = "./output";
    std::vector<Snpe_Runtime_t> runtimes = std::vector<Snpe_Runtime_t>(1, SNPE_RUNTIME_CPU_FLOAT32);
    bool usingInitCache = false;
    bool cpuFixedPointMode = false;
};

// There are 3 mode in PSNPE
// This Sample Code is the example for running with mode SNPE_PSNPE_INPUTOUTPUTTRANSMISSIONMODE_SYNC
static const Snpe_PSNPE_InputOutputTransmissionMode_t g_executionMode = SNPE_PSNPE_INPUTOUTPUTTRANSMISSIONMODE_SYNC;
static Snpe_DlContainer_Handle_t g_dlcHandle = nullptr;
static Snpe_PSNPE_Handle_t g_psnpeHandle = nullptr;
static Snpe_UserBufferList_Handle_t g_inputBufferList = nullptr;
static Snpe_UserBufferList_Handle_t g_outputBufferList = nullptr;

Configure ParseCommandLine(int argc, char** argv);
void Cleanup();

int main(int argc, char** argv) {
    std::cout << "\nSNPE SDK VERSION: v" << GetSdkVersion() <<std::endl;
    // Process command line arguments
    Configure config = ParseCommandLine(argc, argv);

#ifdef __ANDROID__
    // Initialize Logs with level LOG_ERROR.
    Snpe_Util_InitializeLogging(SNPE_LOG_LEVEL_ERROR);
#else
    // Initialize Logs with specified log level as LOG_ERROR and log path as "./Log".
    Snpe_Util_InitializeLoggingPath(SNPE_LOG_LEVEL_ERROR, "./Log");
#endif

    // Update Log Level to LOG_WARN.
    Snpe_Util_SetLogLevel(SNPE_LOG_LEVEL_WARN);

    // Check runtimes
    for (Snpe_Runtime_t runtime: config.runtimes) {
        if (!Snpe_Util_IsRuntimeAvailable(runtime)) {
            std::cerr << "Selected runtime not available." << std::endl;
            std::exit(FAILURE);
        }
    }

    g_dlcHandle = Snpe_DlContainer_Open(config.dlcPath.c_str());
    if (g_dlcHandle == nullptr) {
        std::cerr << "Load Container fail. dlc file: " << config.dlcPath
                  << std::endl;
        std::exit(FAILURE);
    }
    else {
        std::cout << "Successful Load Container file: \""<< config.dlcPath <<"\"." << std::endl;
    }

    // Create and build PSNPE
    g_psnpeHandle = BuildPSNPE(g_dlcHandle,
                               config.runtimes,
                               g_executionMode,
                               SNPE_PERFORMANCE_PROFILE_BURST,
                               config.usingInitCache,
                               config.cpuFixedPointMode);
    if (g_psnpeHandle == nullptr) {
        Cleanup();
        std::exit(FAILURE);
    }

    if (config.usingInitCache) {
        // We can save Cached Container to a dlc file, so that PSNPE can load it and build faster
        // when InitChach is need. In this sample we direct overwrite to original dlc file.
        if (Snpe_DlContainer_Save(g_dlcHandle, config.dlcPath.c_str()) == SNPE_SUCCESS) {
            std::cout << "Saved container into archive successfully" << "\n";
        } else {
            std::cout << "Failed to save container into archive" << std::endl;
        }
    }

    // Getting input informations: name, dimensions, datatype
    std::set<std::string> inputNames;
    std::unordered_map<std::string, std::vector<size_t>> inputDims;
    std::unordered_map<std::string, Snpe_UserBufferEncoding_ElementType_t> dataTypes;
    std::unordered_map<std::string, uint64_t> stepEquivalentTo0;
    std::unordered_map<std::string, float> quantizedStepSize;
    Snpe_StringList_Handle_t inputNamesHandle = Snpe_PSNPE_GetInputTensorNames(g_psnpeHandle);
    const char** end = Snpe_StringList_End(inputNamesHandle);
    for (const char** it = Snpe_StringList_Begin(inputNamesHandle); it != end; ++it) {
        const char* name = *it;
        Snpe_IBufferAttributes_Handle_t bufAttrHandle = Snpe_PSNPE_GetInputOutputBufferAttributes(g_psnpeHandle, name);
        Snpe_TensorShape_Handle_t dimsHandle = Snpe_IBufferAttributes_GetDims(bufAttrHandle);
        size_t rank = Snpe_TensorShape_Rank(dimsHandle);
        const size_t* dims = Snpe_TensorShape_GetDimensions(dimsHandle);
        std::vector<size_t> dimsVec(rank);
        for (size_t i = 0; i < dimsVec.size(); ++i) {
            dimsVec[i] = dims[i];
        }
        inputDims[name] = dimsVec;
        Snpe_UserBufferEncoding_ElementType_t dataType = Snpe_IBufferAttributes_GetEncodingType(bufAttrHandle);
        if (dataType == SNPE_USERBUFFERENCODING_ELEMENTTYPE_TF8
            || dataType == SNPE_USERBUFFERENCODING_ELEMENTTYPE_TF16) {
                Snpe_UserBufferEncoding_Handle_t encodingRef = Snpe_IBufferAttributes_GetEncoding_Ref(bufAttrHandle);
                stepEquivalentTo0[name] = Snpe_UserBufferEncodingTfN_GetStepExactly0(encodingRef);
                quantizedStepSize[name] = Snpe_UserBufferEncodingTfN_GetQuantizedStepSize(encodingRef);
        }
        dataTypes[name] = dataType;
        inputNames.insert(name);
        Snpe_TensorShape_Delete(dimsHandle);
        Snpe_IBufferAttributes_Delete(bufAttrHandle);
    }
    Snpe_StringList_Delete(inputNamesHandle);

    // Getting ouput informations: name, dimensions, datatype
    std::set<std::string> outputNames;
    std::unordered_map<std::string, std::vector<size_t>> outputDims;
    Snpe_StringList_Handle_t outputNamesHandle = Snpe_PSNPE_GetOutputTensorNames(g_psnpeHandle);
    end = Snpe_StringList_End(outputNamesHandle);
    for (const char** it = Snpe_StringList_Begin(outputNamesHandle); it != end; ++it) {
        const char* name = *it;
        Snpe_IBufferAttributes_Handle_t bufAttrHandle = Snpe_PSNPE_GetInputOutputBufferAttributes(g_psnpeHandle, name);
        Snpe_TensorShape_Handle_t dimsHandle = Snpe_IBufferAttributes_GetDims(bufAttrHandle);
        size_t rank = Snpe_TensorShape_Rank(dimsHandle);
        const size_t* dims = Snpe_TensorShape_GetDimensions(dimsHandle);
        std::vector<size_t> dimsVec(rank);
        for (size_t i = 0; i < dimsVec.size(); ++i) {
            dimsVec[i] = dims[i];
        }
        outputDims[name] = dimsVec;
        Snpe_UserBufferEncoding_ElementType_t dataType = Snpe_IBufferAttributes_GetEncodingType(bufAttrHandle);
        if (dataType == SNPE_USERBUFFERENCODING_ELEMENTTYPE_TF8
            || dataType == SNPE_USERBUFFERENCODING_ELEMENTTYPE_TF16) {
                Snpe_UserBufferEncoding_Handle_t encodingRef = Snpe_IBufferAttributes_GetEncoding_Ref(bufAttrHandle);
                stepEquivalentTo0[name] = Snpe_UserBufferEncodingTfN_GetStepExactly0(encodingRef);
                quantizedStepSize[name] = Snpe_UserBufferEncodingTfN_GetQuantizedStepSize(encodingRef);
        }
        dataTypes[name] = dataType;
        outputNames.insert(name);
        Snpe_TensorShape_Delete(dimsHandle);
        Snpe_IBufferAttributes_Delete(bufAttrHandle);
    }
    Snpe_StringList_Delete(outputNamesHandle);

    std::cout << "\nInput:" << std::endl;
    for (std::string name : inputNames) {
        std::cout << "Name: " << name
                  << "\tDimensions: " << ArrayToStr(inputDims[name])
                  << "\tDataType: " << DataTypeToStr(dataTypes[name]);
        std::cout << "\n" << std::endl;
    }
    std::cout << "Output:" << std::endl;
    for (std::string name : outputNames) {
        std::cout << "Name: " << name
                  << "\tDimensions: " << ArrayToStr(outputDims[name])
                  << "\tDataType: " << DataTypeToStr(dataTypes[name]);
        std::cout << "\n" << std::endl;
    }

    // batchSize is the first value of input dimensions.
    // The first dimension of all inputs and outputs must be equal to batchSize,
    // otherwise something went wrong
    size_t batchSize = inputDims.begin()->second[0];
    size_t inputFileNumber = 0;
    std::vector<std::unordered_map<std::string, std::vector<std::string>>> inputFileList;
    // Open the input file listing and group input files into batches
    inputFileList = ProcessInputList(config.inputListPath, batchSize, inputNames, inputFileNumber);
    if (inputFileList.empty()) {
        std::cerr << "Error: load input list fail." << std::endl;
        Cleanup();
        std::exit(FAILURE);
    }

    // Load contents of input file batches into a UserBuffer
    // execute the network with the input and save each of the returned output to a file.
    // SNPE/PSNPE allows its input and output buffers that are fed to the network
    // to come from user-backed buffers. First, SNPE buffers are created from
    // user-backed storage. These SNPE buffers are then supplied to the network
    // and the results are stored in user-backed output buffers. This allows for
    // reusing the same buffers for multiple inputs and outputs.
    std::vector<std::unordered_map<std::string, std::vector<uint8_t>>> applicationInputBufferList;
    std::vector<std::unordered_map<std::string, std::vector<uint8_t>>> applicationOutputBufferList;
    g_inputBufferList = CreateUserBufferList(inputDims,
                                             dataTypes,
                                             inputFileList.size(),
                                             applicationInputBufferList,
                                             stepEquivalentTo0,
                                             quantizedStepSize);
    g_outputBufferList = CreateUserBufferList(outputDims,
                                              dataTypes,
                                              inputFileList.size(),
                                              applicationOutputBufferList,
                                              stepEquivalentTo0,
                                              quantizedStepSize);

    if (!LoadFileToUserBufferList(g_inputBufferList, inputFileList, applicationInputBufferList)) {
        std::cerr << "Loading input file paths to input UserBufferList fail." << std::endl;
        Cleanup();
        std::exit(FAILURE);
    }

    Snpe_ErrorCode_t executeStatus = Snpe_PSNPE_Execute(g_psnpeHandle, g_inputBufferList, g_outputBufferList);
    if (executeStatus != SNPE_SUCCESS) {
        std::cerr << "Error: PSNPE execute fail. status: "<< static_cast<typename std::underlying_type<decltype(executeStatus)>::type>(executeStatus) << std::endl;
        Cleanup();
        std::exit(FAILURE);
    }
    else {
        std::cout << "PSNPE execute success." << std::endl;
    }

    if(!SaveUserBufferList(g_outputBufferList,
                           applicationOutputBufferList,
                           config.outputDir,
                           true, batchSize, inputFileNumber)){
        std::cerr << "Error: Save outputs fail. output path: "<< config.outputDir << std::endl;
        Cleanup();
        std::exit(FAILURE);
    }

    Cleanup();
    std::cout << "Sample running executed!" << std::endl;
    return SUCCESS;
}


void Cleanup()
{
    DeleteUserBufferList(g_inputBufferList);
    DeleteUserBufferList(g_outputBufferList);
    if(g_psnpeHandle != nullptr) {
        Snpe_PSNPE_Delete(g_psnpeHandle);
    }
    if (g_dlcHandle != nullptr) {
        Snpe_DlContainer_Delete(g_dlcHandle);
    }
}

// Program can be exit with FAILURE if any argument wrong
Configure ParseCommandLine(int argc, char** argv) {
    Configure config;
    int opt = 0;
#ifndef _WIN32
    while ((opt = getopt(argc, argv, "h i: d: o: c x r:")) != -1) {
#else
    enum OPTIONS
    {
        OPT_HELP = 'h',
        OPT_CONTAINER = 'd',
        OPT_INPUT_LIST = 'i',
        OPT_OUTPUT_DIR = 'o',
        OPT_RUNTIME = 'r',
        OPT_INITBLOBSCACHE = 'c',
        OPT_CPU_FXP = 'x'
    };
    static struct WinOpt::option long_options[] = {
      {"h",     WinOpt::no_argument,          NULL,  OPT_HELP},
      {"d",     WinOpt::required_argument,    NULL,  OPT_CONTAINER},
      {"i",     WinOpt::required_argument,    NULL,  OPT_INPUT_LIST},
      {"o",     WinOpt::required_argument,    NULL,  OPT_OUTPUT_DIR},
      {"r",     WinOpt::required_argument,    NULL,  OPT_RUNTIME},
      {"c",     WinOpt::no_argument,          NULL,  OPT_INITBLOBSCACHE},
      {"x",     WinOpt::no_argument,          NULL,  OPT_CPU_FXP},
      {NULL,                      0,                  NULL,  0 }
    };
    int long_index = 0;
    while ((opt = WinOpt::GetOptLongOnly(argc, argv, "", long_options, &long_index)) != -1) {
#endif
        switch (opt) {
            case 'h':
                std::cout
                        << "\nDESCRIPTION:\n"
                        << "------------\n"
                        << "Example application demonstrating how to load and execute a neural network\n"
                        << "using the PSNPE C API.\n"
                        << "\n\n"
                        << "REQUIRED ARGUMENTS:\n"
                        << "-------------------\n"
                        << "  -d  <FILE>   Path to the DL container containing the network.\n"
                        << "  -i  <FILE>   Path to a file listing the inputs for the network.\n"
                        << "\n"
                        << "OPTIONAL ARGUMENTS:\n"
                        << "-------------------\n"
                        << "  -o  <PATH>    Path to directory to store output results.\n"
                        << "                If not specified DataType, this sample select datatype automatically base on model.\n"
                        << "  -c            Enable init caching to accelerate the initialization process of PSNPE. Defaults to disable.\n"
                        << "  -x            Enable the fixed point execution on CPU runtime for PSNPE. Defaults to disable.\n"
                        << "  -r  <RUNTIME> The list of runtime to be used e.g cpu,dsp (cpu is default). Valid values are:\n"
                        << "                    cpu_float32 (Snapdragon CPU)       = Data & Math: float 32bit \n"
                        << "                    gpu_float32_16_hybrid (Adreno GPU) = Data: float 16bit Math: float 32bit \n"
                        << "                    dsp_fixed8_tf (Hexagon DSP)        = Data & Math: 8bit fixed point Tensorflow style format \n"
                        << "                    gpu_float16 (Adreno GPU)           = Data: float 16bit Math: float 16bit \n"
                        #if DNN_RUNTIME_HAVE_AIP_RUNTIME
                        << "                    aip_fixed8_tf (Snapdragon HTA+HVX) = Data & Math: 8bit fixed point Tensorflow style format \n"
                        #endif
                        << "                    cpu (Snapdragon CPU)               = Same as cpu_float32 \n"
                        << "                    gpu (Adreno GPU)                   = Same as gpu_float32_16_hybrid \n"
                        << "                    dsp (Hexagon DSP)                  = Same as dsp_fixed8_tf \n"
                        #if DNN_RUNTIME_HAVE_AIP_RUNTIME
                        << "                    aip (Snapdragon HTA+HVX)           = Same as aip_fixed8_tf \n"
                        #endif
                        << std::endl;
                std::exit(SUCCESS);
            case 'd':
                config.dlcPath = optarg;
                break;
            case 'i':
                config.inputListPath = optarg;
                break;
            case 'o':
                config.outputDir = optarg;
                break;
            case 'c':
                config.usingInitCache = true;
                break;
            case 'x':
                config.cpuFixedPointMode = true;
                break;
            case 'r':
                {
                    std::string runtimeListStr = optarg;
                    config.runtimes.clear();
                    std::vector<std::string> runtimeStrList = Split(runtimeListStr, ",");
                    if (runtimeStrList.empty()) {
                        runtimeStrList.push_back(runtimeListStr);
                    }
                    for (auto &runtimeStr: runtimeStrList) {

                        Snpe_Runtime_t runtime = Snpe_RuntimeList_StringToRuntime(runtimeStr.c_str());
                        if (runtime != SNPE_RUNTIME_UNSET) {
                            config.runtimes.push_back(runtime);
                        }
                        else {
                            std::cerr << "Error: Invalid values passed to the argument -r " << runtimeListStr
                                      << ". Please provide comma seperated runtime order of precedence" << std::endl;
                            std::exit(FAILURE);
                        }
                    }
                }
                break;
            default:
                std::cerr << "Invalid parameter specified. Please run with the -h flag to see required arguments."
                          << std::endl;
                std::exit(FAILURE);
        }
    }
    return config;
}
