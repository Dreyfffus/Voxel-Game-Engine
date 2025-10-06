#pragma once

#include <vk_types.h>

#include <deque>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <vk_mem_alloc.h>

#include <camera.h>
#include <vk_descriptors.h>
#include <vk_loader.h>
#include <vk_pipelines.h>
struct MeshAsset;
namespace fastgltf {
	struct Mesh;
}
class VulkanEngine;

struct EngineStats {
	float frametime;
	int triangle_count;
	int drawcall_count;
	int scene_update_time;
	float mesh_draw_time;
};

struct RenderObject {
	uint32_t indexCount;
	uint32_t firstIndex;
	VkBuffer indexBuffer;
	MaterialInstance* material;
	Bounds bounds;
	glm::mat4 transform;
	VkDeviceAddress vertexBufferAddress;
};

struct FrameData {	
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
	VkSemaphore swapchainSemaphore, renderSemaphore;
	VkFence renderFence;
	DynamicDescriptorAllocator descriptorAllocator;
	DeletionQueue deletionQueue;
};

struct ComputeEffect {
	const char* name;
	VkPipeline pipeline;
	VkPipelineLayout layout;
	PushConstants data;
};

struct GPUSceneData {
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 viewproj;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection;
	glm::vec4 sunlightColor;
};	

struct MeshNode : public Node {
	std::shared_ptr<MeshAsset> mesh;
	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
};

struct DrawContext {
	std::vector<RenderObject> OpaqueSurfaces;
	std::vector<RenderObject> TransparentSurfaces;
};

constexpr unsigned int FRAME_OVERLAP = 2;

struct GLTFMetallic_Roughness {
	MaterialPipeline opaquePipeline;
	MaterialPipeline transparentPipeline;

	VkDescriptorSetLayout materialLayout;

	struct MaterialConstants {
		glm::vec4 colorFactor;
		glm::vec4 metalRoughFactor;
		glm::vec4 extra[14];
	};

	struct MaterialResources {
		AllocatedImage colorImage;
		VkSampler colorSampler;
		AllocatedImage metalRoughImage;
		VkSampler metalRoughSampler;
		VkBuffer dataBuffer;
		uint32_t bufferOffset;
	};

	DescriptorWriter writer;

	void buildPipelines(VulkanEngine* engine);
	void clearResources(VkDevice device);

	MaterialInstance writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources& resources, DynamicDescriptorAllocator& allocator);
};

class VulkanEngine {
public:
	EngineStats stats;
	Camera camera;
	DrawContext drawContext;
	std::unordered_map<std::string, std::shared_ptr<Node>> loadedNodes;
	FrameData frames[FRAME_OVERLAP];
	FrameData& get_current_frame() { return frames[frameNumber % FRAME_OVERLAP]; }
	VkQueue graphicsQueue;
	uint32_t graphicsQueueFamily;
	bool isInitialized{ false };
	int frameNumber{ 0 };
	bool stopRendering{ false };
	bool resizeRequest{ false };
	VkExtent2D windowExtent{ 1100, 900 };
	struct SDL_Window* window{ nullptr };
	AllocatedImage drawImage;
	AllocatedImage depthImage;
	AllocatedImage whiteImage;
	AllocatedImage blackImage;
	AllocatedImage greyImage;
	AllocatedImage errorTexture;
	VkSampler defaultSamplerLinear;
	VkSampler defaultSamplerNearest;
	VkExtent2D drawExtent;
	float renderScale{ 1.0f };
	VkInstance instance;
	VkDebugUtilsMessengerEXT debugMessenger;
	VkPhysicalDevice chosenGPU;
	VkDevice driver;
	VkSurfaceKHR surface;
	VkSwapchainKHR swapchain;
	VkFormat swapchainImageFormat;
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	VkExtent2D swapchainExtent;
	DeletionQueue mainDeletionQueue;
	VmaAllocator allocator;
	DynamicDescriptorAllocator globalDescriptorAllocator;
	VkDescriptorSet drawImageDescriptors;
	VkDescriptorSetLayout drawImageDescriptorLayout;
	VkDescriptorSetLayout singleImageDescriptorLayout;
	VkPipeline  currentPipeline;
	VkPipelineLayout backgroundPipelineLayout;
	VkPipelineLayout meshPipelineLayout;
	VkPipeline meshPipeline;
	VkFence immFence;
	VkCommandBuffer immCmdBuffer;
	VkCommandPool immCmdPool;
	GPUSceneData sceneData;
	VkDescriptorSetLayout gpuSceneDescriptorSetLayout;
	std::vector<ComputeEffect> backgroundEffects;
	int currentBackgroundEffect{ 0 };
	MaterialInstance defaultMat;
	GLTFMetallic_Roughness metalRoughMat;
	std::vector<std::shared_ptr<MeshAsset>> sceneMeshes;
	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;


	static VulkanEngine& get();
	void init();
	void cleanup();
	void draw();
	void run();
	void immediate_cmd(std::function<void(VkCommandBuffer cmd)>&& function);
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
	AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags flags, VmaMemoryUsage memoryUsage);
	AllocatedImage createImage(VkExtent3D size, VkFormat format, VkImageUsageFlags flags, bool mipmapped = false);
	AllocatedImage createImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	void destroyImage(const AllocatedImage& image);
	void destroyBuffer(const AllocatedBuffer& buffer);

private:

	void initVulkan();
	void updateScene();
	void initSwapchain();
	void initCommands();
	void initSyncStructures();
	void createSwapchain(uint32_t width, uint32_t height);
	void drawBackground(VkCommandBuffer cmd);
	void initDescriptors();
	void initPipelines();
	void initGradientPipelines();
	void initMeshPipeline();
	void initImGui();
	void resizeSwapchain();
	void drawImGui(VkCommandBuffer cmd, VkImageView targetImageview);
	void drawMesh(VkCommandBuffer cmd);
	void init_default_data();
	void destroySwapchain();
};





