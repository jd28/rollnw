#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/profiles/nwn1/gff_propset_component_json.hpp>
#include <nw/profiles/nwn1/propset_gff_policy.hpp>
#include <nw/smalls/runtime.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

class Nwn1GffPropsetComponentJson : public ::testing::Test {
protected:
    void SetUp() override
    {
        owns_services_ = nw::kernel::services().get<nw::smalls::Runtime>() == nullptr;
        if (owns_services_) {
            nw::kernel::services().start();
        }
        nw::kernel::runtime().add_module_path(fs::path("stdlib/core"));
        nw::kernel::runtime().add_module_path(fs::path("stdlib/nwn1"));
        registry_ = std::make_unique<nwn1::PropsetGffPolicyRegistry>(
            nwn1::make_nwn1_propset_policy_registry());
    }

    void TearDown() override
    {
        registry_.reset();
        if (owns_services_) {
            nw::kernel::services().shutdown();
        }
    }

    bool owns_services_ = false;
    std::unique_ptr<nwn1::PropsetGffPolicyRegistry> registry_;
};

TEST_F(Nwn1GffPropsetComponentJson, ConvertsBlueprintGffToPropsetsAndComponentsOnly)
{
    auto& rt = nw::kernel::runtime();
    ASSERT_NE(rt.load_module("core.trigger"), nullptr);

    nlohmann::json out;
    auto result = nwn1::gff_file_to_propset_component_json(
        "test_data/user/development/pl_spray_sewage.utt",
        out,
        &rt,
        registry_.get(),
        nw::SerializationProfile::blueprint);

    ASSERT_TRUE(result) << result.error;
    EXPECT_EQ(result.object_type, nw::ObjectType::trigger);
    EXPECT_EQ(result.profile, nw::SerializationProfile::blueprint);
    EXPECT_FALSE(out.contains("$type"));
    EXPECT_FALSE(out.contains("$version"));
    EXPECT_TRUE(out.contains("object"));
    EXPECT_TRUE(out.contains("components"));
    EXPECT_TRUE(out.contains("core.trigger.TriggerState"));
    EXPECT_FALSE(out.contains("core.object.ObjectCommon"));
    EXPECT_FALSE(out.contains("scripts"));
    EXPECT_FALSE(out.contains("trap"));
    EXPECT_FALSE(out.contains("linked_to"));
    EXPECT_FALSE(out.contains("faction"));

    ASSERT_TRUE(out.at("components").contains("locals"));
    EXPECT_TRUE(out.at("components").at("locals").is_object());
    EXPECT_FALSE(out.at("components").contains("resref"));
    EXPECT_FALSE(out.at("components").contains("tag"));
    EXPECT_FALSE(out.at("components").contains("name"));
    EXPECT_TRUE(out.at("object").contains("resref"));
    EXPECT_TRUE(out.at("object").contains("tag"));
    EXPECT_TRUE(out.at("object").contains("name"));

    EXPECT_TRUE(out.at("core.trigger.TriggerState").contains("linked_to"));
    EXPECT_TRUE(out.at("core.trigger.TriggerState").contains("faction"));
}

TEST_F(Nwn1GffPropsetComponentJson, ConvertsInstanceGffSpatialComponent)
{
    auto& rt = nw::kernel::runtime();
    ASSERT_NE(rt.load_module("core.trigger"), nullptr);

    nlohmann::json out;
    auto result = nwn1::gff_file_to_propset_component_json(
        "test_data/user/development/pl_spray_sewage.utt",
        out,
        &rt,
        registry_.get(),
        nw::SerializationProfile::instance);

    ASSERT_TRUE(result) << result.error;
    EXPECT_EQ(result.object_type, nw::ObjectType::trigger);
    EXPECT_EQ(result.profile, nw::SerializationProfile::instance);
    EXPECT_FALSE(out.contains("$type"));
    EXPECT_FALSE(out.contains("$version"));

    ASSERT_TRUE(out.at("components").contains("location"));
    EXPECT_TRUE(out.at("components").at("location").is_object());
}

TEST_F(Nwn1GffPropsetComponentJson, MissingFileFails)
{
    auto& rt = nw::kernel::runtime();
    ASSERT_NE(rt.load_module("core.trigger"), nullptr);

    nlohmann::json out = nlohmann::json::object({{"sentinel", true}});
    auto result = nwn1::gff_file_to_propset_component_json(
        "test_data/user/development/does_not_exist.utt",
        out,
        &rt,
        registry_.get(),
        nw::SerializationProfile::blueprint);

    EXPECT_FALSE(result);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(out.is_null());
}
