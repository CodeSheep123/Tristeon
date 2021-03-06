﻿#include "Pipeline.h"
#include "Core/Rendering/ShaderFile.h"
#include "Data/TextFile.h"
#include "Misc/Console.h"
#include "Core/Rendering/Vulkan/InternalMeshRendererVulkan.h"
#include "Core/Rendering/Vulkan/MaterialVulkan.h"
#include "Data/Mesh.h"
#include "Core/BindingData.h"

#include <spirv_cross/spirv_cross.hpp>

namespace Tristeon
{
	namespace Core
	{
		namespace Rendering
		{
			namespace Vulkan
			{
				//Easier to read
				using VertexInputState = vk::PipelineVertexInputStateCreateInfo;
				using AssemblyInputState = vk::PipelineInputAssemblyStateCreateInfo;
				using ViewportState = vk::PipelineViewportStateCreateInfo;
				using RasterizationState = vk::PipelineRasterizationStateCreateInfo;
				using MultisampleState = vk::PipelineMultisampleStateCreateInfo;
				using DepthStencilState = vk::PipelineDepthStencilStateCreateInfo;
				using ColorBlendState = vk::PipelineColorBlendStateCreateInfo;
				using DynamicState = vk::PipelineDynamicStateCreateInfo;

				Pipeline::Pipeline(VulkanBindingData* bind, ShaderFile file, vk::Extent2D extent, vk::RenderPass renderPass, bool enableBuffers, vk::PrimitiveTopology topology) : device(bind->device)
				{
					//Store vars
					this->file = file;
					this->topology = topology;
					this->enableBuffers = enableBuffers;
					this->binding = bind;

					//Init
					createDescriptorLayout(file.getProps());
					create(extent, renderPass);
				}

				void Pipeline::createDescriptorLayout(std::map<int, ShaderProperty> properties)
				{
					//Uniform buf
					vk::DescriptorSetLayoutBinding const ubo = vk::DescriptorSetLayoutBinding(
						0, vk::DescriptorType::eUniformBuffer,
						1, vk::ShaderStageFlagBits::eVertex,
						nullptr);
					vk::DescriptorSetLayoutCreateInfo ci = vk::DescriptorSetLayoutCreateInfo({}, 1, &ubo);
					device.createDescriptorSetLayout(&ci, nullptr, &descriptorSetLayout1);

					//Shortcut for conversion from custom enum to vulkan shaderstages
					static std::map<ShaderStage, vk::ShaderStageFlagBits> shaderStages = {
						{ ShaderStage::Vertex, vk::ShaderStageFlagBits::eVertex },
						{ ShaderStage::Fragment, vk::ShaderStageFlagBits::eFragment },
						{ ShaderStage::Geometry, vk::ShaderStageFlagBits::eGeometry },
						{ ShaderStage::Compute, vk::ShaderStageFlagBits::eCompute },
						{ ShaderStage::All, vk::ShaderStageFlagBits::eAll },
						{ ShaderStage::All_Graphics, vk::ShaderStageFlagBits::eAllGraphics }
					};

					//Get bindings based on the given property list
					std::vector<vk::DescriptorSetLayoutBinding> bindings;
					int i = 0;
					for (const auto pair : properties)
					{
						ShaderProperty const p = pair.second;

						switch (p.valueType)
						{
							//Image descriptor
							case DT_Image:
							{
								vk::DescriptorSetLayoutBinding const sampler = vk::DescriptorSetLayoutBinding(
									i, vk::DescriptorType::eCombinedImageSampler,
									1, shaderStages[p.shaderStage],
									nullptr);
								bindings.push_back(sampler);
								break;
							}
							//All the others just get a buffer
							default:
							{
								vk::DescriptorSetLayoutBinding const buf = vk::DescriptorSetLayoutBinding(
									i, vk::DescriptorType::eUniformBuffer,
									1, shaderStages[p.shaderStage],
									nullptr);
								bindings.push_back(buf);
								break;
							}
						}
						i++;
					}
					vk::DescriptorSetLayoutCreateInfo ci2 = vk::DescriptorSetLayoutCreateInfo({}, bindings.size(), bindings.data());
					device.createDescriptorSetLayout(&ci2, nullptr, &descriptorSetLayout2);
				}

				vk::ShaderModule Pipeline::createShaderModule(const std::vector<char>& code, vk::Device device)
				{
					//Create a standard shader module with the given code
					const vk::ShaderModuleCreateInfo info = vk::ShaderModuleCreateInfo({}, code.size(), (const uint32_t*)code.data());
					return device.createShaderModule(info, nullptr);
				}

				Pipeline::~Pipeline()
				{
					//Destroy all resources allocated by pipeline
					device.destroyDescriptorSetLayout(descriptorSetLayout1);
					device.destroyDescriptorSetLayout(descriptorSetLayout2);
					cleanup();
				}

				void Pipeline::rebuild(vk::Extent2D extent, vk::RenderPass renderPass)
				{
					//Cleanup and then build again
					cleanup();
					create(extent, renderPass);
				}

				void Pipeline::create(vk::Extent2D extent, vk::RenderPass renderPass)
				{
					//Load shaders and create modules
					Data::TextFile vertex = Data::TextFile(file.getPath(RAPI_Vulkan, ST_Vertex), Data::FileMode::FM_Binary);
					Data::TextFile fragment = Data::TextFile(file.getPath(RAPI_Vulkan, ST_Fragment), Data::FileMode::FM_Binary);
					vertexShader = createShaderModule(vertex.readAllVector(), device);
					fragmentShader = createShaderModule(fragment.readAllVector(), device);

					//Create shader stage 
					const vk::PipelineShaderStageCreateInfo vert = vk::PipelineShaderStageCreateInfo(
					{}, vk::ShaderStageFlagBits::eVertex,
						vertexShader, "main",
						nullptr);

					const vk::PipelineShaderStageCreateInfo frag = vk::PipelineShaderStageCreateInfo(
					{}, vk::ShaderStageFlagBits::eFragment,
						fragmentShader, "main",
						nullptr);
					vk::PipelineShaderStageCreateInfo shaderStages[] = { vert, frag };

					//Get vertex input data
					auto binding = getBindingDescription();
					auto attributes = getAttributeDescription();
					VertexInputState vertexState = VertexInputState({}, enableBuffers ? 1 : 0, enableBuffers ? &binding : nullptr, enableBuffers ? attributes.size() : 0, enableBuffers ? attributes.data() : nullptr);

					//Define the assembly state
					AssemblyInputState assemblyState = vk::PipelineInputAssemblyStateCreateInfo({}, topology, false);

					//Define the viewport and scissor rect
					vk::Viewport viewport = vk::Viewport(0, 0, extent.width, extent.height, 0, 1.0f);
					vk::Rect2D scissor = vk::Rect2D({ 0, 0 }, extent);
					ViewportState viewportState = vk::PipelineViewportStateCreateInfo({}, 1, &viewport, 1, &scissor);

					//Define the rasterizer's behavior
					RasterizationState rasterizerState = vk::PipelineRasterizationStateCreateInfo(
					{}, false, false,
						vk::PolygonMode::eFill,
						vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, //TODO: Turn culling on again
						false,
						false, 0, 0,
						1.0f);

					//Define the multisampler (multisampling disabled)
					MultisampleState multisampleState = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1, false, 1, nullptr, false, false);

					//Define the depth stencil state (depth buffer enabled, stencil buffer disabled)
					DepthStencilState depthStencilState = vk::PipelineDepthStencilStateCreateInfo(
					{},
						VK_TRUE, VK_TRUE,
						vk::CompareOp::eLess,
						VK_FALSE,
						VK_FALSE,
						{}, {},
						0.0f, 1.0f
					);

					//Define color blending //TODO: Enable blending if requested
					vk::PipelineColorBlendAttachmentState attachment = vk::PipelineColorBlendAttachmentState(VK_FALSE, //Blend Enable
						vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, //SrcColor, DstColor, ColorBlendOp
						vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, //SrcAlpha, DstAlpha, AlphaBlendOp
						vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
					ColorBlendState colorBlendState = vk::PipelineColorBlendStateCreateInfo({}, false, vk::LogicOp::eClear, 1, &attachment);

					//Define dynamic variables
					vk::DynamicState dynamicStates[] = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };
					DynamicState dynamicState = vk::PipelineDynamicStateCreateInfo({}, 2, dynamicStates);

					//Create pipeline layout
					vk::DescriptorSetLayout layouts[] = { descriptorSetLayout1, descriptorSetLayout2 };
					const vk::PipelineLayoutCreateInfo ci = vk::PipelineLayoutCreateInfo({}, 2, layouts, 0, nullptr);
					const vk::Result r = device.createPipelineLayout(&ci, nullptr, &pipelineLayout);
					Misc::Console::t_assert(r == vk::Result::eSuccess, "Failed to create pipeline layout!");

					//Create pipeline with all the previously defined stages
					const vk::GraphicsPipelineCreateInfo gci = vk::GraphicsPipelineCreateInfo({},
						2, shaderStages,
						&vertexState, &assemblyState,
						nullptr, &viewportState,
						&rasterizerState, &multisampleState,
						&depthStencilState,
						&colorBlendState, &dynamicState,
						pipelineLayout, renderPass,
						0, nullptr, -1);
					pipeline = device.createGraphicsPipeline(nullptr, gci);
				}

				void Pipeline::cleanup() const
				{
					device.destroyPipeline(pipeline);
					device.destroyPipelineLayout(pipelineLayout);

					device.destroyShaderModule(vertexShader);
					device.destroyShaderModule(fragmentShader);
				}

				vk::VertexInputBindingDescription Pipeline::getBindingDescription()
				{
					//VertexInput is defiend by the Vertex struct
					return vk::VertexInputBindingDescription(
						0, sizeof(Data::Vertex),
						vk::VertexInputRate::eVertex
					);
				}

				std::array<vk::VertexInputAttributeDescription, 3> Pipeline::getAttributeDescription()
				{
					//Vertex struct attributes and their memory offset
					std::array<vk::VertexInputAttributeDescription, 3> attributes = {};
					attributes[0] = vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Data::Vertex, pos));
					attributes[1] = vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Data::Vertex, normal));
					attributes[2] = vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Data::Vertex, texCoord));
					return attributes;
				}
			}
		}
	}
}
