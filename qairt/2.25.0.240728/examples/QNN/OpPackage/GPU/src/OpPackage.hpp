//=============================================================================
//
//  Copyright (c) 2020-2022, 2024 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "GPU/QnnGpuOpPackage.h"
#include "QnnGpuTypeMacros.hpp"

class Operation;

class OpPackage {
 public:
  static std::unique_ptr<OpPackage> create(const std::string& packageName,
                                           const QnnGpu_DeviceProperties_t* deviceProperties);

  Qnn_ErrorHandle_t operationExists(std::string opName);
  Qnn_ErrorHandle_t getPackageInfo(const QnnOpPackage_Info_t** info);
  Qnn_ErrorHandle_t createOperation(QnnGpuOpPackage_GraphInfrastructure_t* graphInfrastructure,
                                    const QnnGpuOpPackage_Node_t* node,
                                    QnnGpu_Operation_t** operation);
  Qnn_ErrorHandle_t freeOperation(QnnGpu_Operation_t* operation);

 private:
  typedef std::shared_ptr<Operation> (*OpCreateFunc_t)(const QnnGpuOpPackage_Node_t* node,
                                                       Qnn_ErrorHandle_t* status);

  OpPackage(const std::string& packageName, const QnnGpu_DeviceProperties_t* deviceProperties);

  const std::string m_packageName;
  std::map<std::string, OpCreateFunc_t> m_opBuilders;
  std::vector<const char*> m_opNames;
  QnnOpPackage_Info_t m_packageInfo;
  Qnn_ApiVersion_t m_sdkApiVersion;
  std::map<QnnGpu_Operation_t*, std::shared_ptr<Operation>> m_compiledOperations;
};
