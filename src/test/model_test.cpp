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
#include <deque>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../model.hpp"
#include "test_utils.hpp"
#include "mockmodelinstancechangingstates.hpp"

class ModelDefaultVersions : public ::testing::Test {};

TEST_F(ModelDefaultVersions, DefaultVersionNullWhenNoVersionAdded) {
    MockModelWithInstancesJustChangingStates mockModel;
    std::shared_ptr<ovms::ModelInstance> defaultInstance;
    defaultInstance = mockModel.getDefaultModelInstance();
    EXPECT_EQ(nullptr, defaultInstance);
}

TEST_F(ModelDefaultVersions, DefaultVersionNullWhenVersionRetired) {
    MockModelWithInstancesJustChangingStates mockModel;
    std::shared_ptr<ovms::model_versions_t> versionsToChange = std::make_shared<ovms::model_versions_t>();
    versionsToChange->push_back(1);
    ovms::ModelConfig config = DUMMY_MODEL_CONFIG;
    mockModel.addVersions(versionsToChange, config);
    mockModel.retireVersions(versionsToChange);

    std::shared_ptr<ovms::ModelInstance> defaultInstance;
    defaultInstance = mockModel.getDefaultModelInstance();
    EXPECT_EQ(nullptr, defaultInstance);
}

TEST_F(ModelDefaultVersions, DefaultVersionShouldReturnValidWhen1Added) {
    MockModelWithInstancesJustChangingStates mockModel;
    std::shared_ptr<ovms::model_versions_t> versionsToChange = std::make_shared<ovms::model_versions_t>();
    versionsToChange->push_back(1);
    ovms::ModelConfig config = DUMMY_MODEL_CONFIG;
    ASSERT_EQ(mockModel.addVersions(versionsToChange, config), ovms::StatusCode::OK);

    std::shared_ptr<ovms::ModelInstance> defaultInstance;
    defaultInstance = mockModel.getDefaultModelInstance();
    EXPECT_TRUE(nullptr != defaultInstance);
    EXPECT_EQ(1, defaultInstance->getVersion());
}

TEST_F(ModelDefaultVersions, DefaultVersionShouldReturnHighest) {
    MockModelWithInstancesJustChangingStates mockModel;
    std::shared_ptr<ovms::model_versions_t> versionsToChange = std::make_shared<ovms::model_versions_t>();
    versionsToChange->push_back(1);
    ovms::ModelConfig config = DUMMY_MODEL_CONFIG;
    ASSERT_EQ(mockModel.addVersions(versionsToChange, config), ovms::StatusCode::OK);
    versionsToChange->clear();
    versionsToChange->push_back(2);
    config.setVersion(2);
    ASSERT_EQ(mockModel.addVersions(versionsToChange, config), ovms::StatusCode::OK);

    std::shared_ptr<ovms::ModelInstance> defaultInstance;
    defaultInstance = mockModel.getDefaultModelInstance();
    EXPECT_TRUE(nullptr != defaultInstance);
    EXPECT_EQ(2, defaultInstance->getVersion());
}

TEST_F(ModelDefaultVersions, DefaultVersionShouldReturnHighestNonRetired) {
    MockModelWithInstancesJustChangingStates mockModel;
    std::shared_ptr<ovms::model_versions_t> versionsToChange = std::make_shared<ovms::model_versions_t>();
    versionsToChange->push_back(1);
    ovms::ModelConfig config = DUMMY_MODEL_CONFIG;
    ASSERT_EQ(mockModel.addVersions(versionsToChange, config), ovms::StatusCode::OK);
    versionsToChange->clear();

    versionsToChange->push_back(2);
    config.setVersion(2);
    ASSERT_EQ(mockModel.addVersions(versionsToChange, config), ovms::StatusCode::OK);
    versionsToChange->clear();

    versionsToChange->push_back(2);
    mockModel.retireVersions(versionsToChange);
    versionsToChange->clear();

    std::shared_ptr<ovms::ModelInstance> defaultInstance;
    defaultInstance = mockModel.getDefaultModelInstance();
    EXPECT_TRUE(nullptr != defaultInstance);
    EXPECT_EQ(1, defaultInstance->getVersion());
}

TEST_F(ModelDefaultVersions, DefaultVersionShouldReturnHighestWhenVersionReloaded) {
    MockModelWithInstancesJustChangingStates mockModel;
    std::shared_ptr<ovms::model_versions_t> versionsToChange = std::make_shared<ovms::model_versions_t>();
    versionsToChange->push_back(1);
    ovms::ModelConfig config = DUMMY_MODEL_CONFIG;
    ASSERT_EQ(mockModel.addVersions(versionsToChange, config), ovms::StatusCode::OK);
    versionsToChange->clear();

    versionsToChange->push_back(2);
    config.setVersion(2);
    ASSERT_EQ(mockModel.addVersions(versionsToChange, config), ovms::StatusCode::OK);
    versionsToChange->clear();

    versionsToChange->push_back(2);
    mockModel.retireVersions(versionsToChange);
    versionsToChange->clear();

    versionsToChange->push_back(2);
    config.setVersion(2);
    ASSERT_EQ(mockModel.reloadVersions(versionsToChange, config), ovms::StatusCode::OK);
    versionsToChange->clear();

    std::shared_ptr<ovms::ModelInstance> defaultInstance;
    defaultInstance = mockModel.getDefaultModelInstance();
    EXPECT_TRUE(nullptr != defaultInstance);
    EXPECT_EQ(2, defaultInstance->getVersion());
}