﻿#include "RenderManagerVulkan.h"

//Engine 
#include <Core/BindingData.h>
#include "Core/Message.h"
#include "Core/Settings.h"
#include "Misc/Console.h"
#include "Math/Vector2.h"
#include "Core/Transform.h"

#include "Core/ManagerProtocol.h"
#include "Core/Rendering/Components/Renderer.h"
#include "InternalMeshRendererVulkan.h"

//Vulkan 
#include "HelperClasses/VulkanCore.h"
#include "HelperClasses/Device.h"
#include "HelperClasses/Swapchain.h"
#include "HelperClasses/Pipeline.h"
#include "HelperClasses/EditorGrid.h"
#include "HelperClasses/CameraRenderData.h"
#include "MaterialVulkan.h"
#include <Core/Rendering/Material.h>

//RenderTechniques
#include "ForwardVulkan.h"
#include "../RenderTechniques/RTechniques.h"

//Vulkan help classes
#include "HelperClasses/VulkanExtensions.h"
#include "HelperClasses/VulkanFormat.h"
#include "HelperClasses/VulkanFrame.h"
#include "Editor/JsonSerializer.h"
#include "DebugDrawManagerVulkan.h"
using Tristeon::Misc::Console;

namespace Tristeon
{
	namespace Core
	{
		namespace Rendering
		{
			namespace Vulkan
			{
				RenderManager::RenderManager(BindingData* data) : Rendering::RenderManager(data)
				{
					//Store data
					this->window = data->window;
					this->data = dynamic_cast<VulkanBindingData*>(data);
					Console::t_assert(this->data != nullptr, "RenderManagerVulkan received binding data that isn't for vulkan!");
				}

				void RenderManager::init()
				{
					//Subscribe to callbacks
					Rendering::RenderManager::init();
					ManagerProtocol::subscribeToMessage(MT_WINDOW_RESIZE, [&](Message msg)
					{
						Math::Vector2* vec = reinterpret_cast<Math::Vector2*>(msg.userData);
						resizeWindow(static_cast<int>(vec->x), static_cast<int>(vec->y));
					});

#ifdef EDITOR
					ManagerProtocol::subscribeToMessage(MT_PRERENDER, [&](Message msg) { ManagerProtocol::sendMessage({ MT_SHARE_DATA, &editor }); });
#endif

					//Create render technique
					switch (Settings::getRenderTechnique())
					{
					case RT_Forward: 
						technique = new Forward(this);
						break;
					default: 
						Console::error("Trying to use an unsupported Render technique: " + std::to_string(Settings::getRenderTechnique()));
						break;
					}

					//Setup vulkan
					setupVulkan();

#ifdef EDITOR
					//Setup editor camera
					editor.trans = new Transform();
					editor.cam = new CameraRenderData(this, dynamic_cast<VulkanBindingData*>(bindingData), offscreenPass, onscreenPipeline, true);
#endif
					//Create the debug draw manager
					DebugDrawManager::instance = new Vulkan::DebugDrawManager(data, offscreenPass);
				}

				void RenderManager::render()
				{
					//Don't render anything if there's nothing to render
					if (internalRenderers.size() == 0 && renderables.size() == 0)
						return;
					
					//Prepare frame
					index = VulkanFrame::prepare(vulkan->device, swapchain->getSwapchain(), imageAvailable);

					//Render scene
					renderScene();

					//Render cameras
					technique->renderCameras();

					//Submit frame
					submitCameras();
					VulkanFrame::submit(vulkan->device, swapchain->getSwapchain(), renderFinished, vulkan->presentQueue, index);
				}

				Pipeline* RenderManager::getPipeline(ShaderFile file)
				{
					RenderManager* rm = (RenderManager*)instance;

					for (Pipeline* p : rm->pipelines)
						if (p->getShaderFile().getNameID() == file.getNameID())
						{
							return p;
						}

					Pipeline *p = new Pipeline(rm->data, file, rm->swapchain->extent2D, rm->offscreenPass);
					rm->pipelines.push_back(p);
					return p;
				}

				void RenderManager::renderScene()
				{
					if (!inPlayMode)
					{
#ifdef EDITOR
						//Render editor camera
						glm::mat4 const view = Components::Camera::getViewMatrix(editor.trans);
						glm::mat4 const proj = Components::Camera::getProjectionMatrix((float)editor.size.x / (float)editor.size.y, 60, 0.1f, 1000.0f);
						technique->renderScene(view, proj, editor.cam);
#endif
					}
					else
					{
						//Render gameplay cameras
						for (auto const cam : cameraData)
						{
							vk::Extent2D const extent = swapchain->extent2D;
							glm::mat4 const view = cam.first->getViewMatrix();
							glm::mat4 const proj = cam.first->getProjectionMatrix((float)extent.width / (float)extent.height);
							technique->renderScene(view, proj, cam.second);
						}
					}
				}

				RenderManager::~RenderManager()
				{
					//Wait till the device is finished
					vk::Device d = vulkan->device;
					d.waitIdle();

					delete DebugDrawManager::instance;

					//Render technique
					delete technique;
					for (auto i : internalRenderers) delete i;
					internalRenderers.clear();

					//Cameras and renderpasses
					for (const auto pair : cameraData) delete pair.second;
					d.destroyRenderPass(offscreenPass);

#ifdef EDITOR
					//Editor
					delete editor.trans;
					delete editor.cam;
					delete grid;
#endif
					//Semaphores
					d.destroySemaphore(imageAvailable);
					d.destroySemaphore(renderFinished);


					//Materials
					for (auto m : materials) delete m.second;
					for (Pipeline* p : pipelines) delete p;
					delete onscreenPipeline;
					
					//DescriptorPool
					d.destroyDescriptorPool(descriptorPool);
					
					//Commandpool
					d.destroyCommandPool(commandPool);

					//Core
					delete swapchain;
					delete vulkan;
				}

				void RenderManager::reset()
				{
					//Get rid of references to scene objects
					Rendering::RenderManager::reset();
					internalRenderers.clear();

					//TODO: Clear camera refs
				}

				void RenderManager::setupVulkan()
				{
					//Core vulkan
					vulkan = new VulkanCore(data);
					
					//Swapchain
					swapchain = new Swapchain(vulkan->dev, vulkan->surface, bindingData->tristeonWindow->width, bindingData->tristeonWindow->height);
					data->swapchain = swapchain;
					data->renderPass = swapchain->renderpass;

					//Pools
					createDescriptorPool();
					createCommandPool();

					//Offscreen/onscreen data
					prepareOffscreenPass();
					prepareOnscreenPipeline();

					//Create main screen framebuffers
					swapchain->createFramebuffers();

					//Create main screen command buffers
					createCommandBuffer();

					//Initialize textures and descriptorsets
					for (auto m : materials)
						m.second->updateProperties();

					//Create image semaphores
					vk::SemaphoreCreateInfo ci{};
					vulkan->device.createSemaphore(&ci, nullptr, &imageAvailable);
					vulkan->device.createSemaphore(&ci, nullptr, &renderFinished);

#ifdef EDITOR
					//Create editor grid
					grid = new EditorGrid(data, offscreenPass);
#endif
				}

				void RenderManager::createDescriptorPool()
				{
					//UBO Buffer
					vk::DescriptorPoolSize const poolUniform = vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, materials.size()*10 + 1000);
					//Samplers
					vk::DescriptorPoolSize const poolSampler = vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, materials.size() + 1000);

					//Create pool
					std::array<vk::DescriptorPoolSize, 2> poolSizes = { poolUniform, poolSampler };
					vk::DescriptorPoolCreateInfo poolInfo = vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, materials.size() + 1000*11, poolSizes.size(), poolSizes.data());
					vk::Result const r = vulkan->device.createDescriptorPool(&poolInfo, nullptr, &descriptorPool);
					Console::t_assert(r == vk::Result::eSuccess, "Failed to create descriptor pool: " + to_string(r));
					
					//Store data
					data->descriptorPool = descriptorPool;
				}

				void RenderManager::submitCameras()
				{
					//Wait till present queue is ready to receive more commands
					vulkan->presentQueue.waitIdle();
					vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

					CameraRenderData* last = nullptr;
					if (inPlayMode)
					{
						//Submit cameras
						for (const auto p : cameraData)
						{
							CameraRenderData* c = p.second;
							vk::Semaphore wait = last == nullptr ? imageAvailable : last->offscreen.sema;
							vk::SubmitInfo s = vk::SubmitInfo(1, &wait, waitStages, 1, &c->offscreen.cmd, 1, &c->offscreen.sema );
							vulkan->graphicsQueue.submit(1, &s, nullptr);
							last = c;
						}
					}
					else
					{
#ifdef EDITOR
						//Submit editor camera
						CameraRenderData* c = editor.cam;
						if (c != nullptr)
						{
							vk::Semaphore wait = imageAvailable;
							vk::SubmitInfo s = vk::SubmitInfo(1, &wait, waitStages, 1, &c->offscreen.cmd, 1, &c->offscreen.sema);
							vulkan->graphicsQueue.submit(1, &s, nullptr);
							last = c;
						}
#endif
					}
					
					//Submit onscreen
					vk::SubmitInfo s2 = vk::SubmitInfo(
						1, last == nullptr ? &imageAvailable : &last->offscreen.sema,
						waitStages, 
						1, &primaryBuffers[index], 
						1, &renderFinished);
					vulkan->graphicsQueue.submit(1, &s2, nullptr); 
				}

				void RenderManager::prepareOnscreenPipeline()
				{
					ShaderFile file = ShaderFile("Screen", "Files/Shaders/", "ScreenV", "ScreenF");
					onscreenPipeline = new Pipeline(data, file, swapchain->extent2D, swapchain->renderpass, false);
				}

				void RenderManager::prepareOffscreenPass()
				{
					//Color attachment 
					vk::AttachmentDescription const color = vk::AttachmentDescription({},
						vk::Format::eR8G8B8A8Unorm,
						vk::SampleCountFlagBits::e1,
						vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
						vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
						vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

					//The offscreen pass renders 3D data and as such it uses a depth buffer
					vk::AttachmentDescription const depth = vk::AttachmentDescription({},
						VulkanFormat::findDepthFormat(vulkan->physicalDevice),
						vk::SampleCountFlagBits::e1,
						vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
						vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
						vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);

					//Attachments
					std::array<vk::AttachmentDescription, 2> attachmentDescr = { color, depth };
					vk::AttachmentReference colorRef = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);
					vk::AttachmentReference depthRef = vk::AttachmentReference(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

					//Subpass takes the references to our attachments
					vk::SubpassDescription subpass = vk::SubpassDescription({}, vk::PipelineBindPoint::eGraphics,
						0, nullptr,
						1, &colorRef,
						nullptr, &depthRef,
						0, nullptr);

					//Dependencies
					std::array<vk::SubpassDependency, 2> dependencies = {
						vk::SubpassDependency(
							VK_SUBPASS_EXTERNAL, 0,
							vk::PipelineStageFlagBits::eBottomOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
							vk::AccessFlagBits::eMemoryRead, vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
							vk::DependencyFlagBits::eByRegion),

						vk::SubpassDependency(
							0, VK_SUBPASS_EXTERNAL,
							vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
							vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eMemoryRead,
							vk::DependencyFlagBits::eByRegion)
					};

					//Create the renderpass
					vk::RenderPassCreateInfo const rp = vk::RenderPassCreateInfo({},
						attachmentDescr.size(), attachmentDescr.data(),
						1, &subpass,
						dependencies.size(), dependencies.data()
					);
					offscreenPass = vulkan->device.createRenderPass(rp);
				}

				TObject* RenderManager::registerRenderer(Message msg)
				{
					//Get and register internal renderer
					TObject* o = Rendering::RenderManager::registerRenderer(msg);
					Renderer* r = dynamic_cast<Renderer*>(o);
					if (r != nullptr)
					{
						InternalRenderer* internal = r->getInternalRenderer();
						InternalMeshRenderer* meshr = dynamic_cast<InternalMeshRenderer*>(internal);
						Console::t_assert(meshr != nullptr, "Render Manager Vulkan received a Renderer with an internal renderer that hasn't been created for Vulkan!");
						internalRenderers.push_back(meshr);
					}

					return o;
				}

				TObject* RenderManager::deregisterRenderer(Message msg)
				{
					//Get and deregister internal renderer
					TObject* o = Rendering::RenderManager::deregisterRenderer(msg);
					Renderer* r = dynamic_cast<Renderer*>(o);
					if (r != nullptr)
					{
						InternalMeshRenderer* meshr = dynamic_cast<InternalMeshRenderer*>(r->getInternalRenderer());
						internalRenderers.remove(meshr);
					}
					return o;
				}

				Components::Camera* RenderManager::registerCamera(Message msg)
				{
					//Base class registers the camera, we can create our respective data by using the resulting value of the base function
					Components::Camera* cam = Rendering::RenderManager::registerCamera(msg);
					
					//Create camera render data
					CameraRenderData* data = new CameraRenderData(this, dynamic_cast<VulkanBindingData*>(bindingData), offscreenPass, onscreenPipeline);
					cameraData.insert(std::make_pair(cam, data));
					
					//For inherited classes
					return cam;
				}
				
				Components::Camera* RenderManager::deregisterCamera(Message msg)
				{
					//Base class deregisters the camera, we can delete our respective data by using the resulting value of the base function
					Components::Camera* cam = Rendering::RenderManager::deregisterCamera(msg);

					//Get our camera renderdata and erase it
					CameraRenderData* data = cameraData[cam];
					cameraData.erase(cam);
					delete data;
					
					//For inherited classes
					return cam;
				}

				void RenderManager::createCommandPool()
				{
					//Commandpool creation, requires the graphics family we're using
					const QueueFamilyIndices indices = QueueFamilyIndices::get(vulkan->physicalDevice, vulkan->surface);
					vk::CommandPoolCreateInfo ci = vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, indices.graphicsFamily);
					const vk::Result r = vulkan->device.createCommandPool(&ci, nullptr, &commandPool);
					Console::t_assert(r == vk::Result::eSuccess, "Failed to create command pool: " + to_string(r));

					//Store data
					data->commandPool = commandPool;
				}

				void RenderManager::createCommandBuffer()
				{
					primaryBuffers.resize(swapchain->getFramebufferCount());

					//Create a commandbuffer for every framebuffer
					vk::CommandBufferAllocateInfo alloc = vk::CommandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, primaryBuffers.size());
					const vk::Result r = vulkan->device.allocateCommandBuffers(&alloc, primaryBuffers.data());
					Console::t_assert(r == vk::Result::eSuccess, "Failed to allocate command buffers: " + to_string(r)); 
				}

				void RenderManager::resizeWindow(int newWidth, int newHeight)
				{
					//Don't resize if the size is 0
					if (newWidth == 0 || newHeight == 0)
						return;

					//Wait for other commands to be finished
					vulkan->device.waitIdle();
					
					//Rebuild swapchain
					swapchain->rebuild(newWidth, newHeight);
					data->renderPass = swapchain->renderpass;

					//Rebuild offscreen renderpass
					vulkan->device.destroyRenderPass(offscreenPass);
					prepareOffscreenPass();

					//Rebuild camera data
					for (auto const p : cameraData)
						p.second->rebuild(this, offscreenPass, onscreenPipeline);
#ifdef EDITOR
					//Rebuild editor data
					editor.cam->rebuild(this, offscreenPass, onscreenPipeline);
					grid->rebuild(offscreenPass);
#endif
					//Rebuild pipelines
					for (int i = 0; i < pipelines.size(); i++)
						pipelines[i]->rebuild(swapchain->extent2D, offscreenPass);

					((Vulkan::DebugDrawManager*)DebugDrawManager::instance)->rebuild(offscreenPass);

					//Rebuild framebuffers
					swapchain->createFramebuffers();
				}

				vk::Framebuffer RenderManager::getActiveFrameBuffer() const
				{
					return swapchain->getFramebufferAt(index);
				}

				Rendering::Material* RenderManager::getmaterial(std::string filePath)
				{
					//Try to return the material from our batched materials
					if (materials.find(filePath) != materials.end())
						return materials[filePath];

					//Don't even bother doing anything if the material doesn't exist
					if (!std::experimental::filesystem::exists(filePath))
						return nullptr;

					//Our materials can only be .mat files
					if (std::experimental::filesystem::path(filePath).extension() != ".mat")
						return nullptr;
					
					Vulkan::Material* m = new Vulkan::Material();
					m->deserialize(JsonSerializer::load(filePath));
					
					//Set up the material 
					m->updateProperties(true);
					materials[filePath] = m;
					return m;
				}
			}
		}
	}
}