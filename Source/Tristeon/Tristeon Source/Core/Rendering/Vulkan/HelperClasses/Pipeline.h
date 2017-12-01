﻿#pragma once
#include <string>
#include "Misc/Property.h"
#include <vulkan/vulkan.hpp>
#include "Core/Rendering/ShaderFile.h"

namespace Tristeon
{
	namespace Core
	{
		//Forward decl
		class VulkanBindingData;

		namespace Rendering
		{
			//Forward decl
			class Window;
			class ShaderFile;

			namespace Vulkan
			{
				//Forward decl
				class Material;
				class Forward;

				/**
				 * \brief Defines the vulkan shading pipeline for a vertex-fragment shader combination
				 */
				class Pipeline
				{
					friend Forward;
					friend Material;
				public:
					/**
					 * \brief Creates a new instance of pipeline. Initializes the descriptor layout, uniform buffer and creates the rendering pipeline
					 * \param binding Rendering data
					 * \param file The shader file data
					 * \param extent The window extent (width, height)
					 * \param renderPass The renderpass this pipeline is bound to
					 * \param enableBuffers Enables/disables vertex input binding/attributes
					 * \param topologyMode The way the shaders are supposed to render data
					 */
					explicit Pipeline(VulkanBindingData* binding, ShaderFile file, vk::Extent2D extent, vk::RenderPass renderPass, bool enableBuffers = true, vk::PrimitiveTopology topologyMode = vk::PrimitiveTopology::eTriangleList);
					/**
					 * \brief Cleans up all resources created/used by this pipeline object
					 */
					~Pipeline();

					/**
					* \brief The name of the pipeline
					*/
					ReadOnlyProperty(std::string, name);
					GetPropertyConst(name) { return _name; }

					/**
					 * \return Returns the Vulkan pipeline object 
					 */
					vk::Pipeline getPipeline() const { return pipeline; }
					/**
					 * \return Returns the Vulkan pipeline layout 
					 */
					vk::PipelineLayout getPipelineLayout() const { return pipelineLayout; }

					/**
					 * \return Returns both the descriptor layouts 
					 */
					std::array<vk::DescriptorSetLayout, 2> getDescriptorLayouts() const { return { descriptorSetLayout1, descriptorSetLayout2 }; }
					
					/**
					 * \return Returns the descriptor set layout describing the uniform buffer input 
					 */
					vk::DescriptorSetLayout getUniformLayout() const { return descriptorSetLayout1; }
					/**
					 * \return Returns the descriptor set layout describing the sampler input 
					 */
					vk::DescriptorSetLayout getSamplerLayout() const { return descriptorSetLayout2; }

					/**
					 * \brief Rebuilds the Vulkan Pipeline. Generally used when the window size/swapchain extent has changed.
					 * \param extent The swapchain/window extent (width, height)
					 * \param renderPass The renderpass this pipeline is bound to
					 */
					void rebuild(vk::Extent2D extent, vk::RenderPass renderPass);
				private:
					void createDescriptorLayout();
					/**
					 * \brief Creates the Vulkan Pipeline
					 * \param extent The swapchain/window extent (width, height)
					 * \param renderPass The renderpass this pipeline is bound to
					 */
					void create(vk::Extent2D extent, vk::RenderPass renderPass);
					/**
					 * \brief Deletes all resources created by pipeline
					 */
					void cleanup() const;

					/**
					 * \brief The way this pipeline is supposed to render data
					 */
					vk::PrimitiveTopology topology;

					/**
					 * \brief Rendering data
					 */
					VulkanBindingData* binding;
					/**
					 * \brief Enable/disable vertex input binding/attributes
					 */
					bool enableBuffers;

					/**
					 * \brief The name of the pipeline
					 */
					std::string _name;
					/**
					 * \brief The Vulkan logical device
					 */
					vk::Device device;

					/**
					 * \brief The vertex shader module
					 */
					vk::ShaderModule vertexShader;
					/**
					 * \brief The fragment shader module
					 */
					vk::ShaderModule fragmentShader;

					/**
					 * \brief The layout of the pipeline
					 */
					vk::PipelineLayout pipelineLayout;

					/**
					 * \brief THe vulkan pipeline
					 */
					vk::Pipeline pipeline;

					vk::DescriptorSetLayout descriptorSetLayout1;
					vk::DescriptorSetLayout descriptorSetLayout2;
					/**
					 * \brief Creates a generic shader module
					 * \param code The shader code 
					 * \param device The vulkan device, used to create the shader module
					 * \return Returns the resulting shader module
					 */
					static vk::ShaderModule createShaderModule(const std::vector<char>& code, vk::Device device);

					/**
					 * \return Gets the binding description, describing what vertex data will be passed to the shader
					 */
					static vk::VertexInputBindingDescription getBindingDescription();
					/**
					 * \return Gets the vertex input attribute description. Used to describe every separate attribute of the vertex data 
					 */
					static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescription();

					/**
					 * \brief The shaderfile describing the shader filepath
					 */
					ShaderFile file;
				};
			}
		}
	}
}