#include <nvvk/context_vk.hpp>
#include <nvvk/resourceallocator_vk.hpp>
#include <nvvk/error_vk.hpp>

static const uint64_t render_width = 800;
static const uint64_t render_height = 600;

int main(int argc, const char** argv)
{
	std::vector<const char*> extensions = {
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
	};
	nvvk::ContextCreateInfo deviceInfo;
	deviceInfo.apiMajor = 1;
	deviceInfo.apiMinor = 4;
	for (const auto& ext : extensions) {
		deviceInfo.addDeviceExtension(ext);
	}
	nvvk::Context context;               // Encapsulates device state in a single object
	context.init(deviceInfo);            // Initialize the context

	nvvk::ResourceAllocatorDedicated allocator;
	allocator.init(context, context.m_physicalDevice);
	////
	// BUFFER ALLOCATION
	////
	VkDeviceSize bufferSizeBytes = render_width * render_height * 3 * sizeof(float);
	VkBufferCreateInfo bufferCreateInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
										.size  = bufferSizeBytes,
										.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
	nvvk::Buffer buffer = allocator.createBuffer(bufferCreateInfo,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	////
	//COMMAND POOL
	////
	VkCommandPoolCreateInfo cmdPoolInfo{.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
									.queueFamilyIndex = context.m_queueGCT};
	VkCommandPool cmdPool;
	NVVK_CHECK(vkCreateCommandPool(context, &cmdPoolInfo, nullptr, &cmdPool));
	////
	// Allocate a command buffer
	////
	VkCommandBufferAllocateInfo cmdAllocInfo{.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
											 .commandPool        = cmdPool,
											 .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
											 .commandBufferCount = 1};
	VkCommandBuffer             cmdBuffer;
	NVVK_CHECK(vkAllocateCommandBuffers(context, &cmdAllocInfo, &cmdBuffer));
	////
	// BEGIN RECORDING
	////
	VkCommandBufferBeginInfo beginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	NVVK_CHECK(vkBeginCommandBuffer(cmdBuffer, &beginInfo));
	////
	// FILL THE BUFFER
	////
	const float fillValue = 0.5f;
	const uint32_t& fillValueU32 = reinterpret_cast<const uint32_t&>(fillValue);
	vkCmdFillBuffer(cmdBuffer, buffer.buffer, 0, bufferSizeBytes, fillValueU32);
	////
	// ADD MEMORY BARRIER
	////
	VkMemoryBarrier memoryBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
									.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
									.dstAccessMask = VK_ACCESS_HOST_READ_BIT,};
	vkCmdPipelineBarrier(cmdBuffer,
						VK_PIPELINE_STAGE_TRANSFER_BIT,
						VK_PIPELINE_STAGE_HOST_BIT,
						0, 1, &memoryBarrier,
						0, nullptr,
						0, nullptr);
	////
	// END RECORDING
	////
	NVVK_CHECK(vkEndCommandBuffer(cmdBuffer));
	////
	// SUBMITTING COMMAND
	////
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
								.commandBufferCount = 1,
								.pCommandBuffers = &cmdBuffer,};
	NVVK_CHECK(vkQueueSubmit(context.m_queueGCT, 1, &submitInfo, VK_NULL_HANDLE));

	////
	// WAITING FOR THE GPU TO FINISH
	////
	NVVK_CHECK(vkQueueWaitIdle(context.m_queueGCT));

	////
	//PRINT OUT BUFFER DATA FIRST 3 ELEMENTS
	////
	void *data = allocator.map(buffer);
	float *fltData = reinterpret_cast<float *>(data);
	printf("First three elements: %f, %f, %f\n", fltData[0], fltData[1], fltData[2]);
	allocator.unmap(buffer);

	////
	// TIDYING UP
	////
	vkFreeCommandBuffers(context, cmdPool, 1, &cmdBuffer);
	vkDestroyCommandPool(context, cmdPool, nullptr);
	allocator.destroy(buffer);
	allocator.deinit();
	context.deinit();
}