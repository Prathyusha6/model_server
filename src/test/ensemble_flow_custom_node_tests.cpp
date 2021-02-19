//*****************************************************************************
// Copyright 2021 Intel Corporation
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
#include <array>
#include <functional>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../custom_node.hpp"
#include "../custom_node_library_manager.hpp"
#include "../dl_node.hpp"
#include "../entry_node.hpp"
#include "../exit_node.hpp"
#include "../node_library.hpp"
#include "../pipelinedefinition.hpp"
#include "test_utils.hpp"

using namespace ovms;

using tensorflow::serving::PredictRequest;
using tensorflow::serving::PredictResponse;

class EnsembleFlowCustomNodePipelineExecutionTest : public TestWithTempDir {
protected:
    void SetUp() override {
        TestWithTempDir::SetUp();
        CustomNodeLibraryManager manager;
        ASSERT_EQ(manager.loadLibrary(
                      this->libraryName,
                      this->libraryPath),
            StatusCode::OK);
        ASSERT_EQ(manager.getLibrary(
                      this->libraryName,
                      this->library),
            StatusCode::OK);
    }

    template <typename T>
    void prepareRequest(const std::vector<T>& data) {
        this->prepareRequest(this->request, data);
    }

    template <typename T>
    void prepareRequest(PredictRequest& request, const std::vector<T>& data, const std::string& inputName = pipelineInputName) {
        tensorflow::TensorProto& proto = (*request.mutable_inputs())[inputName];
        proto.set_dtype(tensorflow::DataTypeToEnum<T>::value);
        proto.mutable_tensor_content()->assign((char*)data.data(), data.size() * sizeof(T));
        proto.mutable_tensor_shape()->add_dim()->set_size(1);
        proto.mutable_tensor_shape()->add_dim()->set_size(data.size());
    }

    template <typename T>
    std::unique_ptr<Pipeline> prepareSingleNodePipelineWithLibraryMock() {
        const std::vector<float> inputValues{3.5, 2.1, -0.2};
        this->prepareRequest(inputValues);
        auto input_node = std::make_unique<EntryNode>(&request);
        auto output_node = std::make_unique<ExitNode>(&response);
        auto custom_node = std::make_unique<CustomNode>(
            customNodeName,
            createLibraryMock<T>(),
            parameters_t{});

        auto pipeline = std::make_unique<Pipeline>(*input_node, *output_node);
        pipeline->connect(*input_node, *custom_node, {{pipelineInputName, customNodeInputName}});
        pipeline->connect(*custom_node, *output_node, {{customNodeOutputName, pipelineOutputName}});

        pipeline->push(std::move(input_node));
        pipeline->push(std::move(custom_node));
        pipeline->push(std::move(output_node));
        return pipeline;
    }

    template <typename T>
    void checkResponse(std::vector<T> data, std::function<T(T)> op) {
        this->checkResponse(this->pipelineOutputName, data, op);
    }

    template <typename T>
    void checkResponse(const std::string& outputName, std::vector<T> data, std::function<T(T)> op) {
        this->checkResponse(outputName, this->response, data, op);
    }

    template <typename T>
    void checkResponse(const std::string& outputName, const PredictResponse& response, const std::vector<T>& data, const shape_t& shape) {
        ASSERT_TRUE(response.outputs().contains(outputName));
        const auto& proto = response.outputs().at(outputName);

        ASSERT_EQ(proto.tensor_content().size(), data.size() * sizeof(T));
        ASSERT_EQ(proto.tensor_shape().dim_size(), shape.size());
        for (size_t i = 0; i < shape.size(); ++i) {
            ASSERT_EQ(proto.tensor_shape().dim(i).size(), shape[i]);
        }

        auto* ptr = reinterpret_cast<const T*>(proto.tensor_content().c_str());
        const std::vector<T> actual(ptr, ptr + data.size());
        for (size_t i = 0; i < actual.size(); i++) {
            EXPECT_NEAR(actual[i], data[i], 0.001) << " i is: " << i;
        }
    }

    template <typename T>
    void checkResponse(const std::string& outputName, const PredictResponse& response, std::vector<T> data, std::function<T(T)> op) {
        std::transform(data.begin(), data.end(), data.begin(), op);
        ASSERT_TRUE(response.outputs().contains(outputName));
        const auto& proto = response.outputs().at(outputName);

        ASSERT_EQ(proto.tensor_content().size(), data.size() * sizeof(T));
        ASSERT_EQ(proto.tensor_shape().dim_size(), 2);
        ASSERT_EQ(proto.tensor_shape().dim(0).size(), 1);
        ASSERT_EQ(proto.tensor_shape().dim(1).size(), data.size());

        auto* ptr = reinterpret_cast<const T*>(proto.tensor_content().c_str());

        const std::vector<T> actual(ptr, ptr + data.size());

        for (size_t i = 0; i < actual.size(); i++) {
            EXPECT_NEAR(actual[i], data[i], 0.001);
        }
    }

    template <typename T>
    static NodeLibrary createLibraryMock() {
        return NodeLibrary{
            T::execute,
            T::getInputsInfo,
            T::getOutputsInfo,
            T::release};
    }

    PredictRequest request;
    PredictResponse response;

    NodeLibrary library;

    const std::string customNodeName = "add_sub_node";
    const std::string libraryName = "add_sub_lib";
    const std::string libraryPath = "/ovms/bazel-bin/src/lib_node_add_sub.so";
    const std::string customNodeInputName = "input_numbers";
    const std::string customNodeOutputName = "output_numbers";
    static constexpr const char* pipelineInputName = "pipeline_input";
    const std::string pipelineOutputName = "pipeline_output";
};

TEST_F(EnsembleFlowCustomNodePipelineExecutionTest, AddSubCustomNode) {
    // Most basic configuration, just process single add-sub custom node pipeline request
    // input  add-sub  output
    //  O------->O------->O
    const std::vector<float> inputValues{3.2, 5.7, -2.4};
    this->prepareRequest(inputValues);

    const float addValue = 2.5;
    const float subValue = 4.8;

    auto input_node = std::make_unique<EntryNode>(&request);
    auto output_node = std::make_unique<ExitNode>(&response);
    auto custom_node = std::make_unique<CustomNode>(customNodeName, library,
        parameters_t{
            {"add_value", std::to_string(addValue)},
            {"sub_value", std::to_string(subValue)}});

    Pipeline pipeline(*input_node, *output_node);
    pipeline.connect(*input_node, *custom_node, {{pipelineInputName, customNodeInputName}});
    pipeline.connect(*custom_node, *output_node, {{customNodeOutputName, pipelineOutputName}});

    pipeline.push(std::move(input_node));
    pipeline.push(std::move(custom_node));
    pipeline.push(std::move(output_node));

    ASSERT_EQ(pipeline.execute(), StatusCode::OK);
    ASSERT_EQ(response.outputs().size(), 1);

    this->checkResponse<float>(inputValues, [addValue, subValue](float value) -> float {
        return value + addValue - subValue;
    });
}

class EnsembleFlowCustomNodeAndDemultiplexerGatherPipelineExecutionTest : public EnsembleFlowCustomNodePipelineExecutionTest {

}

TEST_F(EnsembleFlowCustomNodeAndDemultiplexerGatherPipelineExecutionTest, MultipleDemultiplexerLevels) {
    // Most basic configuration, just process single add-sub custom node pipeline request
    // input  add-sub  output
    //  O------->O------->O
    const uint demultiplicationLayersCount = 10; // TODO make dependent from core count
    // SetUp load needed libraries
    ConstructorEnabledModelManager modelManager;
    ModelConfig config = DUMMY_MODEL_CONFIG;
    ASSERT_EQ(modelManager.reloadModelWithVersions(config), StatusCode::OK);
    CustomNodeLibraryManager manager;
    NodeLibrary differentOpsLibrary;
    NodeLibrary chooseMaxLibrary;
    const std::string differentOpsLibraryName{"different_ops"};
    const std::string chooseMaxLibraryName{"choose_max"};
    const std::string differentOpsLibraryPath{"/ovms/bazel-bin/src/lib_node_perform_different_operations.so"};
    const std::string chooseMaxLibraryPath{"/ovms/bazel-bin/src/lib_node_choose_maximum.so"};
    ASSERT_EQ(manager.loadLibrary(
                  differentOpsLibraryName,
                  differentOpsLibraryPath),
        StatusCode::OK);
    ASSERT_EQ(manager.getLibrary(
                  differentOpsLibraryName,
                  differentOpsLibrary),
        StatusCode::OK);
    ASSERT_EQ(manager.loadLibrary(
                  chooseMaxLibraryName,
                  chooseMaxLibraryPath),
        StatusCode::OK);
    ASSERT_EQ(manager.getLibrary(
                  chooseMaxLibraryName,
                  chooseMaxLibrary),
        StatusCode::OK);
    // values choosen in a way that first choosen different ops result will be addition. all following ones will be multiplications
    const std::vector<float> inputValues{0.2, 0.7, -0.4, -0.1, 0.0001, -0.8, 0.7,0.8,0.9,0.1};
    const std::vector<float> inputFactors{1, -1, 2, 2};
    parameters_t parameters{
            {"selection_criteria", "MAXIMUM_MAXIMUM"}};
    PredictRequest predictRequest;
    const std::string pipelineInputName = "pipeline_input";
    const std::string pipelineOutputName = "pipeline_output";
    const std::string pipelineFactorsName = "pipeline_factors";
    const std::string chooseMaxInputName = "input_tensors";
    const std::string chooseMaxOutputName = "maximum_tensor";
    const std::string differentOpsInputName = "input_numbers";
    const std::string differentOpsFactorsInputName = "op_factors";
    const std::string differentOpsOutputName = "different_ops_results";
    const std::unordered_map<std::string, std::string> differentOpsOutputAlias{{differentOpsOutputName, differentOpsOutputName}};
    const std::unordered_map<std::string, std::string> chooseMaxOutputAlias{{chooseMaxOutputName, chooseMaxOutputName}};
    this->prepareRequest(predictRequest, inputValues, pipelineInputName);
    this->prepareRequest(predictRequest, inputFactors, pipelineFactorsName);

    auto input_node = std::make_unique<EntryNode>(&predictRequest);
    auto output_node = std::make_unique<ExitNode>(&response);

    const std::string dummyNodeName = "dummy";
    std::vector<std::unique_ptr<Node>> nodes(2 + 3 * demultiplicationLayersCount); // entry + exit + (choose + differentOps + dummy) * layerCount
    nodes[0] = std::move(input_node);
    nodes[1] = std::move(output_node);
    size_t i = 2;
    const std::string differentOpsNodeName {"different-ops-node"};
    const std::string chooseMaxNodeName {"choose-max-node"};
    const uint32_t demultiplyCount = 4; // different ops library has (1,4,10) as output
    for (size_t demultiplicationLayer = 0; demultiplicationLayer < demultiplicationLayersCount; ++demultiplicationLayer) {
        nodes[i++] = std::make_unique<CustomNode>(differentOpsNodeName + "-" + std::to_string(demultiplicationLayer), differentOpsLibrary, parameters_t{}, differentOpsOutputAlias, demultiplyCount);
        nodes[i++] = std::make_unique<DLNode>(dummyNodeName + "-" + std::to_string(demultiplicationLayer), "dummy", std::nullopt, modelManager);
        nodes[i++] = std::make_unique<CustomNode>(chooseMaxNodeName + "-" + std::to_string(demultiplicationLayer), chooseMaxLibrary, parameters, chooseMaxOutputAlias, 0, std::set<std::string>({differentOpsNodeName + "-" + std::to_string(demultiplicationLayer)}));
    }

    Pipeline pipeline(dynamic_cast<EntryNode&>(*nodes[0]), dynamic_cast<ExitNode&>(*nodes[1]));
    SPDLOG_ERROR("DemLayers:{}, i:{}", demultiplicationLayersCount, i);
    i = 2;
    for (size_t demultiplicationLayer = 0; demultiplicationLayer < demultiplicationLayersCount; ++demultiplicationLayer) {
        SPDLOG_ERROR("ER:{}   deL:{}, i + 1:{}, i+ 2:{}, i+3:{}", i, demultiplicationLayer, i + 1, i + 2, i + 3);
        if (i == 2) { // first node after entry
            pipeline.connect(*nodes[0], *nodes[i], {{pipelineFactorsName, differentOpsFactorsInputName},
                                                    {pipelineInputName, differentOpsInputName}});
        } else { // node inside pipeline
            pipeline.connect(*nodes[0], *nodes[i], {{pipelineFactorsName, differentOpsFactorsInputName}}); // if 0
        }
        pipeline.connect(*nodes[i], *nodes[i + 1], {{differentOpsOutputName, DUMMY_MODEL_INPUT_NAME}});
        pipeline.connect(*nodes[i + 1], *nodes[i + 2], {{DUMMY_MODEL_OUTPUT_NAME, chooseMaxInputName}});
        if ((i + 3) != (2 + 3 * demultiplicationLayersCount)) {
            pipeline.connect(*nodes[i + 2], *nodes[i + 3], {{chooseMaxOutputName, differentOpsInputName}}); // if last then else
        } else { // connect to exit node
            pipeline.connect(*nodes[i + 2], *nodes[1], {{chooseMaxOutputName, pipelineOutputName}}); // if last then else
        }
        SPDLOG_ERROR("ER:{}   deL:{}", i, demultiplicationLayer);
        i = i + 3;
    }

    for (auto& node : nodes) {
        pipeline.push(std::move(node));
    }

    ASSERT_EQ(pipeline.execute(), StatusCode::OK);
    ASSERT_EQ(response.outputs().size(), 1);

    // prepare data to verify
    auto expectedResult = inputValues;
    std::transform(expectedResult.begin(), expectedResult.end(), expectedResult.begin(),
            [demultiplicationLayersCount, inputFactors](float f){
                for(size_t iterations = 0; iterations < demultiplicationLayersCount; ++iterations) {
                    // input values are prepared in a way that the first layer will choose adding operation tensor
                    if (iterations == 0) {
                        f += inputFactors[0];
                    } else {
                        f *= inputFactors[2]; // different ops mutliply will be choosen
                    }
                    f += 1; // dummy
                    SPDLOG_ERROR("Intermediate expected:{}", f);
                }
                SPDLOG_ERROR("Final expected:{}", f);
                return f;
            });
    this->checkResponse(pipelineOutputName, response, expectedResult, {1, 10});

/*    this->checkResponse<float>(inputValues, [addValue, subValue](float value) -> float {
        return value + addValue - subValue;
    });*/
}

TEST_F(EnsembleFlowCustomNodePipelineExecutionTest, SeriesOfCustomNodes) {
    constexpr int N = 100;
    constexpr int PARAMETERS_PAIRS_COUNT = 2;
    static_assert(PARAMETERS_PAIRS_COUNT > 0);
    static_assert(N > PARAMETERS_PAIRS_COUNT);
    static_assert((N % PARAMETERS_PAIRS_COUNT) == 0);
    // input      add-sub x N      output
    //  O------->O->O...O->O------->O

    const std::vector<float> inputValues{3.2, 5.7, -2.4};
    this->prepareRequest(inputValues);

    const std::array<float, PARAMETERS_PAIRS_COUNT> addValues{1.5, -2.4};
    const std::array<float, PARAMETERS_PAIRS_COUNT> subValues{-5.1, 1.9};

    auto input_node = std::make_unique<EntryNode>(&request);
    auto output_node = std::make_unique<ExitNode>(&response);

    std::unique_ptr<CustomNode> custom_nodes[N];
    for (int i = 0; i < N; i++) {
        custom_nodes[i] = std::make_unique<CustomNode>(customNodeName + std::to_string(i), library,
            parameters_t{
                {"add_value", std::to_string(addValues[i % PARAMETERS_PAIRS_COUNT])},
                {"sub_value", std::to_string(subValues[i % PARAMETERS_PAIRS_COUNT])}});
    }

    Pipeline pipeline(*input_node, *output_node);
    pipeline.connect(*input_node, *(custom_nodes[0]), {{pipelineInputName, customNodeInputName}});
    pipeline.connect(*(custom_nodes[N - 1]), *output_node, {{customNodeOutputName, pipelineOutputName}});
    for (int i = 0; i < N - 1; i++) {
        pipeline.connect(*(custom_nodes[i]), *(custom_nodes[i + 1]), {{customNodeOutputName, customNodeInputName}});
    }

    pipeline.push(std::move(input_node));
    pipeline.push(std::move(output_node));
    for (auto& custom_node : custom_nodes) {
        pipeline.push(std::move(custom_node));
    }

    ASSERT_EQ(pipeline.execute(), StatusCode::OK);
    ASSERT_EQ(response.outputs().size(), 1);

    this->checkResponse<float>(inputValues, [N, addValues, subValues](float value) -> float {
        for (int i = 0; i < PARAMETERS_PAIRS_COUNT; i++) {
            value += (N / PARAMETERS_PAIRS_COUNT) * addValues[i];
            value -= (N / PARAMETERS_PAIRS_COUNT) * subValues[i];
        }
        return value;
    });
}

TEST_F(EnsembleFlowCustomNodePipelineExecutionTest, ParallelCustomNodes) {
    constexpr int N = 200;
    constexpr int PARAMETERS_PAIRS_COUNT = 5;
    static_assert(PARAMETERS_PAIRS_COUNT > 0);
    static_assert(N > PARAMETERS_PAIRS_COUNT);
    static_assert((N % PARAMETERS_PAIRS_COUNT) == 0);
    /* input    add-sub x N      output
        O---------->O------------->O
        ...        ...            /\
        L---------->O-------------_|
    */

    const std::vector<float> inputValues{9.1, -3.7, 22.2};
    this->prepareRequest(inputValues);

    const std::array<float, PARAMETERS_PAIRS_COUNT> addValues{4.5, 0.2, -0.6, 0.4, -2.5};
    const std::array<float, PARAMETERS_PAIRS_COUNT> subValues{8.5, -3.2, 10.0, -0.5, 2.4};

    auto input_node = std::make_unique<EntryNode>(&request);
    auto output_node = std::make_unique<ExitNode>(&response);

    Pipeline pipeline(*input_node, *output_node);
    std::unique_ptr<CustomNode> custom_nodes[N];
    for (int i = 0; i < N; i++) {
        custom_nodes[i] = std::make_unique<CustomNode>(customNodeName + std::to_string(i), library,
            parameters_t{
                {"add_value", std::to_string(addValues[i % PARAMETERS_PAIRS_COUNT])},
                {"sub_value", std::to_string(subValues[i % PARAMETERS_PAIRS_COUNT])}});
        pipeline.connect(*input_node, *(custom_nodes[i]),
            {{pipelineInputName, customNodeInputName}});
        pipeline.connect(*(custom_nodes[i]), *output_node,
            {{customNodeOutputName, pipelineOutputName + std::to_string(i)}});
        pipeline.push(std::move(custom_nodes[i]));
    }
    pipeline.push(std::move(input_node));
    pipeline.push(std::move(output_node));

    ASSERT_EQ(pipeline.execute(), StatusCode::OK);
    ASSERT_EQ(response.outputs().size(), N);

    for (int i = 0; i < N; i++) {
        this->checkResponse<float>(
            pipelineOutputName + std::to_string(i),
            inputValues,
            [i, addValues, subValues](float value) -> float {
                value += addValues[i % PARAMETERS_PAIRS_COUNT];
                value -= subValues[i % PARAMETERS_PAIRS_COUNT];
                return value;
            });
    }
}

TEST_F(EnsembleFlowCustomNodePipelineExecutionTest, CustomAndDLNodes) {
    // input  add-sub1 dummy  add-sub2 output
    //  O------->O------O--------O------>O
    ConstructorEnabledModelManager modelManager;
    ModelConfig config = DUMMY_MODEL_CONFIG;
    modelManager.reloadModelWithVersions(config);

    const std::vector<float> inputValues{
        4, 1.5, -5, -2.5, 9.3, 0.3, -0.15, 7.4, 5.2, -2.4};
    this->prepareRequest(inputValues);

    const float addValues[] = {-0.85, 30.2};
    const float subValues[] = {1.35, -28.5};

    auto input_node = std::make_unique<EntryNode>(&request);
    auto output_node = std::make_unique<ExitNode>(&response);
    auto model_node = std::make_unique<DLNode>(
        "dummy_node",
        "dummy",
        std::nullopt,
        modelManager);
    std::unique_ptr<CustomNode> custom_node[] = {
        std::make_unique<CustomNode>(customNodeName + "_0", library,
            parameters_t{
                {"add_value", std::to_string(addValues[0])},
                {"sub_value", std::to_string(subValues[0])}}),
        std::make_unique<CustomNode>(customNodeName + "_1", library,
            parameters_t{
                {"add_value", std::to_string(addValues[1])},
                {"sub_value", std::to_string(subValues[1])}})};

    Pipeline pipeline(*input_node, *output_node);
    pipeline.connect(*input_node, *(custom_node[0]), {{pipelineInputName, customNodeInputName}});
    pipeline.connect(*(custom_node[0]), *model_node, {{customNodeOutputName, DUMMY_MODEL_INPUT_NAME}});
    pipeline.connect(*model_node, *(custom_node[1]), {{DUMMY_MODEL_OUTPUT_NAME, customNodeInputName}});
    pipeline.connect(*(custom_node[1]), *output_node, {{customNodeOutputName, pipelineOutputName}});

    pipeline.push(std::move(input_node));
    pipeline.push(std::move(custom_node[0]));
    pipeline.push(std::move(custom_node[1]));
    pipeline.push(std::move(model_node));
    pipeline.push(std::move(output_node));

    ASSERT_EQ(pipeline.execute(), StatusCode::OK);
    ASSERT_EQ(response.outputs().size(), 1);

    this->checkResponse<float>(inputValues, [addValues, subValues](float value) -> float {
        return value + DUMMY_ADDITION_VALUE + addValues[0] + addValues[1] - subValues[0] - subValues[1];
    });
}

struct LibraryFailInExecute {
    static int execute(const struct CustomNodeTensor*, int, struct CustomNodeTensor**, int*, const struct CustomNodeParam*, int) {
        return 1;
    }
    static int getInputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int getOutputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int release(void* ptr) {
        free(ptr);
        return 0;
    }
};

TEST_F(EnsembleFlowCustomNodePipelineExecutionTest, FailInCustomNodeExecution) {
    auto pipeline = this->prepareSingleNodePipelineWithLibraryMock<LibraryFailInExecute>();
    ASSERT_EQ(pipeline->execute(), StatusCode::NODE_LIBRARY_EXECUTION_FAILED);
}

struct LibraryCorruptedOutputHandle {
    static int execute(const struct CustomNodeTensor*, int, struct CustomNodeTensor** handle, int* outputsNum, const struct CustomNodeParam*, int) {
        *handle = nullptr;
        *outputsNum = 5;
        return 0;
    }
    static int getInputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int getOutputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int release(void* ptr) {
        free(ptr);
        return 0;
    }
};

TEST_F(EnsembleFlowCustomNodePipelineExecutionTest, FailInCustomNodeOutputsCorruptedHandle) {
    auto pipeline = this->prepareSingleNodePipelineWithLibraryMock<LibraryCorruptedOutputHandle>();
    ASSERT_EQ(pipeline->execute(), StatusCode::NODE_LIBRARY_OUTPUTS_CORRUPTED);
}

struct LibraryCorruptedOutputsNumber {
    static int execute(const struct CustomNodeTensor*, int, struct CustomNodeTensor** handle, int* outputsNum, const struct CustomNodeParam*, int) {
        *handle = (struct CustomNodeTensor*)malloc(5 * sizeof(struct CustomNodeTensor));
        *outputsNum = 0;
        return 0;
    }
    static int getInputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int getOutputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int release(void* ptr) {
        free(ptr);
        return 0;
    }
};

TEST_F(EnsembleFlowCustomNodePipelineExecutionTest, FailInCustomNodeOutputsCorruptedNumberOfOutputs) {
    auto pipeline = this->prepareSingleNodePipelineWithLibraryMock<LibraryCorruptedOutputsNumber>();
    ASSERT_EQ(pipeline->execute(), StatusCode::NODE_LIBRARY_OUTPUTS_CORRUPTED_COUNT);
}

struct LibraryMissingOutput {
    static int execute(const struct CustomNodeTensor*, int, struct CustomNodeTensor** handle, int* outputsNum, const struct CustomNodeParam*, int) {
        *handle = (struct CustomNodeTensor*)malloc(sizeof(struct CustomNodeTensor));
        *outputsNum = 1;
        (*handle)->name = "random_not_connected_output";
        (*handle)->precision = CustomNodeTensorPrecision::FP32;
        (*handle)->dims = (uint64_t*)malloc(sizeof(uint64_t));
        (*handle)->dims[0] = 1;
        (*handle)->dimsLength = 1;
        (*handle)->data = (uint8_t*)malloc(sizeof(float) * sizeof(uint8_t));
        (*handle)->dataLength = sizeof(float);
        return 0;
    }
    static int getInputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int getOutputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int release(void* ptr) {
        free(ptr);
        return 0;
    }
};

TEST_F(EnsembleFlowCustomNodePipelineExecutionTest, FailInCustomNodeMissingOutput) {
    auto pipeline = this->prepareSingleNodePipelineWithLibraryMock<LibraryMissingOutput>();
    ASSERT_EQ(pipeline->execute(), StatusCode::NODE_LIBRARY_MISSING_OUTPUT);
}

struct LibraryIncorrectOutputPrecision {
    static int execute(const struct CustomNodeTensor*, int, struct CustomNodeTensor** handle, int* outputsNum, const struct CustomNodeParam*, int) {
        *handle = (struct CustomNodeTensor*)malloc(sizeof(struct CustomNodeTensor));
        *outputsNum = 1;
        (*handle)->name = "output_numbers";
        (*handle)->precision = CustomNodeTensorPrecision::UNSPECIFIED;
        (*handle)->dims = (uint64_t*)malloc(sizeof(uint64_t));
        (*handle)->dimsLength = 1;
        (*handle)->data = (uint8_t*)malloc(sizeof(uint8_t));
        (*handle)->dataLength = 1;
        return 0;
    }
    static int getInputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int getOutputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int release(void* ptr) {
        free(ptr);
        return 0;
    }
};

TEST_F(EnsembleFlowCustomNodePipelineExecutionTest, FailInCustomNodeOutputInvalidPrecision) {
    auto pipeline = this->prepareSingleNodePipelineWithLibraryMock<LibraryIncorrectOutputPrecision>();
    ASSERT_EQ(pipeline->execute(), StatusCode::NODE_LIBRARY_INVALID_PRECISION);
}

struct LibraryIncorrectOutputShape {
    static int execute(const struct CustomNodeTensor*, int, struct CustomNodeTensor** handle, int* outputsNum, const struct CustomNodeParam*, int) {
        *handle = (struct CustomNodeTensor*)malloc(sizeof(struct CustomNodeTensor));
        *outputsNum = 1;
        (*handle)->name = "output_numbers";
        (*handle)->precision = CustomNodeTensorPrecision::FP32;
        (*handle)->dims = nullptr;
        (*handle)->dimsLength = 0;
        (*handle)->data = (uint8_t*)malloc(sizeof(uint8_t));
        (*handle)->dataLength = 1;
        return 0;
    }
    static int getInputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int getOutputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int release(void* ptr) {
        free(ptr);
        return 0;
    }
};

TEST_F(EnsembleFlowCustomNodePipelineExecutionTest, FailInCustomNodeOutputInvalidShape) {
    auto pipeline = this->prepareSingleNodePipelineWithLibraryMock<LibraryIncorrectOutputShape>();
    ASSERT_EQ(pipeline->execute(), StatusCode::NODE_LIBRARY_INVALID_SHAPE);
}

struct LibraryIncorrectOutputContentSize {
    static int execute(const struct CustomNodeTensor*, int, struct CustomNodeTensor** handle, int* outputsNum, const struct CustomNodeParam*, int) {
        *handle = (struct CustomNodeTensor*)malloc(sizeof(struct CustomNodeTensor));
        *outputsNum = 1;
        (*handle)->name = "output_numbers";
        (*handle)->precision = CustomNodeTensorPrecision::FP32;
        (*handle)->dims = (uint64_t*)malloc(sizeof(uint64_t));
        (*handle)->dimsLength = 1;
        (*handle)->data = nullptr;
        (*handle)->dataLength = 0;
        return 0;
    }
    static int getInputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int getOutputsInfo(struct CustomNodeTensorInfo**, int*, const struct CustomNodeParam*, int) {
        return 0;
    }
    static int release(void* ptr) {
        free(ptr);
        return 0;
    }
};

TEST_F(EnsembleFlowCustomNodePipelineExecutionTest, FailInCustomNodeOutputInvalidContentSize) {
    auto pipeline = this->prepareSingleNodePipelineWithLibraryMock<LibraryIncorrectOutputContentSize>();
    ASSERT_EQ(pipeline->execute(), StatusCode::NODE_LIBRARY_INVALID_CONTENT_SIZE);
}

class EnsembleFlowCustomNodeFactoryCreateThenExecuteTest : public EnsembleFlowCustomNodePipelineExecutionTest {};

TEST_F(EnsembleFlowCustomNodeFactoryCreateThenExecuteTest, SimplePipelineFactoryCreationWithCustomNode) {
    // Nodes
    // request   custom    response
    //  O--------->O---------->O
    //          add-sub
    ConstructorEnabledModelManager manager;
    PipelineFactory factory;

    const std::vector<float> inputValues{7.8, -2.4, 1.9, 8.7, -2.4, 3.5};
    this->prepareRequest(inputValues);

    const float addValue = 0.9;
    const float subValue = 7.3;

    std::vector<NodeInfo> info{
        {NodeKind::ENTRY, ENTRY_NODE_NAME, "", std::nullopt, {{pipelineInputName, pipelineInputName}}},
        {NodeKind::CUSTOM, "custom_node", "", std::nullopt, {{customNodeOutputName, customNodeOutputName}},
            std::nullopt, {}, library, parameters_t{{"add_value", std::to_string(addValue)}, {"sub_value", std::to_string(subValue)}}},
        {NodeKind::EXIT, EXIT_NODE_NAME},
    };

    pipeline_connections_t connections;

    // request (pipelineInputName) O--------->O custom node (customNodeInputName)
    connections["custom_node"] = {
        {ENTRY_NODE_NAME, {{pipelineInputName, customNodeInputName}}}};

    // custom node (customNodeOutputName) O--------->O response (pipelineOutputName)
    connections[EXIT_NODE_NAME] = {
        {"custom_node", {{customNodeOutputName, pipelineOutputName}}}};

    std::unique_ptr<Pipeline> pipeline;
    ASSERT_EQ(factory.createDefinition("my_new_pipeline", info, connections, manager), StatusCode::OK);
    ASSERT_EQ(factory.create(pipeline, "my_new_pipeline", &request, &response, manager), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);

    this->checkResponse<float>(inputValues, [addValue, subValue](float value) -> float {
        return value + addValue - subValue;
    });
}

TEST_F(EnsembleFlowCustomNodeFactoryCreateThenExecuteTest, ParallelPipelineFactoryUsageWithCustomNode) {
    //                 Nodes
    //              custom_node_N
    //         v-------->O----------v
    //  request O--------->O---------->O response     x   PARALLEL_SIMULATED_REQUEST_COUNT
    //         ^-------->O----------^
    //                add-sub
    ConstructorEnabledModelManager manager;
    PipelineFactory factory;

    const int PARALLEL_CUSTOM_NODES = 3;
    const int PARALLEL_SIMULATED_REQUEST_COUNT = 30;

    const std::vector<float> inputValues{7.8, -2.4, 1.9, 8.7, -2.4, 3.5};
    std::array<PredictRequest, PARALLEL_SIMULATED_REQUEST_COUNT> requests{};

    for (int i = 0; i < PARALLEL_SIMULATED_REQUEST_COUNT; i++) {
        this->prepareRequest(requests[i], inputValues);
    }

    const std::array<float, PARALLEL_CUSTOM_NODES> addValues{-1.5, 1.4, -0.1};
    const std::array<float, PARALLEL_CUSTOM_NODES> subValues{4.9, -1.9, -0.9};

    std::vector<NodeInfo> info{
        {NodeKind::ENTRY, ENTRY_NODE_NAME, "", std::nullopt, {{pipelineInputName, pipelineInputName}}},
        {NodeKind::EXIT, EXIT_NODE_NAME},
    };

    for (int i = 0; i < PARALLEL_CUSTOM_NODES; i++) {
        info.emplace_back(std::move(NodeInfo(
            NodeKind::CUSTOM,
            "custom_node_" + std::to_string(i),
            "", std::nullopt,
            {{customNodeOutputName, customNodeOutputName}},
            std::nullopt, {},
            library, parameters_t{{"add_value", std::to_string(addValues[i])}, {"sub_value", std::to_string(subValues[i])}})));
    }

    pipeline_connections_t connections;

    for (int i = 0; i < PARALLEL_CUSTOM_NODES; i++) {
        // request (pipelineInputName) O--------->O custom_node_N (customNodeInputName)
        connections["custom_node_" + std::to_string(i)] = {
            {ENTRY_NODE_NAME, {{pipelineInputName, customNodeInputName}}}};
    }

    auto& responseConnections = connections[EXIT_NODE_NAME];
    for (int i = 0; i < PARALLEL_CUSTOM_NODES; i++) {
        responseConnections["custom_node_" + std::to_string(i)] =
            {{customNodeOutputName, "output_" + std::to_string(i)}};
    }

    std::unique_ptr<Pipeline> pipeline;
    ASSERT_EQ(factory.createDefinition("my_new_pipeline", info, connections, manager), StatusCode::OK);
    ASSERT_EQ(factory.create(pipeline, "my_new_pipeline", &requests[0], &response, manager), StatusCode::OK);

    auto run = [this, &requests, &manager, &factory, &inputValues, addValues, subValues, PARALLEL_CUSTOM_NODES](int i) {
        std::unique_ptr<Pipeline> pipeline;
        PredictResponse response_local;

        ASSERT_EQ(factory.create(pipeline, "my_new_pipeline", &requests[i], &response_local, manager), StatusCode::OK);
        ASSERT_EQ(pipeline->execute(), StatusCode::OK);

        for (int n = 0; n < PARALLEL_CUSTOM_NODES; n++) {
            this->checkResponse<float>("output_" + std::to_string(n), response_local, inputValues, [addValues, subValues, n](float value) -> float {
                return value + addValues[n] - subValues[n];
            });
        }
    };

    std::vector<std::promise<void>> promises(PARALLEL_SIMULATED_REQUEST_COUNT);
    std::vector<std::thread> threads;

    for (int n = 0; n < PARALLEL_SIMULATED_REQUEST_COUNT; n++) {
        threads.emplace_back(std::thread([&promises, n, &run]() {
            promises[n].get_future().get();
            run(n);
        }));
    }

    // Sleep to allow all threads to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for (auto& promise : promises) {
        promise.set_value();
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

static const char* pipelineCustomNodeConfig = R"(
{
    "model_config_list": [],
    "custom_node_library_config_list": [
        {
            "name": "lib_add_sub",
            "base_path": "/ovms/bazel-bin/src/lib_node_add_sub.so"
        }
    ],
    "pipeline_config_list": [
        {
            "name": "my_pipeline",
            "inputs": ["pipeline_input"],
            "nodes": [
                {
                    "name": "custom_node",
                    "library_name": "lib_add_sub",
                    "params": {
                        "add_value": "3.2",
                        "sub_value": "2.7"
                    },
                    "type": "custom",
                    "inputs": [
                        {"input_numbers": {"node_name": "request",
                                           "data_item": "pipeline_input"}}
                    ],
                    "outputs": [
                        {"data_item": "output_numbers",
                         "alias": "custom_node_output"}
                    ]
                }
            ],
            "outputs": [
                {"pipeline_output": {"node_name": "custom_node",
                                     "data_item": "custom_node_output"}
                }
            ]
        }
    ]
})";

class EnsembleFlowCustomNodeLoadConfigThenExecuteTest : public EnsembleFlowCustomNodePipelineExecutionTest {
protected:
    void SetUp() override {
        TestWithTempDir::SetUp();
        configJsonFilePath = directoryPath + "/ovms_config_file.json";
    }

    void loadCorrectConfiguration() {
        this->loadConfiguration(pipelineCustomNodeConfig);
    }

    void loadConfiguration(const char* configContent) {
        createConfigFileWithContent(configContent, configJsonFilePath);
        ASSERT_EQ(manager.loadConfig(configJsonFilePath), StatusCode::OK);
    }

    void checkResponseForCorrectConfiguration() {
        this->checkResponse<float>(inputValues, [](float value) -> float {
            return value + 3.2 - 2.7;
        });
    }

    std::string configJsonFilePath;
    const std::string pipelineName = "my_pipeline";
    ConstructorEnabledModelManager manager;
    const std::vector<float> inputValues{2.4, 9.3, -7.1};
};

TEST_F(EnsembleFlowCustomNodeLoadConfigThenExecuteTest, AddSubCustomNode) {
    std::unique_ptr<Pipeline> pipeline;
    this->prepareRequest(inputValues);
    this->loadCorrectConfiguration();
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);
    this->checkResponseForCorrectConfiguration();
}

static const char* pipelineCustomNodeReferenceMissingLibraryConfig = R"(
{
    "model_config_list": [],
    "custom_node_library_config_list": [
        {
            "name": "lib_add_sub",
            "base_path": "/ovms/bazel-bin/src/lib_node_add_sub.so"
        }
    ],
    "pipeline_config_list": [
        {
            "name": "my_pipeline",
            "inputs": ["pipeline_input"],
            "nodes": [
                {
                    "name": "custom_node",
                    "library_name": "non_existing_library",
                    "params": {
                        "add_value": "3.2",
                        "sub_value": "2.7"
                    },
                    "type": "custom",
                    "inputs": [
                        {"input_numbers": {"node_name": "request",
                                           "data_item": "pipeline_input"}}
                    ],
                    "outputs": [
                        {"data_item": "output_numbers",
                         "alias": "custom_node_output"}
                    ]
                }
            ],
            "outputs": [
                {"pipeline_output": {"node_name": "custom_node",
                                     "data_item": "custom_node_output"}
                }
            ]
        }
    ]
})";

TEST_F(EnsembleFlowCustomNodeLoadConfigThenExecuteTest, ReferenceMissingLibraryThenCorrect) {
    std::unique_ptr<Pipeline> pipeline;
    this->prepareRequest(inputValues);

    // Loading correct configuration is required for test to pass.
    // This is due to fact that when OVMS loads pipeline definition for the first time and fails, its status is RETIRED.
    this->loadCorrectConfiguration();
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);
    this->checkResponseForCorrectConfiguration();
    response.Clear();

    this->loadConfiguration(pipelineCustomNodeReferenceMissingLibraryConfig);
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::PIPELINE_DEFINITION_NOT_LOADED_YET);
    response.Clear();

    this->loadCorrectConfiguration();
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);
    this->checkResponseForCorrectConfiguration();
}

static const char* pipelineCustomNodeReferenceLibraryWithExecutionErrorLibraryConfig = R"(
{
    "model_config_list": [],
    "custom_node_library_config_list": [
        {
            "name": "lib_add_sub_new",
            "base_path": "/ovms/bazel-bin/src/lib_node_mock.so"
        }
    ],
    "pipeline_config_list": [
        {
            "name": "my_pipeline",
            "inputs": ["pipeline_input"],
            "nodes": [
                {
                    "name": "custom_node",
                    "library_name": "lib_add_sub_new",
                    "params": {
                        "add_value": "3.2",
                        "sub_value": "2.7"
                    },
                    "type": "custom",
                    "inputs": [
                        {"input_numbers": {"node_name": "request",
                                           "data_item": "pipeline_input"}}
                    ],
                    "outputs": [
                        {"data_item": "output_numbers",
                         "alias": "custom_node_output"}
                    ]
                }
            ],
            "outputs": [
                {"pipeline_output": {"node_name": "custom_node",
                                     "data_item": "custom_node_output"}
                }
            ]
        }
    ]
})";

TEST_F(EnsembleFlowCustomNodeLoadConfigThenExecuteTest, ReferenceLibraryWithExecutionErrorThenCorrect) {
    std::unique_ptr<Pipeline> pipeline;
    this->prepareRequest(inputValues);

    // Loading correct configuration is required for test to pass.
    // This is due to fact that when OVMS loads pipeline definition for the first time and fails, its status is RETIRED.
    this->loadCorrectConfiguration();
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);
    this->checkResponseForCorrectConfiguration();
    response.Clear();

    this->loadConfiguration(pipelineCustomNodeReferenceLibraryWithExecutionErrorLibraryConfig);
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::NODE_LIBRARY_EXECUTION_FAILED);
    response.Clear();

    this->loadCorrectConfiguration();
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);
    this->checkResponseForCorrectConfiguration();
}

static const char* pipelineCustomNodeMissingParametersConfig = R"(
{
    "model_config_list": [],
    "custom_node_library_config_list": [
        {
            "name": "lib_add_sub",
            "base_path": "/ovms/bazel-bin/src/lib_node_add_sub.so"
        }
    ],
    "pipeline_config_list": [
        {
            "name": "my_pipeline",
            "inputs": ["pipeline_input"],
            "nodes": [
                {
                    "name": "custom_node",
                    "library_name": "lib_add_sub",
                    "params": {
                        "random_parameter": "abcd"
                    },
                    "type": "custom",
                    "inputs": [
                        {"input_numbers": {"node_name": "request",
                                           "data_item": "pipeline_input"}}
                    ],
                    "outputs": [
                        {"data_item": "output_numbers",
                         "alias": "custom_node_output"}
                    ]
                }
            ],
            "outputs": [
                {"pipeline_output": {"node_name": "custom_node",
                                     "data_item": "custom_node_output"}
                }
            ]
        }
    ]
})";

TEST_F(EnsembleFlowCustomNodeLoadConfigThenExecuteTest, MissingRequiredNodeParametersThenCorrect) {
    std::unique_ptr<Pipeline> pipeline;
    this->prepareRequest(inputValues);

    // Loading correct configuration is required for test to pass.
    // This is due to fact that when OVMS loads pipeline definition for the first time and fails, its status is RETIRED.
    this->loadCorrectConfiguration();
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);
    this->checkResponseForCorrectConfiguration();
    response.Clear();

    this->loadConfiguration(pipelineCustomNodeMissingParametersConfig);
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::NODE_LIBRARY_EXECUTION_FAILED);
    response.Clear();

    this->loadCorrectConfiguration();
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);
    this->checkResponseForCorrectConfiguration();
}

static const char* pipelineCustomNodeLibraryNotEscapedPathConfig = R"(
{
    "model_config_list": [],
    "custom_node_library_config_list": [
        {
            "name": "lib_add_sub_new",
            "base_path": "/ovms/bazel-bin/src/../src/lib_node_add_sub.so"
        }
    ],
    "pipeline_config_list": [
        {
            "name": "my_pipeline",
            "inputs": ["pipeline_input"],
            "nodes": [
                {
                    "name": "custom_node",
                    "library_name": "lib_add_sub_new",
                    "params": {
                        "add_value": "3.2",
                        "sub_value": "2.7"
                    },
                    "type": "custom",
                    "inputs": [
                        {"input_numbers": {"node_name": "request",
                                           "data_item": "pipeline_input"}}
                    ],
                    "outputs": [
                        {"data_item": "output_numbers",
                         "alias": "custom_node_output"}
                    ]
                }
            ],
            "outputs": [
                {"pipeline_output": {"node_name": "custom_node",
                                     "data_item": "custom_node_output"}
                }
            ]
        }
    ]
})";

TEST_F(EnsembleFlowCustomNodeLoadConfigThenExecuteTest, ReferenceLibraryWithRestrictedBasePathThenCorrect) {
    std::unique_ptr<Pipeline> pipeline;
    this->prepareRequest(inputValues);

    // Loading correct configuration is required for test to pass.
    // This is due to fact that when OVMS loads pipeline definition for the first time and fails, its status is RETIRED.
    this->loadCorrectConfiguration();
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);
    this->checkResponseForCorrectConfiguration();
    response.Clear();

    this->loadConfiguration(pipelineCustomNodeLibraryNotEscapedPathConfig);
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::PIPELINE_DEFINITION_NOT_LOADED_YET);
    response.Clear();

    this->loadCorrectConfiguration();
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);
    this->checkResponseForCorrectConfiguration();
}

static const char* pipelineCustomNodeDifferentOperationsConfig = R"(
{
    "model_config_list": [],
    "custom_node_library_config_list": [
        {
            "name": "lib_perform_different_operations",
            "base_path": "/ovms/bazel-bin/src/lib_node_perform_different_operations.so"
        }
    ],
    "pipeline_config_list": [
        {
            "name": "my_pipeline",
            "inputs": ["pipeline_input", "pipeline_factors"],
            "nodes": [
                {
                    "name": "custom_node",
                    "library_name": "lib_perform_different_operations",
                    "type": "custom",
                    "inputs": [
                        {"input_numbers": {"node_name": "request",
                                           "data_item": "pipeline_input"}},
                        {"op_factors": {"node_name": "request",
                                           "data_item": "pipeline_factors"}}
                    ],
                    "outputs": [
                        {"data_item": "different_ops_results",
                         "alias": "custom_node_output"}
                    ]
                }
            ],
            "outputs": [
                {"pipeline_output": {"node_name": "custom_node",
                                     "data_item": "custom_node_output"}
                }
            ]
        }
    ]
})";

class EnsembleFlowCustomNodeAndDemultiplexerLoadConfigThenExecuteTest : public EnsembleFlowCustomNodeLoadConfigThenExecuteTest {
protected:
    void SetUp() override {
        EnsembleFlowCustomNodeLoadConfigThenExecuteTest::SetUp();
        configJsonFilePath = directoryPath + "/ovms_config_file.json";
    }
    const std::string differentOpsInputName = "pipeline_input";
    const std::string differentOpsFactorsName = "pipeline_factors";
};

enum OPS {
    ADD,
    SUB,
    MULTIPLY,
    DIVIDE
};

static void prepareDifferentOpsExpectedOutput(std::vector<float>& expectedOutput, const std::vector<float>& input, const std::vector<float>& factors) {
    for (size_t j = 0; j < 4; ++j) {  // iterate over ops
        for (size_t i = 0; i < DUMMY_MODEL_OUTPUT_SIZE; ++i) {
            size_t index = DUMMY_MODEL_OUTPUT_SIZE * j + i;
            switch (j) {
            case ADD:
                expectedOutput[index] = input[i] + factors[j];
                break;
            case SUB:
                expectedOutput[index] = input[i] - factors[j];
                break;
            case MULTIPLY:
                expectedOutput[index] = input[i] * factors[j];
                break;
            case DIVIDE:
                expectedOutput[index] = input[i] / factors[j];
                break;
            }
        }
    }
}

enum class Method {
    MAXIMUM_MAXIMUM,
    MAXIMUM_MINIMUM,
    MAXIMUM_AVERAGE,
};

std::vector<float> prepareGatherHighestExpectedOutput(std::vector<float> input, Method option) {
    std::vector<float> expectedOutput(DUMMY_MODEL_OUTPUT_SIZE);
    size_t tensorsCount = input.size() / DUMMY_MODEL_OUTPUT_SIZE;
    // perform operations
    std::vector<float> minimums(tensorsCount, std::numeric_limits<int>::max());
    std::vector<float> maximums(tensorsCount, std::numeric_limits<int>::lowest());
    std::vector<float> averages(tensorsCount, 0);
    for (size_t opId = 0; opId < tensorsCount; ++opId) {  // iterate over ops
        for (size_t i = 0; i < DUMMY_MODEL_OUTPUT_SIZE; ++i) {
            size_t index = DUMMY_MODEL_OUTPUT_SIZE * opId + i;
            switch (option) {
            case Method::MAXIMUM_MAXIMUM:
                maximums[opId] = std::max(maximums[opId], input[index]);
                break;
            case Method::MAXIMUM_MINIMUM:
                minimums[opId] = std::min(maximums[opId], input[index]);
                break;
            case Method::MAXIMUM_AVERAGE:
                averages[opId] += input[index];
                break;
            default:
                throw std::logic_error("");
                break;
            }
        }
        averages[opId] /= DUMMY_MODEL_OUTPUT_SIZE;
    }
    // choose tensor
    size_t whichTensor = 42;
    const std::vector<float>* fromWhichContainerToChoose = &maximums;
    switch (option) {
    case Method::MAXIMUM_MAXIMUM:
        fromWhichContainerToChoose = &maximums;
        break;
    case Method::MAXIMUM_MINIMUM:
        fromWhichContainerToChoose = &minimums;
        break;
    case Method::MAXIMUM_AVERAGE:
        fromWhichContainerToChoose = &averages;
        break;
    default:
        throw std::logic_error("");
    }
    whichTensor = std::distance(fromWhichContainerToChoose->begin(),
        std::max_element(fromWhichContainerToChoose->begin(),
            fromWhichContainerToChoose->end()));
    // copy tensor
    std::copy(input.begin() + DUMMY_MODEL_OUTPUT_SIZE * whichTensor,
        input.begin() + DUMMY_MODEL_OUTPUT_SIZE * (whichTensor + 1),
        expectedOutput.begin());
    return expectedOutput;
}

TEST_F(EnsembleFlowCustomNodeAndDemultiplexerLoadConfigThenExecuteTest, JustDifferentOpsCustomNode) {
    std::unique_ptr<Pipeline> pipeline;
    std::vector<float> input{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<float> factors{1, 3, 2, 2};  // add/sub/multiply/divide
    this->prepareRequest(request, input, differentOpsInputName);
    this->prepareRequest(request, factors, differentOpsFactorsName);
    this->loadConfiguration(pipelineCustomNodeDifferentOperationsConfig);
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);

    std::vector<float> expectedOutput(4 * DUMMY_MODEL_OUTPUT_SIZE);
    prepareDifferentOpsExpectedOutput(expectedOutput, input, factors);
    this->checkResponse("pipeline_output", response, expectedOutput, {1, 4, 10});
}

static const char* pipelineCustomNodeDifferentOperationsThenDummyConfig = R"(
{
    "custom_node_library_config_list": [
        {
            "name": "lib_perform_different_operations",
            "base_path": "/ovms/bazel-bin/src/lib_node_perform_different_operations.so"
        }
    ],
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy",
                "target_device": "CPU",
                "model_version_policy": {"all": {}},
                "nireq": 1
            }
        }
    ],
    "pipeline_config_list": [
        {
            "name": "my_pipeline",
            "inputs": ["pipeline_input", "pipeline_factors"],
            "nodes": [
                {
                    "name": "custom_node",
                    "library_name": "lib_perform_different_operations",
                    "type": "custom",
                    "demultiply_count": 4,
                    "inputs": [
                        {"input_numbers": {"node_name": "request",
                                           "data_item": "pipeline_input"}},
                        {"op_factors": {"node_name": "request",
                                           "data_item": "pipeline_factors"}}
                    ],
                    "outputs": [
                        {"data_item": "different_ops_results",
                         "alias": "custom_node_output"}
                    ]
                },
                {
                    "name": "dummyNode",
                    "model_name": "dummy",
                    "type": "DL model",
                    "inputs": [
                        {"b": {"node_name": "custom_node",
                               "data_item": "custom_node_output"}}
                    ],
                    "outputs": [
                        {"data_item": "a",
                         "alias": "dummy_output"}
                    ]
                }
            ],
            "outputs": [
                {"pipeline_output": {"node_name": "dummyNode",
                                     "data_item": "dummy_output"}
                }
            ]
        }
    ]
})";
TEST_F(EnsembleFlowCustomNodeAndDemultiplexerLoadConfigThenExecuteTest, DifferentOpsCustomNodeThenDummy) {
    std::unique_ptr<Pipeline> pipeline;
    std::vector<float> input{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<float> factors{1, 3, 2, 2};  // add/sub/multiply/divide
    this->prepareRequest(request, input, differentOpsInputName);
    this->prepareRequest(request, factors, differentOpsFactorsName);
    this->loadConfiguration(pipelineCustomNodeDifferentOperationsThenDummyConfig);
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);

    std::vector<float> expectedOutput(4 * DUMMY_MODEL_OUTPUT_SIZE);
    prepareDifferentOpsExpectedOutput(expectedOutput, input, factors);
    std::transform(expectedOutput.begin(), expectedOutput.end(), expectedOutput.begin(),
        [](float f) -> float { return f + 1; });
    this->checkResponse("pipeline_output", response, expectedOutput, {1, 4, 10});
}

static const char* pipelineCustomNodeDifferentOperationsThenDummyThenChooseMaximumConfig = R"(
{
    "custom_node_library_config_list": [
        {
            "name": "lib_perform_different_operations",
            "base_path": "/ovms/bazel-bin/src/lib_node_perform_different_operations.so"
        },
        {
            "name": "lib_choose_maximum",
            "base_path": "/ovms/bazel-bin/src/lib_node_choose_maximum.so"
        }
    ],
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy",
                "target_device": "CPU",
                "model_version_policy": {"all": {}},
                "nireq": 1
            }
        }
    ],
    "pipeline_config_list": [
        {
            "name": "my_pipeline",
            "inputs": ["pipeline_input", "pipeline_factors"],
            "nodes": [
                {
                    "name": "custom_node",
                    "library_name": "lib_perform_different_operations",
                    "type": "custom",
                    "demultiply_count": 4,
                    "inputs": [
                        {"input_numbers": {"node_name": "request",
                                           "data_item": "pipeline_input"}},
                        {"op_factors": {"node_name": "request",
                                           "data_item": "pipeline_factors"}}
                    ],
                    "outputs": [
                        {"data_item": "different_ops_results",
                         "alias": "custom_node_output"}
                    ]
                },
                {
                    "name": "dummyNode",
                    "model_name": "dummy",
                    "type": "DL model",
                    "inputs": [
                        {"b": {"node_name": "custom_node",
                               "data_item": "custom_node_output"}}
                    ],
                    "outputs": [
                        {"data_item": "a",
                         "alias": "dummy_output"}
                    ]
                },
                {
                    "name": "choose_max",
                    "library_name": "lib_choose_maximum",
                    "type": "custom",
                    "gather_from_node": "custom_node",
                    "params": {
                        "selection_criteria": "MAXIMUM_MINIMUM"
                    },
                    "inputs": [
                        {"input_tensors": {"node_name": "dummyNode",
                                           "data_item": "dummy_output"}}
                    ],
                    "outputs": [
                        {"data_item": "maximum_tensor",
                         "alias": "maximum_tensor_alias"}
                    ]
                }
            ],
            "outputs": [
                {"pipeline_output": {"node_name": "choose_max",
                                     "data_item": "maximum_tensor_alias"}
                }
            ]
        }
    ]
})";

TEST_F(EnsembleFlowCustomNodeAndDemultiplexerLoadConfigThenExecuteTest, DifferentOpsCustomNodeThenDummyThenChooseMaximum) {
    std::unique_ptr<Pipeline> pipeline;
    std::vector<float> input{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<float> factors{1, 3, 2, 2};  // add/sub/multiply/divide
    this->prepareRequest(request, input, differentOpsInputName);
    this->prepareRequest(request, factors, differentOpsFactorsName);
    this->loadConfiguration(pipelineCustomNodeDifferentOperationsThenDummyThenChooseMaximumConfig);
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);

    std::vector<float> expectedOutput(4 * DUMMY_MODEL_OUTPUT_SIZE);
    prepareDifferentOpsExpectedOutput(expectedOutput, input, factors);
    std::transform(expectedOutput.begin(), expectedOutput.end(), expectedOutput.begin(),
        [](float f) -> float { return f + 1; });
    std::vector<float> expectedResult = prepareGatherHighestExpectedOutput(expectedOutput, Method::MAXIMUM_MINIMUM);
    this->checkResponse("pipeline_output", response, expectedResult, {1, 10});
}

static const char* pipelineCustomNodeDifferentOperationsThenDummyThenChooseMaximumThenDummyConfig = R"(
{
    "custom_node_library_config_list": [
        {
            "name": "lib_perform_different_operations",
            "base_path": "/ovms/bazel-bin/src/lib_node_perform_different_operations.so"
        },
        {
            "name": "lib_choose_maximum",
            "base_path": "/ovms/bazel-bin/src/lib_node_choose_maximum.so"
        }
    ],
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy",
                "target_device": "CPU",
                "model_version_policy": {"all": {}},
                "nireq": 1
            }
        }
    ],
    "pipeline_config_list": [
        {
            "name": "my_pipeline",
            "inputs": ["pipeline_input", "pipeline_factors"],
            "nodes": [
                {
                    "name": "custom_node",
                    "library_name": "lib_perform_different_operations",
                    "type": "custom",
                    "demultiply_count": 4,
                    "inputs": [
                        {"input_numbers": {"node_name": "request",
                                           "data_item": "pipeline_input"}},
                        {"op_factors": {"node_name": "request",
                                           "data_item": "pipeline_factors"}}
                    ],
                    "outputs": [
                        {"data_item": "different_ops_results",
                         "alias": "custom_node_output"}
                    ]
                },
                {
                    "name": "dummyNode",
                    "model_name": "dummy",
                    "type": "DL model",
                    "inputs": [
                        {"b": {"node_name": "custom_node",
                               "data_item": "custom_node_output"}}
                    ],
                    "outputs": [
                        {"data_item": "a",
                         "alias": "dummy_output"}
                    ]
                },
                {
                    "name": "choose_max",
                    "library_name": "lib_choose_maximum",
                    "type": "custom",
                    "gather_from_node": "custom_node",
                    "params": {
                        "selection_criteria": "MAXIMUM_MAXIMUM"
                    },
                    "inputs": [
                        {"input_tensors": {"node_name": "dummyNode",
                                           "data_item": "dummy_output"}}
                    ],
                    "outputs": [
                        {"data_item": "maximum_tensor",
                         "alias": "maximum_tensor_alias"}
                    ]
                },
                {
                    "name": "dummyNode2",
                    "model_name": "dummy",
                    "type": "DL model",
                    "inputs": [
                        {"b": {"node_name": "choose_max",
                               "data_item": "maximum_tensor_alias"}}
                    ],
                    "outputs": [
                        {"data_item": "a",
                         "alias": "dummy_output"}
                    ]
                }
            ],
            "outputs": [
                {"pipeline_output": {"node_name": "dummyNode2",
                                     "data_item": "dummy_output"}
                }
            ]
        }
    ]
})";
TEST_F(EnsembleFlowCustomNodeAndDemultiplexerLoadConfigThenExecuteTest, DifferentOpsCustomNodeThenDummyThenChooseMaximumThenDummyAgain) {
    std::unique_ptr<Pipeline> pipeline;
    std::vector<float> input{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<float> factors{1, 3, 2, 2};  // add/sub/multiply/divide
    this->prepareRequest(request, input, differentOpsInputName);
    this->prepareRequest(request, factors, differentOpsFactorsName);
    this->loadConfiguration(pipelineCustomNodeDifferentOperationsThenDummyThenChooseMaximumThenDummyConfig);
    ASSERT_EQ(manager.createPipeline(pipeline, pipelineName, &request, &response), StatusCode::OK);
    ASSERT_EQ(pipeline->execute(), StatusCode::OK);

    std::vector<float> expectedOutput(4 * DUMMY_MODEL_OUTPUT_SIZE);
    prepareDifferentOpsExpectedOutput(expectedOutput, input, factors);
    std::transform(expectedOutput.begin(), expectedOutput.end(), expectedOutput.begin(),
        [](float f) -> float { return f + 1; });
    std::vector<float> expectedResult = prepareGatherHighestExpectedOutput(expectedOutput, Method::MAXIMUM_MAXIMUM);
    std::transform(expectedResult.begin(), expectedResult.end(), expectedResult.begin(),
        [](float f) -> float { return f + 1; });
    this->checkResponse("pipeline_output", response, expectedResult, {1, 10});
}
