#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <math.h>
#include <algorithm>
#include <functional>
#include <deque>
#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"
#include <vk_mem_alloc.h>
#include <fmt/core.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#define VK_CHECK(x) do { VkResult err = x; if (err) { fmt::print("[VULKAN ERROR]: {} at file {} line {}", string_VkResult(err), __FILE__, __LINE__); abort(); }} while (0);
#define VK_CHECK_RESULT(x) vk_check(x, __FILE__, __LINE__);

inline VkResult vk_check(VkResult x, const char* file, int line ) {
	if (x != VK_SUCCESS) {
		fmt::print("[VULKAN ERROR]: {} at line {} in file {}", string_VkResult(x), line, file);
	}
	return x;
}

struct DeletionQueue {
	std::deque<std::function<void()>> deletors;
	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)();
		}

		deletors.clear();
	}
};

struct AllocatedImage {
	VkImage image;
	VkImageView imageView;
	VmaAllocation allocation;
	VkExtent3D imageExtent;
	VkFormat imageFormat;
};

struct PushConstants {
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct AllocatedBuffer {
	VkBuffer buffer;
	VmaAllocation allocation;
	VmaAllocationInfo allocInfo;
};

struct Vertex {
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
};

struct GPUMeshBuffers {
	AllocatedBuffer indexBuffer;
	AllocatedBuffer vertexBuffer;
	VkDeviceAddress vertexBufferAddress;
};

struct GPUDrawPushConstants {
	glm::mat4 worldMatrix;
	VkDeviceAddress vertexBuffer;
};

enum class MaterialPass :uint8_t {
	MAIN_COLOR, TRASNPARENT, OTHER
};

struct MaterialPipeline {
	VkPipeline pipeline;
	VkPipelineLayout layout;
};

struct MaterialInstance {
	MaterialPipeline* pipeline;
	VkDescriptorSet materialSet;
	MaterialPass passType;
};

struct DrawContext;

class IRenderable {
	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

struct Node : public IRenderable {
	std::weak_ptr<Node> parent;
	std::vector<std::shared_ptr<Node>> children;
	glm::mat4 localTransform;
	glm::mat4 worldTransform;

	void refreshTransform(const glm::mat4& parentMatrix) {
		worldTransform = parentMatrix * localTransform;
		for (auto c : children) {
			c->refreshTransform(worldTransform);
		}
	}

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
		for (auto& c : children) {
			c->Draw(topMatrix, ctx);
		}
	}
};
