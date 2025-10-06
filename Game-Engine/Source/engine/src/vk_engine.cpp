#define VMA_IMPLEMENTATION
#define GRADIENT_COMPUTE
#include "vk_engine.h"

#include "vk_images.h"
#include "vk_loader.h"
#include "vk_descriptors.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>

#include "VkBootstrap.h"

#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtx/transform.hpp>

#include "vk_mem_alloc.h"

#ifdef DEBUG
constexpr bool useValidationLayers = true;
#else
constexpr bool useValidationLayers = false;
#endif


VulkanEngine* loadedEngine = nullptr;

VulkanEngine& VulkanEngine::get() { return *loadedEngine; }

void VulkanEngine::init() {
	assert(loadedEngine == nullptr);
	loadedEngine = this;

	SDL_Init(SDL_INIT_VIDEO);
	SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		windowExtent.width,
		windowExtent.height,
		windowFlags
	);

	initVulkan();
	initSwapchain();
	initCommands();
	initSyncStructures();
	initDescriptors();
	initPipelines();
	initImGui();
	init_default_data();

	camera.velocity = glm::vec3(0.f);
	camera.position = glm::vec3(0, 0, 5);
	camera.pitch = 0;
	camera.yaw = 0;

	isInitialized = true;

	std::string structurePath = { "structure.glb" };
	std::string scenePath = { "scene/source/Untitled.glb" };
	auto structureFile = load_gltf(this, scenePath);

	assert(structureFile.has_value());

	loadedScenes["structure"] = *structureFile;
}


void VulkanEngine::cleanup() {
	if (isInitialized) {
		vkDeviceWaitIdle(driver);

		loadedScenes.clear();
		metalRoughMat.clearResources(driver);

		for (int i = 0; i < FRAME_OVERLAP; i++) {
			vkDestroyCommandPool(driver, frames[i].commandPool, nullptr);

			vkDestroyFence(driver, frames[i].renderFence, nullptr);
			vkDestroySemaphore(driver, frames[i].renderSemaphore, nullptr);
			vkDestroySemaphore(driver, frames[i].swapchainSemaphore, nullptr);
			frames[i].deletionQueue.flush();
		}
		mainDeletionQueue.flush();
		destroySwapchain();
		vkDestroySurfaceKHR(instance, surface, nullptr);
		vkDestroyDevice(driver, nullptr);
		vkb::destroy_debug_utils_messenger(instance, debugMessenger);
		vkDestroyInstance(instance, nullptr);
		SDL_DestroyWindow(window);
	}
	loadedEngine = nullptr;
}


void VulkanEngine::draw() {
	updateScene();
	VK_CHECK(vkWaitForFences(driver, 1, &get_current_frame().renderFence, true, UINT64_MAX));
	get_current_frame().deletionQueue.flush();
	get_current_frame().descriptorAllocator.clearPools(driver);
	uint32_t swapchainImageIndex;
	VkResult resize = vkAcquireNextImageKHR(driver, swapchain, UINT64_MAX, get_current_frame().swapchainSemaphore, nullptr, &swapchainImageIndex);
	if (resize == VK_ERROR_OUT_OF_DATE_KHR || resize == VK_SUBOPTIMAL_KHR) {
		resizeRequest = true;
		return;
	}
	drawExtent.height = std::min(swapchainExtent.height, drawImage.imageExtent.height) * renderScale;
	drawExtent.width = std::min(swapchainExtent.width, drawImage.imageExtent.width) * renderScale;

	VK_CHECK(vkResetFences(driver, 1, &get_current_frame().renderFence));
	VK_CHECK(vkResetCommandBuffer(get_current_frame().commandBuffer, 0));
	VkCommandBuffer cmd = get_current_frame().commandBuffer;
	VkCommandBufferBeginInfo beginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);	
	VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

	vkutil::transition_image(cmd, drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	drawBackground(cmd);

	vkutil::transition_image(cmd, drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	vkutil::transition_image(cmd, depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	drawMesh(cmd);

	vkutil::transition_image(cmd, drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	vkutil::transition_image(cmd, swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	vkutil::copy_image_to_image(cmd, drawImage.image, swapchainImages[swapchainImageIndex], drawExtent, swapchainExtent);

	vkutil::transition_image(cmd, swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	drawImGui(cmd, swapchainImageViews[swapchainImageIndex]);

	vkutil::transition_image(cmd, swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);
	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame().swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame().renderSemaphore);
	VkSubmitInfo2 submit = vkinit::submit_info(&cmdInfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, get_current_frame().renderFence));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &get_current_frame().renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	resize = vkQueuePresentKHR(graphicsQueue, &presentInfo);
	
	if (resize == VK_ERROR_OUT_OF_DATE_KHR || resize == VK_SUBOPTIMAL_KHR) {
		resizeRequest = true;
	}

	frameNumber++;
}


void VulkanEngine::run() {
	SDL_Event event;
	bool bQuit = false;
	bool fps = false;

	while (!bQuit) {
		auto start = std::chrono::system_clock::now();

		while (SDL_PollEvent(&event) != 0) {
			if (event.type == SDL_QUIT) bQuit = true;
			if (event.type == SDL_WINDOWEVENT) {
				if (event.type == SDL_WINDOWEVENT_MINIMIZED) {
					stopRendering = true;
				}
				if (event.type == SDL_WINDOWEVENT_RESTORED) {
					stopRendering = false;
				}	
			}

			if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
				bQuit = true;
			}
			if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_p) {
				fps = !fps;
				SDL_SetRelativeMouseMode(fps ? SDL_TRUE : SDL_FALSE);
			}

			if (fps) camera.processSDLEvents(event);
			ImGui_ImplSDL2_ProcessEvent(&event);
		}

		if (stopRendering) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		if (resizeRequest) {
			resizeSwapchain();
			fmt::print("[CONSOLE INFO]: Resized to {}, {}\n", windowExtent.width, windowExtent.height);
			resizeRequest = false;
		}

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("Stats");

		ImGui::Text("frametime %f ms", stats.frametime);
		ImGui::Text("draw time %f ms", stats.mesh_draw_time);
		ImGui::Text("update time %f ms", stats.scene_update_time);
		ImGui::Text("triangles %i", stats.triangle_count);
		ImGui::Text("draws %i", stats.drawcall_count);
		ImGui::End();
		
		ImGui::Render();

		draw();

		auto end = std::chrono::system_clock::now();

		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		stats.frametime = elapsed.count() / 1000.f;
	}
}


void VulkanEngine::initVulkan() {
	vkb::InstanceBuilder instanceBuilder;

	auto instanceReticle = instanceBuilder.set_app_name("Vulkan Game Engine")
		.request_validation_layers(useValidationLayers)
		.use_default_debug_messenger()
		.require_api_version(1, 3, 0)
		.build();
	vkb::Instance vkbInstance = instanceReticle.value();
	instance = vkbInstance.instance;
	debugMessenger = vkbInstance.debug_messenger;

	SDL_Vulkan_CreateSurface(window, instance, &surface);
	VkPhysicalDeviceVulkan13Features features{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
	};
	features.dynamicRendering = true;
	features.synchronization2 = true;

	VkPhysicalDeviceVulkan12Features features12 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
	};
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing = true;

	vkb::PhysicalDeviceSelector selector{ vkbInstance };
	vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 3)
		.set_required_features_12(features12)
		.set_required_features_13(features)
		.set_surface(surface)
		.select()
		.value();

	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	vkb::Device device = deviceBuilder.build().value();

	driver = device.device;
	chosenGPU = physicalDevice.physical_device;

	graphicsQueue = device.get_queue(vkb::QueueType::graphics).value();
	graphicsQueueFamily = device.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = chosenGPU;
	allocatorInfo.device = driver;
	allocatorInfo.instance = instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &allocator);

	mainDeletionQueue.push_function([&]() {
		vmaDestroyAllocator(allocator);
	});
}


void VulkanEngine::initCommands() {
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	for (int i = 0; i < FRAME_OVERLAP; i++) {

		VK_CHECK(vkCreateCommandPool(driver, &commandPoolInfo, nullptr, &frames[i].commandPool));

		VkCommandBufferAllocateInfo allocInfo = vkinit::command_buffer_allocate_info(frames[i].commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(driver, &allocInfo, &frames[i].commandBuffer));
	}

	VK_CHECK(vkCreateCommandPool(driver, &commandPoolInfo, nullptr, &immCmdPool));

	VkCommandBufferAllocateInfo allocInfo = vkinit::command_buffer_allocate_info(immCmdPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(driver, &allocInfo, &immCmdBuffer));

	mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(driver, immCmdPool, nullptr);
		});
}


void VulkanEngine::initSwapchain() {
	createSwapchain(windowExtent.width, windowExtent.height);
	VkExtent3D drawImageExtent = {
		windowExtent.width,
		windowExtent.height,
		1
	};

	drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimgInfo = vkinit::image_create_info(drawImage.imageFormat, drawImageUsages, drawImageExtent);
	VmaAllocationCreateInfo rimgAllocInfo = {};
	rimgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(allocator, &rimgInfo, &rimgAllocInfo, &drawImage.image, &drawImage.allocation, nullptr);

	VkImageViewCreateInfo rviewInfo = vkinit::imageview_create_info(drawImage.imageFormat, drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(driver, &rviewInfo, nullptr, &drawImage.imageView));

	depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	depthImage.imageExtent = drawImageExtent;
	VkImageUsageFlags depthUsages{};
	depthUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkImageCreateInfo dinfo = vkinit::image_create_info(depthImage.imageFormat, depthUsages, depthImage.imageExtent);
	vmaCreateImage(allocator, &dinfo, &rimgAllocInfo, &depthImage.image, &depthImage.allocation, nullptr);

	VkImageViewCreateInfo dviewInfo = vkinit::imageview_create_info(depthImage.imageFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
	VK_CHECK(vkCreateImageView(driver, &dviewInfo, nullptr, &depthImage.imageView));

	mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(driver, drawImage.imageView, nullptr);
		vmaDestroyImage(allocator, drawImage.image, drawImage.allocation);
		vkDestroyImageView(driver, depthImage.imageView, nullptr);
		vmaDestroyImage(allocator, depthImage.image, depthImage.allocation);
		});
}


void VulkanEngine::createSwapchain(uint32_t width, uint32_t height) {
	vkb::SwapchainBuilder swapchainBuilder{ chosenGPU, driver, surface };
	swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.set_desired_format(VkSurfaceFormatKHR{ .format = swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();
	swapchainExtent = vkbSwapchain.extent;
	swapchain = vkbSwapchain.swapchain;
	swapchainImages = vkbSwapchain.get_images().value();
	swapchainImageViews = vkbSwapchain.get_image_views().value();
}


void VulkanEngine::destroySwapchain() {
	vkDestroySwapchainKHR(driver, swapchain, nullptr);
	for (int i = 0; i < swapchainImageViews.size(); i++) {
		vkDestroyImageView(driver, swapchainImageViews[i], nullptr);
	}
}


void VulkanEngine::initSyncStructures() {
	VkFenceCreateInfo fenceInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreInfo = vkinit::semaphore_create_info();
	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(driver, &fenceInfo, nullptr, &frames[i].renderFence));
		VK_CHECK(vkCreateSemaphore(driver, &semaphoreInfo, nullptr, &frames[i].renderSemaphore));
		VK_CHECK(vkCreateSemaphore(driver, &semaphoreInfo, nullptr, &frames[i].swapchainSemaphore));
	}

	VK_CHECK(vkCreateFence(driver, &fenceInfo, nullptr, &immFence));
	mainDeletionQueue.push_function([=]() {
		vkDestroyFence(driver, immFence, nullptr);
		});
}


void VulkanEngine::drawBackground(VkCommandBuffer cmd) {
	ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, backgroundPipelineLayout, 0, 1, &drawImageDescriptors, 0, nullptr);

	vkCmdPushConstants(cmd, backgroundPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &effect.data);
	
	vkCmdDispatch(cmd, std::ceil(drawExtent.width / 16.0), std::ceil(drawExtent.height / 16.0), 1);
}


void VulkanEngine::initDescriptors() {
	std::vector<DynamicDescriptorAllocator::PoolSizeRatio> sizes = {
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER}
	};

	globalDescriptorAllocator.init(driver, 10, sizes);

	{
		DescriptorLayoutBuilder builder; 
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		drawImageDescriptorLayout = builder.build(driver, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		gpuSceneDescriptorSetLayout = builder.build(driver, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		singleImageDescriptorLayout = builder.build(driver, VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	drawImageDescriptors = globalDescriptorAllocator.allocate(driver, drawImageDescriptorLayout);

	{
		DescriptorWriter writer;
		writer.writeImage(0, drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.updateSet(driver, drawImageDescriptors);
	}

	mainDeletionQueue.push_function([&]() {
		globalDescriptorAllocator.destroyPools(driver);
		vkDestroyDescriptorSetLayout(driver, drawImageDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(driver, gpuSceneDescriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(driver, singleImageDescriptorLayout, nullptr);
		});

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		// create a descriptor pool
		std::vector<DynamicDescriptorAllocator::PoolSizeRatio> frame_sizes = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
		};

		frames[i].descriptorAllocator = DynamicDescriptorAllocator{};
		frames[i].descriptorAllocator.init(driver, 128, frame_sizes);

		mainDeletionQueue.push_function([&, i]() {
			frames[i].descriptorAllocator.destroyPools(driver);
			});
	}
}

void VulkanEngine::initPipelines() {
	initMeshPipeline();
	initGradientPipelines();
	metalRoughMat.buildPipelines(this);
}

void VulkanEngine::initMeshPipeline() {
	VkShaderModule triangleFragShader;
	if (!vkutil::load_shader_module("shader.frag", driver, &triangleFragShader)) {
		fmt::print("Error when building the triangle fragment shader module\n");
	}
	else {
		fmt::print("Triangle fragment shader succesfully loaded\n");
	}

	VkShaderModule triangleVertexShader;
	if (!vkutil::load_shader_module("shader.vert", driver, &triangleVertexShader)) {
		fmt::print("Error when building the triangle vertex shader module\n");
	}
	else {
		fmt::print("Triangle vertex shader succesfully loaded\n");
	}

	VkPushConstantRange bufferRange{};
	bufferRange.offset = 0;
	bufferRange.size = sizeof(GPUDrawPushConstants);
	bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
	pipeline_layout_info.pPushConstantRanges = &bufferRange;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pSetLayouts = &singleImageDescriptorLayout;
	pipeline_layout_info.setLayoutCount = 1;

	VK_CHECK(vkCreatePipelineLayout(driver, &pipeline_layout_info, nullptr, &meshPipelineLayout));

	PipelineBuilder pipelineBuilder;

	
	pipelineBuilder.pipelineLayout = meshPipelineLayout;
	
	pipelineBuilder.setShaders(triangleVertexShader, triangleFragShader);
	
	pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	
	pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
	
	pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	
	pipelineBuilder.setMultisamplingNone();
	
	//pipelineBuilder.enableBlendingAdditive();
	pipelineBuilder.disableBlending();

	pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

	pipelineBuilder.setColorAttachmentFormat(drawImage.imageFormat);
	pipelineBuilder.setDepthFormat(depthImage.imageFormat);
	//pipelineBuilder.setDepthFormat(VK_FORMAT_UNDEFINED);
	
	meshPipeline = pipelineBuilder.buildPipeline(driver);

	
	vkDestroyShaderModule(driver, triangleFragShader, nullptr);
	vkDestroyShaderModule(driver, triangleVertexShader, nullptr);

	mainDeletionQueue.push_function([&]() {
		vkDestroyPipelineLayout(driver, meshPipelineLayout, nullptr);
		vkDestroyPipeline(driver, meshPipeline, nullptr);
		});
}

void VulkanEngine::initGradientPipelines() {
	VkPipelineLayoutCreateInfo computeLayout{};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;
	computeLayout.pSetLayouts = &drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstants{};
	pushConstants.offset = 0;
	pushConstants.size = sizeof(PushConstants);
	pushConstants.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges = &pushConstants;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(driver, &computeLayout, nullptr, &backgroundPipelineLayout));

	VkShaderModule computeDrawShader;
	if (!vkutil::load_shader_module("empty.comp", driver, &computeDrawShader)) {
		fmt::print("[SHADER COMPILE ERROR] error when compiling the compute shader {}\n", "gradient.comp");
	}

	VkPipelineShaderStageCreateInfo stageInfo{};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.pNext = nullptr;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.module = computeDrawShader;
	stageInfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineInfo{};
	computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineInfo.pNext = nullptr;
	computePipelineInfo.layout = backgroundPipelineLayout;
	computePipelineInfo.stage = stageInfo;

	ComputeEffect gradient;
	gradient.layout = backgroundPipelineLayout;
	gradient.name = "gradient";
	gradient.data = {};

	gradient.data.data1 = glm::vec4(0, 0, 0, 1);

	VK_CHECK(vkCreateComputePipelines(driver, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &gradient.pipeline));

	backgroundEffects.push_back(gradient);

	vkDestroyShaderModule(driver, computeDrawShader, nullptr);

	mainDeletionQueue.push_function([&]() {
		for (auto& fx : backgroundEffects) {
			vkDestroyPipeline(driver, fx.pipeline, nullptr);
		}
		backgroundEffects.clear();
		vkDestroyPipelineLayout(driver, backgroundPipelineLayout, nullptr);
		});
}

void VulkanEngine::initImGui() {
	VkDescriptorPoolSize poolSizes[] = { {VK_DESCRIPTOR_TYPE_SAMPLER, 64 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 64 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 64 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 64 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 64 } };

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 128;
	poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
	poolInfo.pPoolSizes = poolSizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(driver, &poolInfo, nullptr, &imguiPool));

	ImGui::CreateContext();

	ImGui_ImplSDL2_InitForVulkan(window);

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Device = driver;
	init_info.Instance = instance;
	init_info.PhysicalDevice = chosenGPU;
	init_info.Queue = graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainImageFormat;

	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	ImGui_ImplVulkan_CreateFontsTexture();

	mainDeletionQueue.push_function([=]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(driver, imguiPool, nullptr);
		});
}

void VulkanEngine::immediate_cmd(std::function<void(VkCommandBuffer cmd)>&& function) {
	VK_CHECK(vkResetFences(driver, 1, &immFence));
	VK_CHECK(vkResetCommandBuffer(immCmdBuffer, 0));

	VkCommandBuffer cmd = immCmdBuffer;
	VkCommandBufferBeginInfo beginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);
	VkSubmitInfo2 submit2 = vkinit::submit_info(&cmdInfo, nullptr, nullptr);

	VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit2, immFence));
	VK_CHECK(vkWaitForFences(driver, 1, &immFence, VK_TRUE, UINT64_MAX));
}

void VulkanEngine::drawImGui(VkCommandBuffer cmd, VkImageView targetImageview){
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageview, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkinit::rendering_info(swapchainExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

bool isVisible(const RenderObject& obj, const glm::mat4& viewproj) {
	std::array<glm::vec3, 8> corners{
		glm::vec3 { 1, 1, 1 },
		glm::vec3 { 1, 1, -1 },
		glm::vec3 { 1, -1, 1 },
		glm::vec3 { 1, -1, -1 },
		glm::vec3 { -1, 1, 1 },
		glm::vec3 { -1, 1, -1 },
		glm::vec3 { -1, -1, 1 },
		glm::vec3 { -1, -1, -1 },
	};

	glm::mat4 matrix = viewproj * obj.transform;

	glm::vec3 min = { 1.5, 1.5, 1.5 };
	glm::vec3 max = { -1.5, -1.5, -1.5 };

	for (int c = 0; c < 8; c++) {
		// project each corner into clip space
		glm::vec4 v = matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.f);

		// perspective correction
		v.x = v.x / v.w;
		v.y = v.y / v.w;
		v.z = v.z / v.w;

		min = glm::min(glm::vec3{ v.x, v.y, v.z }, min);
		max = glm::max(glm::vec3{ v.x, v.y, v.z }, max);
	}

	// check the clip space box is within the view
	if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f || min.y > 1.f || max.y < -1.f) {
		return false;
	}
	else {
		return true;
	}
}

void VulkanEngine::drawMesh(VkCommandBuffer cmd) {
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo = vkinit::rendering_info(windowExtent, &colorAttachment, &depthAttachment);

	vkCmdBeginRendering(cmd, &renderInfo);
	auto start = std::chrono::system_clock::now();
	std::vector<uint32_t> opaque_draws;
	opaque_draws.reserve(drawContext.OpaqueSurfaces.size());

	for (int i = 0; i < drawContext.OpaqueSurfaces.size(); i++) {
		if (isVisible(drawContext.OpaqueSurfaces[i], sceneData.viewproj)) {
			opaque_draws.push_back(i);
		}
	}

	// sort the opaque surfaces by material and mesh
	std::sort(opaque_draws.begin(), opaque_draws.end(), [&](const auto& iA, const auto& iB) {
		const RenderObject& A = drawContext.OpaqueSurfaces[iA];
		const RenderObject& B = drawContext.OpaqueSurfaces[iB];
		if (A.material == B.material) {
			return A.indexBuffer < B.indexBuffer;
		}
		else {
			return A.material < B.material;
		}
		});

	//allocate a new uniform buffer for the scene data
	AllocatedBuffer gpuSceneDataBuffer = createBuffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	//add it to the deletion queue of this frame so it gets deleted once its been used
	get_current_frame().deletionQueue.push_function([=, this]() {
		destroyBuffer(gpuSceneDataBuffer);
		});

	//write the buffer
	GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
	*sceneUniformData = sceneData;

	//create a descriptor set that binds that buffer and update it
	VkDescriptorSet globalDescriptor = get_current_frame().descriptorAllocator.allocate(driver, gpuSceneDescriptorSetLayout);

	DescriptorWriter writer;
	writer.writeBuffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.updateSet(driver, globalDescriptor);

	MaterialPipeline* lastPipeline = nullptr;
	MaterialInstance* lastMaterial = nullptr;
	VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

	auto draw = [&](const RenderObject& r) {
		if (r.material != lastMaterial) {
			lastMaterial = r.material;
			if (r.material->pipeline != lastPipeline) {

				lastPipeline = r.material->pipeline;
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 0, 1,
					&globalDescriptor, 0, nullptr);

				VkViewport viewport = {};
				viewport.x = 0;
				viewport.y = 0;
				viewport.width = drawExtent.width;
				viewport.height = drawExtent.height;
				viewport.minDepth = 0.f;
				viewport.maxDepth = 1.f;

				vkCmdSetViewport(cmd, 0, 1, &viewport);

				VkRect2D scissor = {};
				scissor.offset.x = 0;
				scissor.offset.y = 0;
				scissor.extent.width = drawExtent.width;
				scissor.extent.height = drawExtent.height;

				vkCmdSetScissor(cmd, 0, 1, &scissor);
			}

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 1, 1,
				&r.material->materialSet, 0, nullptr);
		}
		if (r.indexBuffer != lastIndexBuffer) {
			lastIndexBuffer = r.indexBuffer;
			vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		}
		// calculate final mesh matrix
		GPUDrawPushConstants push_constants;
		push_constants.worldMatrix = r.transform;
		push_constants.vertexBuffer = r.vertexBufferAddress;

		vkCmdPushConstants(cmd, r.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);

		stats.drawcall_count++;
		stats.triangle_count += r.indexCount / 3;
		vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
		};

	stats.drawcall_count = 0;
	stats.triangle_count = 0;

	for (auto& r : opaque_draws) {
		draw(drawContext.OpaqueSurfaces[r]);
	}

	for (auto& r : drawContext.TransparentSurfaces) {
		draw(r);
	}

	// we delete the draw commands now that we processed them
	drawContext.OpaqueSurfaces.clear();
	drawContext.TransparentSurfaces.clear();

	auto end = std::chrono::system_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	stats.mesh_draw_time = elapsed.count() / 1000.f;

	vkCmdEndRendering(cmd);
}


AllocatedBuffer VulkanEngine::createBuffer(size_t allocSize, VkBufferUsageFlags flags, VmaMemoryUsage memoryUsage) {
	VkBufferCreateInfo info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	info.pNext = nullptr;
	info.size = allocSize;

	info.usage = flags;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(allocator, &info, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.allocInfo));
	return newBuffer;
}

void VulkanEngine::destroyBuffer(const AllocatedBuffer& buffer) {
	vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}


GPUMeshBuffers VulkanEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) {
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	GPUMeshBuffers newSurface;

	newSurface.vertexBuffer = createBuffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	VkBufferDeviceAddressInfo addrInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	addrInfo.buffer = newSurface.vertexBuffer.buffer;
	newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(driver, &addrInfo);

	newSurface.indexBuffer = createBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	AllocatedBuffer staging = createBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	void* data = staging.allocation->GetMappedData();

	memcpy(data, vertices.data(), vertexBufferSize);

	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

	immediate_cmd([&](VkCommandBuffer cmd) {
		VkBufferCopy vertexCopy{ 0 };
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy;
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
		});

	destroyBuffer(staging);

	return newSurface;
}

void VulkanEngine::init_default_data() {

	sceneMeshes = load_gltf_meshes(this, "basicmesh.glb").value();

	mainDeletionQueue.push_function([&]() {
		for (std::shared_ptr<MeshAsset> mesh : sceneMeshes) {
			destroyBuffer(mesh->meshBuffers.indexBuffer);
			destroyBuffer(mesh->meshBuffers.vertexBuffer);
			mesh->meshBuffers = {};
		}
		});

	uint32_t white = glm::packUnorm4x8(glm::vec4(1.f));
	whiteImage = createImage((void*) & white, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
	uint32_t grey = glm::packUnorm4x8(glm::vec4(glm::vec3(0.66f), 1));
	greyImage = createImage((void*)&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
	uint32_t black = glm::packUnorm4x8(glm::vec4(0.f, 0.f, 0.f, 1.f));
	blackImage = createImage((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1.f, 0.f, 1.f, 1.f));
	std::array<uint32_t, 16 * 16> pixels;
	for (int x = 0; x < 16 ; x++) {
		for (int y = 0; y < 16; y++) {
			pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
		}
	}

	errorTexture = createImage((void*)pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

	VkSamplerCreateInfo samplerInfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;

	VK_CHECK(vkCreateSampler(driver, &samplerInfo, nullptr, &defaultSamplerNearest));

	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;

	VK_CHECK(vkCreateSampler(driver, &samplerInfo, nullptr, &defaultSamplerLinear));

	mainDeletionQueue.push_function([&]() {
		destroyImage(whiteImage);
		destroyImage(greyImage);
		destroyImage(blackImage);
		destroyImage(errorTexture);
		vkDestroySampler(driver, defaultSamplerNearest, nullptr);
		vkDestroySampler(driver, defaultSamplerLinear, nullptr);
		});

	GLTFMetallic_Roughness::MaterialResources materialRes;
	materialRes.colorImage = whiteImage;
	materialRes.colorSampler = defaultSamplerLinear;
	materialRes.metalRoughImage = whiteImage;
	materialRes.metalRoughSampler = defaultSamplerLinear;

	AllocatedBuffer materialConst = createBuffer(sizeof(GLTFMetallic_Roughness::MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	GLTFMetallic_Roughness::MaterialConstants* sceneUniformData = (GLTFMetallic_Roughness::MaterialConstants*)materialConst.allocation->GetMappedData();
	sceneUniformData->colorFactor = glm::vec4{ 1, 1, 1, 1 };
	sceneUniformData->metalRoughFactor = glm::vec4{ 1, 0.5, 0, 0 };
	mainDeletionQueue.push_function([=, this]() {
		destroyBuffer(materialConst);
		});
	materialRes.dataBuffer = materialConst.buffer;
	materialRes.bufferOffset = 0;
	defaultMat = metalRoughMat.writeMaterial(driver, MaterialPass::MAIN_COLOR, materialRes, globalDescriptorAllocator);

	for (auto& m : sceneMeshes) {
		std::shared_ptr<MeshNode> newNode = std::make_shared<MeshNode>();
		newNode->mesh = m;
		newNode->localTransform = glm::mat4{ 1.f };
		newNode->worldTransform = glm::mat4{ 1.f };

		for (auto& s : newNode->mesh->surfaces) {
			s.material = std::make_shared<GLTFMaterial>(defaultMat);
		}
		loadedNodes[m->name] = std::move(newNode);
	}
}

void VulkanEngine::resizeSwapchain() {
	vkDeviceWaitIdle(driver);
	destroySwapchain();

	int width, height;
	SDL_GetWindowSize(window, &width, &height);
	windowExtent.width = width;
	windowExtent.height = height;

	createSwapchain(width, height);

	resizeRequest = false;
}

AllocatedImage VulkanEngine::createImage(VkExtent3D size, VkFormat format, VkImageUsageFlags flags, bool mipmapped) {
	AllocatedImage newImage;
	newImage.imageFormat = format;
	newImage.imageExtent = size;

	VkImageCreateInfo createInfo = vkinit::image_create_info(format, flags, size);
	if(mipmapped){
		createInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
	}

	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VK_CHECK(vmaCreateImage(allocator, &createInfo, &allocInfo, &newImage.image, &newImage.allocation, nullptr));

	VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT) {
		aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	VkImageViewCreateInfo viewInfo = vkinit::imageview_create_info(format, newImage.image, aspectFlags);
	viewInfo.subresourceRange.levelCount = createInfo.mipLevels;

	VK_CHECK(vkCreateImageView(driver, &viewInfo, nullptr, &newImage.imageView));

	return newImage;
}

AllocatedImage VulkanEngine::createImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
	size_t data_size = size.depth * size.width * size.height * 4;
	AllocatedBuffer uploadBuffer = createBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	memcpy(uploadBuffer.allocInfo.pMappedData, data, data_size);
	AllocatedImage newImage = createImage(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);
	immediate_cmd([=](VkCommandBuffer cmd) {
		vkutil::transition_image(cmd, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = size;

		vkCmdCopyBufferToImage(cmd, uploadBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
		if (mipmapped) {
			vkutil::generate_mipmaps(cmd, newImage.image, VkExtent2D{ newImage.imageExtent.width,newImage.imageExtent.height });
		}
		else {
			vkutil::transition_image(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		});

	destroyBuffer(uploadBuffer);
	return newImage;
}

void VulkanEngine::destroyImage(const AllocatedImage& image) {
	vkDestroyImageView(driver, image.imageView, nullptr);
	vmaDestroyImage(allocator, image.image, image.allocation);
}

void VulkanEngine::updateScene() {
	camera.update();
	drawContext.OpaqueSurfaces.clear();
	loadedNodes["Suzanne"]->Draw(glm::mat4{ 1.f }, drawContext); 
	loadedScenes["structure"]->Draw(glm::mat4{ 1.f }, drawContext);
	long long time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
	sceneData.view = camera.getViewMatrix();
	sceneData.projection = glm::perspective(glm::radians(70.f), (float)windowExtent.width / (float)windowExtent.height, 10000.f, 0.1f);
	sceneData.projection[1][1] *= -1;
	sceneData.viewproj = sceneData.projection * sceneData.view;

	sceneData.ambientColor = glm::vec4(0.03f, 0.03f, 0.03f, 1.f);
	sceneData.sunlightColor = glm::vec4(1.f, 1.f, 0.9f, 1.f);
	sceneData.sunlightDirection = glm::vec4(glm::normalize(glm::vec3(1, -3, -1)), 1.f);
}

void GLTFMetallic_Roughness::buildPipelines(VulkanEngine* engine)
{
	VkShaderModule meshFragShader;
	if (!vkutil::load_shader_module("mesh.frag", engine->driver, &meshFragShader)) {
		fmt::println("Error when building the triangle fragment shader module");
	}

	VkShaderModule meshVertexShader;
	if (!vkutil::load_shader_module("mesh.vert", engine->driver, &meshVertexShader)) {
		fmt::println("Error when building the triangle vertex shader module");
	}

	VkPushConstantRange matrixRange{};
	matrixRange.offset = 0;
	matrixRange.size = sizeof(GPUDrawPushConstants);
	matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	DescriptorLayoutBuilder layoutBuilder;
	layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	layoutBuilder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	layoutBuilder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	materialLayout = layoutBuilder.build(engine->driver, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

	VkDescriptorSetLayout layouts[] = { engine->gpuSceneDescriptorSetLayout,
		materialLayout };

	VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
	mesh_layout_info.setLayoutCount = 2;
	mesh_layout_info.pSetLayouts = layouts;
	mesh_layout_info.pPushConstantRanges = &matrixRange;
	mesh_layout_info.pushConstantRangeCount = 1;

	VkPipelineLayout newLayout;
	VK_CHECK(vkCreatePipelineLayout(engine->driver, &mesh_layout_info, nullptr, &newLayout));

	opaquePipeline.layout = newLayout;
	transparentPipeline.layout = newLayout;

	// build the stage-create-info for both vertex and fragment stages. This lets
	// the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;
	pipelineBuilder.setShaders(meshVertexShader, meshFragShader);
	pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.setMultisamplingNone();
	pipelineBuilder.disableBlending();
	pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

	//render format
	pipelineBuilder.setColorAttachmentFormat(engine->drawImage.imageFormat);
	pipelineBuilder.setDepthFormat(engine->depthImage.imageFormat);

	// use the triangle layout we created
	pipelineBuilder.pipelineLayout = newLayout;

	// finally build the pipeline
	opaquePipeline.pipeline = pipelineBuilder.buildPipeline(engine->driver);

	// create the transparent variant
	pipelineBuilder.enableBlendingAdditive();

	pipelineBuilder.enableDepthTest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

	transparentPipeline.pipeline = pipelineBuilder.buildPipeline(engine->driver);

	vkDestroyShaderModule(engine->driver, meshFragShader, nullptr);
	vkDestroyShaderModule(engine->driver, meshVertexShader, nullptr);
}

MaterialInstance GLTFMetallic_Roughness::writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources& resources, DynamicDescriptorAllocator& allocator) {
	MaterialInstance matData;
	matData.passType = pass;
	if (pass == MaterialPass::TRASNPARENT) {
		matData.pipeline = &transparentPipeline;
	}
	else {
		matData.pipeline = &opaquePipeline;
	}

	matData.materialSet = allocator.allocate(device, materialLayout);

	writer.clear();
	writer.writeBuffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.bufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.writeImage(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	writer.writeImage(2, resources.metalRoughImage.imageView, resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	writer.updateSet(device, matData.materialSet);

	return matData;
}

void GLTFMetallic_Roughness::clearResources(VkDevice device)
{
	vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
	vkDestroyPipelineLayout(device, transparentPipeline.layout, nullptr);

	vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
	vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
}


void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
	glm::mat4 nodeMatrix = topMatrix * worldTransform;

	for (auto& s : mesh->surfaces) {
		RenderObject obj;
		obj.indexCount = s.count;
		obj.firstIndex = s.startIndex;
		obj.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
		obj.material = &s.material->data;
		obj.bounds = s.bounds;
		obj.transform = nodeMatrix;
		obj.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

		if (s.material->data.passType == MaterialPass::TRASNPARENT) {
			ctx.TransparentSurfaces.push_back(obj);
		}
		else {
			ctx.OpaqueSurfaces.push_back(obj);
		}
	}

	Node::Draw(topMatrix, ctx);
}