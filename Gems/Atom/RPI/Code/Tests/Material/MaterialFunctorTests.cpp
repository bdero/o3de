/*
 * Copyright (c) Contributors to the Open 3D Engine Project. For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * 
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzTest/AzTest.h>
#include <Common/RPITestFixture.h>
#include <Common/JsonTestUtils.h>
#include <Common/ErrorMessageFinder.h>
#include <Common/ShaderAssetTestUtils.h>
#include <Atom/RPI.Reflect/Material/MaterialFunctor.h>
#include <Atom/RPI.Reflect/Material/MaterialTypeAsset.h>
#include <Atom/RPI.Reflect/Material/MaterialTypeAssetCreator.h>
#include <Atom/RPI.Reflect/Material/MaterialAssetCreator.h>
#include <Atom/RPI.Edit/Material/MaterialFunctorSourceData.h>
#include <Atom/RPI.Public/Material/Material.h>
#include <Material/MaterialAssetTestUtils.h>

//#include <AtomCore/Serialization/Json/JsonUtils.h>
namespace UnitTest
{
    using namespace AZ;
    using namespace RPI;

    class MaterialFunctorTests
        : public RPITestFixture
    {
    public:
        class SetShaderOptionFunctor final
            : public MaterialFunctor
        {
        public:
            AZ_RTTI(SetShaderOptionFunctor, "{6316D98D-D2DD-4E9C-808C-58118DC9FF73}", MaterialFunctor);

            SetShaderOptionFunctor(size_t shaderIndex, ShaderOptionIndex shaderOptionIndex, ShaderOptionValue shaderOptionValue)
                : m_shaderIndex(shaderIndex)
                , m_shaderOptionIndex(shaderOptionIndex)
                , m_shaderOptionValue(shaderOptionValue)
            {
            }

            void Process(MaterialFunctor::RuntimeContext& context) override
            {
                m_processResult = context.SetShaderOptionValue(0, m_shaderOptionIndex, m_shaderOptionValue);
            }

            // Note a real functor wouldn't do this, it's just for testing
            bool GetProcessResult()
            {
                return m_processResult;
            }

        private:
            size_t m_shaderIndex;
            ShaderOptionIndex m_shaderOptionIndex;
            ShaderOptionValue m_shaderOptionValue;
            bool m_processResult = false;
        };

        class PropertyDependencyTestFunctor final
            : public MaterialFunctor
        {
        public:
            MOCK_METHOD0(ProcessCalled, void());

            void Process(RuntimeContext& context) override
            {
                ProcessCalled();

                context.GetMaterialPropertyValue<int32_t>(m_registedPropertyIndex);
                context.GetMaterialPropertyValue<int32_t>(m_registedPropertyName);
                // Should report error in the call.
                context.GetMaterialPropertyValue<int32_t>(m_unregistedPropertyIndex);
                context.GetMaterialPropertyValue<int32_t>(m_unregistedPropertyName);
            }

            MaterialPropertyIndex m_registedPropertyIndex;
            MaterialPropertyIndex m_unregistedPropertyIndex;

            AZ::Name m_registedPropertyName;
            AZ::Name m_unregistedPropertyName;
        };

        class PropertyDependencyTestFunctorSourceData final
            : public MaterialFunctorSourceData
        {
        public:
            FunctorResult CreateFunctor(const RuntimeContext& context) const override
            {
                Ptr<PropertyDependencyTestFunctor> functor = aznew PropertyDependencyTestFunctor;

                functor->m_registedPropertyIndex = context.FindMaterialPropertyIndex(Name{ m_registedPropertyName });
                EXPECT_TRUE(!functor->m_registedPropertyIndex.IsNull());
                AddMaterialPropertyDependency(functor, functor->m_registedPropertyIndex);

                functor->m_unregistedPropertyIndex = context.FindMaterialPropertyIndex(Name{ m_unregistedPropertyName });
                EXPECT_TRUE(!functor->m_unregistedPropertyIndex.IsNull());
                // Intended missing registration to m_materialPropertyDependencies

                functor->m_registedPropertyName = m_registedPropertyName;
                functor->m_unregistedPropertyName = m_unregistedPropertyName;

                return Success(Ptr<MaterialFunctor>(functor));
            }
            AZStd::string m_registedPropertyName;
            AZStd::string m_unregistedPropertyName;
        };

    protected:
        void SetUp() override
        {
            RPITestFixture::SetUp();
        }

        void TearDown() override
        {
            RPITestFixture::TearDown();
        }
    };

    TEST_F(MaterialFunctorTests, MaterialFunctor_RuntimeContext_ShaderOptionNotOwned)
    {
        using namespace AZ::RPI;

        AZStd::vector<RPI::ShaderOptionValuePair> boolOptionValues;
        boolOptionValues.push_back({Name("False"),  RPI::ShaderOptionValue(0)});
        boolOptionValues.push_back({Name("True"), RPI::ShaderOptionValue(1)});

        AZ::RPI::Ptr<AZ::RPI::ShaderOptionGroupLayout> shaderOptions = RPI::ShaderOptionGroupLayout::Create();
        shaderOptions->AddShaderOption(ShaderOptionDescriptor{Name{"o_optionA"}, ShaderOptionType::Boolean, 0, 0, boolOptionValues, Name{"False"}});
        shaderOptions->AddShaderOption(ShaderOptionDescriptor{Name{"o_optionB"}, ShaderOptionType::Boolean, 1, 1, boolOptionValues, Name{"False"}});
        shaderOptions->AddShaderOption(ShaderOptionDescriptor{Name{"o_optionC"}, ShaderOptionType::Boolean, 2, 2, boolOptionValues, Name{"False"}});
        shaderOptions->Finalize();

        Data::Asset<MaterialTypeAsset> materialTypeAsset;
        //Data::Asset<MaterialAsset> materialAsset;

        // Note we don't actually need any properties or functors in the material type. We just need to set up some sample data
        // structures that we can pass to the functors below, especially the shader with shader options.
        MaterialTypeAssetCreator materialTypeCreator;
        materialTypeCreator.Begin(Uuid::CreateRandom());
        materialTypeCreator.AddShader(CreateTestShaderAsset(Uuid::CreateRandom(), CreateCommonTestMaterialSrgLayout(), shaderOptions));
        // We claim ownership of options A and B, but not C. So C is a globally accessible option, not owned by the material.
        materialTypeCreator.ClaimShaderOptionOwnership(Name{"o_optionA"});
        materialTypeCreator.ClaimShaderOptionOwnership(Name{"o_optionB"});
        EXPECT_TRUE(materialTypeCreator.End(materialTypeAsset));

        SetShaderOptionFunctor testFunctorSetOptionA{0, ShaderOptionIndex{0}, ShaderOptionValue{1}};
        SetShaderOptionFunctor testFunctorSetOptionB{0, ShaderOptionIndex{1}, ShaderOptionValue{1}};
        SetShaderOptionFunctor testFunctorSetOptionC{0, ShaderOptionIndex{2}, ShaderOptionValue{1}};
        SetShaderOptionFunctor testFunctorSetOptionInvalid{0, ShaderOptionIndex{3}, ShaderOptionValue{1}};


        // Most of this data can be empty since this particular functor doesn't access it.
        AZStd::vector<MaterialPropertyValue> unusedPropertyValues;
        ShaderResourceGroup* unusedSrg = nullptr;
        ShaderCollection shaderCollectionCopy = materialTypeAsset->GetShaderCollection();

        {
            // Successfully set o_optionA
            MaterialFunctor::RuntimeContext runtimeContext = MaterialFunctor::RuntimeContext{
                unusedPropertyValues,
                materialTypeAsset->GetMaterialPropertiesLayout(),
                &shaderCollectionCopy,
                unusedSrg,
                &testFunctorSetOptionA.GetMaterialPropertyDependencies()
            };
            testFunctorSetOptionA.Process(runtimeContext);
            EXPECT_TRUE(testFunctorSetOptionA.GetProcessResult());
            EXPECT_EQ(1, shaderCollectionCopy[0].GetShaderOptions()->GetValue(ShaderOptionIndex{ 0 }).GetIndex());
            EXPECT_NE(1, shaderCollectionCopy[0].GetShaderOptions()->GetValue(ShaderOptionIndex{ 1 }).GetIndex());
            EXPECT_NE(1, shaderCollectionCopy[0].GetShaderOptions()->GetValue(ShaderOptionIndex{ 2 }).GetIndex());
        }

        {
            // Successfully set o_optionB
            MaterialFunctor::RuntimeContext runtimeContext = MaterialFunctor::RuntimeContext{
                unusedPropertyValues,
                materialTypeAsset->GetMaterialPropertiesLayout(),
                &shaderCollectionCopy,
                unusedSrg,
                &testFunctorSetOptionB.GetMaterialPropertyDependencies()
            };
            testFunctorSetOptionB.Process(runtimeContext);
            EXPECT_TRUE(testFunctorSetOptionB.GetProcessResult());
            EXPECT_EQ(1, shaderCollectionCopy[0].GetShaderOptions()->GetValue(ShaderOptionIndex{ 0 }).GetIndex());
            EXPECT_EQ(1, shaderCollectionCopy[0].GetShaderOptions()->GetValue(ShaderOptionIndex{ 1 }).GetIndex());
            EXPECT_NE(1, shaderCollectionCopy[0].GetShaderOptions()->GetValue(ShaderOptionIndex{ 2 }).GetIndex());
        }

        {
            // Fail to set o_optionC because it is not owned by the material type
            AZ_TEST_START_TRACE_SUPPRESSION;
            MaterialFunctor::RuntimeContext runtimeContext = MaterialFunctor::RuntimeContext{
                unusedPropertyValues,
                materialTypeAsset->GetMaterialPropertiesLayout(),
                &shaderCollectionCopy,
                unusedSrg,
                &testFunctorSetOptionC.GetMaterialPropertyDependencies()
            };
            testFunctorSetOptionC.Process(runtimeContext);
            EXPECT_FALSE(testFunctorSetOptionC.GetProcessResult());
            AZ_TEST_STOP_TRACE_SUPPRESSION(1);
        }

        {
            // Fail to set option index that is out of range
            AZ_TEST_START_TRACE_SUPPRESSION;
            MaterialFunctor::RuntimeContext runtimeContext = MaterialFunctor::RuntimeContext{
                unusedPropertyValues,
                materialTypeAsset->GetMaterialPropertiesLayout(),
                &shaderCollectionCopy,
                unusedSrg,
                &testFunctorSetOptionInvalid.GetMaterialPropertyDependencies()
            };
            testFunctorSetOptionInvalid.Process(runtimeContext);
            EXPECT_FALSE(testFunctorSetOptionInvalid.GetProcessResult());
            AZ_TEST_STOP_TRACE_SUPPRESSION(1);
        }

        EXPECT_EQ(1, shaderCollectionCopy[0].GetShaderOptions()->GetValue(ShaderOptionIndex{0}).GetIndex());
        EXPECT_EQ(1, shaderCollectionCopy[0].GetShaderOptions()->GetValue(ShaderOptionIndex{1}).GetIndex());
        EXPECT_NE(1, shaderCollectionCopy[0].GetShaderOptions()->GetValue(ShaderOptionIndex{2}).GetIndex());
    }

    TEST_F(MaterialFunctorTests, ReprocessTest)
    {
        Data::Asset<MaterialTypeAsset> m_testMaterialTypeAsset;
        Data::Asset<MaterialAsset> m_testMaterialAsset;

        AZ::Name registedPropertyName("PropA");
        AZ::Name unregistedPropertyName("PropB");
        AZ::Name unrelatedPropertyName("PropC");

        MaterialTypeAssetCreator materialTypeCreator;
        materialTypeCreator.Begin(Uuid::CreateRandom());

        materialTypeCreator.BeginMaterialProperty(registedPropertyName, AZ::RPI::MaterialPropertyDataType::Int);
        materialTypeCreator.EndMaterialProperty();
        materialTypeCreator.BeginMaterialProperty(unregistedPropertyName, AZ::RPI::MaterialPropertyDataType::Int);
        materialTypeCreator.EndMaterialProperty();
        materialTypeCreator.BeginMaterialProperty(unrelatedPropertyName, AZ::RPI::MaterialPropertyDataType::Int);
        materialTypeCreator.EndMaterialProperty();

        materialTypeCreator.SetPropertyValue(registedPropertyName, 42);
        materialTypeCreator.SetPropertyValue(unregistedPropertyName, 42);
        materialTypeCreator.SetPropertyValue(unrelatedPropertyName, 42);

        PropertyDependencyTestFunctorSourceData functorSourceData;
        functorSourceData.m_registedPropertyName = registedPropertyName.GetStringView();
        functorSourceData.m_unregistedPropertyName = unregistedPropertyName.GetStringView();

        MaterialFunctorSourceData::FunctorResult result = functorSourceData.CreateFunctor(
            MaterialFunctorSourceData::RuntimeContext(
                "Dummy.materialtype",
                materialTypeCreator.GetMaterialPropertiesLayout(),
                materialTypeCreator.GetMaterialShaderResourceGroupLayout(),
                materialTypeCreator.GetShaderCollection()
            )
        );

        EXPECT_TRUE(result.IsSuccess());
        Ptr<MaterialFunctor>& functor = result.GetValue();
        EXPECT_TRUE(functor != nullptr);
        materialTypeCreator.AddMaterialFunctor(functor);
        materialTypeCreator.End(m_testMaterialTypeAsset);

        MaterialAssetCreator materialCreator;
        materialCreator.Begin(Uuid::CreateRandom(), *m_testMaterialTypeAsset);
        materialCreator.SetPropertyValue(registedPropertyName, 42);
        materialCreator.SetPropertyValue(unregistedPropertyName, 42);
        materialCreator.SetPropertyValue(unrelatedPropertyName, 42);
        materialCreator.End(m_testMaterialAsset);

        EXPECT_TRUE(m_testMaterialAsset->GetMaterialFunctors().size() == 1u);
        PropertyDependencyTestFunctor* testFunctor = static_cast<PropertyDependencyTestFunctor*>(m_testMaterialAsset->GetMaterialFunctors()[0].get());

        ErrorMessageFinder errorMessageFinder;

        // Expect creation will call functor process once.
        EXPECT_CALL(*testFunctor, ProcessCalled()).Times(1);
        // Suppress 1 error as we know an unregistered dependent property will be accessed.
        errorMessageFinder.Reset();
        errorMessageFinder.AddExpectedErrorMessage("Material functor accessing an unregistered material property", 2);
        Data::Instance<Material> material = Material::FindOrCreate(m_testMaterialAsset);
        errorMessageFinder.CheckExpectedErrorsFound();

        material->SetPropertyValue(material->FindPropertyIndex(registedPropertyName), int32_t(24));

        // Expect dependent property change will call functor process once.
        EXPECT_CALL(*testFunctor, ProcessCalled()).Times(1);
        // Suppress 1 error as we know an unregistered dependent property will be accessed.
        errorMessageFinder.Reset();
        errorMessageFinder.AddExpectedErrorMessage("Material functor accessing an unregistered material property", 2);
        material->Compile();
        errorMessageFinder.CheckExpectedErrorsFound();

        // Expect unrelated property change won't call functor process.
        material->SetPropertyValue(material->FindPropertyIndex(unrelatedPropertyName), int32_t(24));

        EXPECT_CALL(*testFunctor, ProcessCalled()).Times(0);
        material->Compile();

        m_testMaterialTypeAsset = {};
        m_testMaterialAsset = {};
    }
}
