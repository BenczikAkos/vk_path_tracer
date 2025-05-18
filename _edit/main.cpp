// Copyright 2020-2024 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0
#include <array>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <nvh/fileoperations.hpp>  // For nvh::loadFile
#include <nvvk/context_vk.hpp>
#include <nvvk/descriptorsets_vk.hpp>  // For nvvk::DescriptorSetContainer
#include <nvvk/error_vk.hpp>
#include <nvvk/raytraceKHR_vk.hpp>        // For nvvk::RaytracingBuilderKHR
#include <nvvk/resourceallocator_vk.hpp>  // For NVVK memory allocators
#include <nvvk/shaders_vk.hpp>            // For nvvk::createShaderModule
#include <nvh/appwindowprofiler.hpp>
#include <nvvk/swapchain_vk.hpp>
#include <nvvk/commands_vk.hpp>
#include <nvvk/pipeline_vk.hpp>

static const uint64_t render_width     = 800;
static const uint64_t render_height    = 600;
static const uint32_t workgroup_width  = 16;
static const uint32_t workgroup_height = 8;

VkCommandBuffer AllocateAndBeginOneTimeCommandBuffer(VkDevice device, VkCommandPool cmdPool)
{
  VkCommandBufferAllocateInfo cmdAllocInfo{.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           .commandPool        = cmdPool,
                                           .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           .commandBufferCount = 1};
  VkCommandBuffer             cmdBuffer;
  NVVK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuffer));
  VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                     .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  NVVK_CHECK(vkBeginCommandBuffer(cmdBuffer, &beginInfo));
  return cmdBuffer;
}

void EndSubmitWaitAndFreeCommandBuffer(VkDevice device, VkQueue queue, VkCommandPool cmdPool, VkCommandBuffer& cmdBuffer)
{
  NVVK_CHECK(vkEndCommandBuffer(cmdBuffer));
  VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmdBuffer};
  NVVK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
  NVVK_CHECK(vkQueueWaitIdle(queue));
  vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
}

VkDeviceAddress GetBufferDeviceAddress(VkDevice device, VkBuffer buffer)
{
  VkBufferDeviceAddressInfo addressInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer};
  return vkGetBufferDeviceAddress(device, &addressInfo);
}

class PathTracerWindow : public nvh::AppWindowProfiler
{
public:
  PathTracerWindow() : AppWindowProfiler(false) {}

  bool begin() override
  {
    // Initialize Vulkan context
    nvvk::ContextCreateInfo deviceInfo;
    deviceInfo.apiMajor = 1;
    deviceInfo.apiMinor = 2;
    deviceInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    m_context.init(deviceInfo);

    // Create swapchain
    m_swapchain.init(m_context);
    m_swapchain.update(m_windowState.m_winSize[0], m_windowState.m_winSize[1]);

    // Create descriptor pool
    m_descriptorPool.init(m_context, 1000, 1000);

    // Create command pool
    VkCommandPoolCreateInfo cmdPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmdPoolInfo.queueFamilyIndex = m_context.m_queueGCT;
    vkCreateCommandPool(m_context, &cmdPoolInfo, nullptr, &m_cmdPool);

    // Create render pass
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchain.getFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    vkCreateRenderPass(m_context, &renderPassInfo, nullptr, &m_renderPass);

    // Create framebuffers
    createFramebuffers();

    // Create pipeline
    createPipeline();

    return true;
  }

  void end() override
  {
    vkDeviceWaitIdle(m_context);
    destroyFramebuffers();
    vkDestroyRenderPass(m_context, m_renderPass, nullptr);
    vkDestroyPipeline(m_context, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_context, m_pipelineLayout, nullptr);
    vkDestroyCommandPool(m_context, m_cmdPool, nullptr);
    m_descriptorPool.deinit();
    m_swapchain.deinit();
    m_context.deinit();
  }

  void think(double time) override
  {
    // Render frame
    VkCommandBuffer cmd = m_swapchain.createCommandBuffer(m_cmdPool);
    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_framebuffers[m_swapchain.getActiveImageIndex()];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain.getExtent();
    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0); // Draw fullscreen triangle
    vkCmdEndRenderPass(cmd);

    m_swapchain.presentCommandBuffer(cmd);
  }

  void resize(int width, int height) override
  {
    vkDeviceWaitIdle(m_context);
    destroyFramebuffers();
    m_swapchain.update(width, height);
    createFramebuffers();
  }

private:
  void createFramebuffers()
  {
    m_framebuffers.resize(m_swapchain.getImageCount());
    for(uint32_t i = 0; i < m_swapchain.getImageCount(); i++)
    {
      VkImageView attachments[] = {m_swapchain.getImageView(i)};
      VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      framebufferInfo.renderPass = m_renderPass;
      framebufferInfo.attachmentCount = 1;
      framebufferInfo.pAttachments = attachments;
      framebufferInfo.width = m_swapchain.getExtent().width;
      framebufferInfo.height = m_swapchain.getExtent().height;
      framebufferInfo.layers = 1;
      vkCreateFramebuffer(m_context, &framebufferInfo, nullptr, &m_framebuffers[i]);
    }
  }

  void destroyFramebuffers()
  {
    for(auto framebuffer : m_framebuffers)
    {
      vkDestroyFramebuffer(m_context, framebuffer, nullptr);
    }
    m_framebuffers.clear();
  }

  void createPipeline()
  {
    // Create shader modules
    VkShaderModule vertShader = nvvk::createShaderModule(m_context, nvh::loadFile("shaders/fullscreen.vert.spv", true, searchPaths));
    VkShaderModule fragShader = nvvk::createShaderModule(m_context, nvh::loadFile("shaders/fullscreen.frag.spv", true, searchPaths));

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    vkCreatePipelineLayout(m_context, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);

    // Create graphics pipeline
    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShader, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShader, "main", nullptr}};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;

    vkCreateGraphicsPipelines(m_context, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);

    vkDestroyShaderModule(m_context, vertShader, nullptr);
    vkDestroyShaderModule(m_context, fragShader, nullptr);
  }

  nvvk::Context m_context;
  nvvk::SwapChain m_swapchain;
  nvvk::DescriptorPool m_descriptorPool;
  VkCommandPool m_cmdPool;
  VkRenderPass m_renderPass;
  std::vector<VkFramebuffer> m_framebuffers;
  VkPipeline m_pipeline;
  VkPipelineLayout m_pipelineLayout;
};

int main(int argc, const char** argv)
{
  PathTracerWindow window;
  return window.run("Vulkan Path Tracer", argc, argv, 800, 600, false);
}
