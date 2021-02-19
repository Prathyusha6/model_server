//*****************************************************************************
// Copyright 2020 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************
#include "pipeline_factory.hpp"

#include "logging.hpp"
#include "pipeline.hpp"
#include "pipelinedefinition.hpp"
#include "prediction_service_utils.hpp"

namespace ovms {

bool PipelineFactory::definitionExists(const std::string& name) const {
    std::shared_lock lock(definitionsMtx);
    return definitions.count(name) == 1;
}

PipelineDefinition* PipelineFactory::findDefinitionByName(const std::string& name) const {
    std::shared_lock lock(definitionsMtx);
    auto it = definitions.find(name);
    if (it == std::end(definitions)) {
        return nullptr;
    } else {
        return it->second.get();
    }
}

void PipelineFactory::retireOtherThan(std::set<std::string>&& pipelinesInConfigFile, ModelManager& manager) {
    std::for_each(definitions.begin(),
        definitions.end(),
        [&pipelinesInConfigFile, &manager](auto& nameDefinitionPair) {
            if (pipelinesInConfigFile.find(nameDefinitionPair.second->getName()) == pipelinesInConfigFile.end() && nameDefinitionPair.second->getStateCode() != PipelineDefinitionStateCode::RETIRED) {
                nameDefinitionPair.second->retire(manager);
            }
        });
}

Status PipelineFactory::createDefinition(const std::string& pipelineName,
    const std::vector<NodeInfo>& nodeInfos,
    const pipeline_connections_t& connections,
    ModelManager& manager) {
    if (definitionExists(pipelineName)) {
        SPDLOG_LOGGER_ERROR(modelmanager_logger, "pipeline definition: {} is already created", pipelineName);
        return StatusCode::PIPELINE_DEFINITION_ALREADY_EXIST;
    }
    std::unique_ptr<PipelineDefinition> pipelineDefinition = std::make_unique<PipelineDefinition>(pipelineName, nodeInfos, connections);

    pipelineDefinition->makeSubscriptions(manager);
    Status validationResult = pipelineDefinition->validate(manager);
    if (!validationResult.ok()) {
        pipelineDefinition->resetSubscriptions(manager);
        SPDLOG_LOGGER_ERROR(modelmanager_logger, "Loading pipeline definition: {} failed: {}", pipelineName, validationResult.string());
        return validationResult;
    }

    std::unique_lock lock(definitionsMtx);
    definitions[pipelineName] = std::move(pipelineDefinition);

    SPDLOG_LOGGER_INFO(modelmanager_logger, "Loading pipeline definition: {} succeeded", pipelineName);
    return StatusCode::OK;
}

Status PipelineFactory::create(std::unique_ptr<Pipeline>& pipeline,
    const std::string& name,
    const tensorflow::serving::PredictRequest* request,
    tensorflow::serving::PredictResponse* response,
    ModelManager& manager) const {
    if (!definitionExists(name)) {
        SPDLOG_LOGGER_INFO(dag_executor_logger, "Pipeline with requested name: {} does not exist", name);
        return StatusCode::PIPELINE_DEFINITION_NAME_MISSING;
    }
    std::shared_lock lock(definitionsMtx);
    auto& definition = *definitions.at(name);
    lock.unlock();
    return definition.create(pipeline, request, response, manager);
}

Status PipelineFactory::reloadDefinition(const std::string& pipelineName,
    const std::vector<NodeInfo>&& nodeInfos,
    const pipeline_connections_t&& connections,
    ModelManager& manager) {
    auto pd = findDefinitionByName(pipelineName);
    if (pd == nullptr) {
        SPDLOG_LOGGER_ERROR(modelmanager_logger, "Requested to reload pipeline definition but it does not exist: {}", pipelineName);
        return StatusCode::UNKNOWN_ERROR;
    }
    return pd->reload(manager, std::move(nodeInfos), std::move(connections));
}

void PipelineFactory::revalidatePipelines(ModelManager& manager) {
    for (auto& [name, definition] : definitions) {
        if (definition->getStatus().isRevalidationRequired()) {
            auto validationResult = definition->validate(manager);
            if (!validationResult.ok()) {
                SPDLOG_LOGGER_ERROR(modelmanager_logger, "Revalidation pipeline definition: {} failed: {}", name, validationResult.string());
            } else {
                SPDLOG_LOGGER_DEBUG(modelmanager_logger, "Revalidation of pipeline: {} succeeded", name);
            }
        }
    }
}

void PipelineFactory::getPipelinesStatuses(std::map<std::string, PipelineDefinitionStatus>& pipelineStatuses) {
    std::shared_lock lock(definitionsMtx);
    for (auto const& [pipelineName, definition] : definitions) {
        auto status = std::pair<std::string, PipelineDefinitionStatus>(pipelineName, definition->getStatus());
        pipelineStatuses.insert(status);
    }
}
}  // namespace ovms
