#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <assert.h>
#include <sys/time.h>
#include <vulkan/vulkan.h>
#include <x265.h>

#define VK_CHECK_RESULT(f) 																				\
{																										\
    VkResult res = (f);																					\
    if (res != VK_SUCCESS)																				\
    {																									\
        printf("Fatal : VkResult is %d in %s at line %d\n", res,  __FILE__, __LINE__);                  \
        assert(res == VK_SUCCESS);																		\
    }																									\
}

typedef char const *cstr_t;
typedef cstr_t *cstrarr_t;

typedef struct {
    float x;
    float y;
} Vec2;

typedef struct {
    float x;
    float y;
    float z;
} Vec3;

typedef struct {
    Vec2 pos;
    Vec3 color;
} Vertex;

typedef void (*callback_t)(const char *, VkDeviceSize);
typedef void (*ycbcr_callback_t)(void *y, void *cb, void *cr);

typedef struct {
    VkQueue queue;
    uint32_t queueFamilyIndex;

    VkShaderModule shader;

    VkFormat format;

    VkImage y;
    VkDeviceMemory yMemory;
    VkImageView yView;

    VkImage cb;
    VkDeviceMemory cbMemory;
    VkImageView cbView;

    VkImage cr;
    VkDeviceMemory crMemory;
    VkImageView crView;

    VkImageView inputView;
    VkFence fence;
    VkCommandBuffer commandBuffer;
    VkCommandPool commandPool;
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkDescriptorSet descriptorSet;
    VkDescriptorSetLayout descriptorSetLayout;

    ycbcr_callback_t callback;
} YCbCr;

typedef struct {
    VkInstance instance;
    VkPhysicalDevice gpu;
    uint32_t graphicsQueueFamilyIndex;
    VkDevice device;
    VkCommandPool commandPool;

    VkFormat format;
    uint32_t width;
    uint32_t height;

    VkRenderPass renderPass;
    VkImage srcImage;
    VkDeviceMemory srcImageMemory;
    VkImageView srcImageView;
    VkFramebuffer framebuffer;
    VkCommandBuffer renderCommandBuffer;
    VkPipelineLayout pipelineLayout;
    VkShaderModule vertShader;
    VkShaderModule fragShader;
    VkPipeline pipeline;
    VkRect2D rect;
    VkBuffer vertexBuffer;
    size_t vertexCount;
    VkDeviceMemory vertexBufferMemory;
    VkImage dstImage;
    VkDeviceMemory dstImageMemory;
    VkCommandBuffer copyCommandBuffer;
    VkQueue graphicQueue;
    VkFence renderFence;
    VkFence copyFence;
    callback_t callback;

    YCbCr ycbcr;
} Elham;


uint32_t const width = 50;
uint32_t const height = 50;

Vertex vertices[] = {
    {.pos = {-1.0f, -1.0f}, .color = {1.0f, 0.0f, 0.0f}},
    {.pos = {1.0f, 1.0f}, .color = {0.0f, 1.0f, 0.0f}},
    {.pos = {-1.0f, 1.0f}, .color = {0.0f, 0.0f, 1.0f}}
};

void rotateVec2(Vec2 center, float angle, Vec2 *p) {
    float s = sinf(angle);
    float c = cosf(angle);

    // translate point back to origin:
    p->x -= center.x;
    p->y -= center.y;

    // rotate point
    float xnew = p->x * c - p->y * s;
    float ynew = p->x * s + p->y * c;

    // translate point back:
    p->x = xnew + center.x;
    p->y = ynew + center.y;
}

void checkValidationLayerSupport(uint32_t *count, cstrarr_t *layers) {
    uint32_t cnt;

    vkEnumerateInstanceLayerProperties(&cnt, NULL);
    printf("Validation layers:\n");

    VkLayerProperties *lyrs = malloc(cnt * sizeof(VkLayerProperties));
    vkEnumerateInstanceLayerProperties(&cnt, lyrs);
    cstrarr_t names = malloc(cnt * sizeof(cstr_t));

    for (int i = 0; i < cnt; i++) {
        VkLayerProperties *lyr = (lyrs + i);
        uint32_t version = lyr->specVersion;

        printf(
            "%02d %s %d.%d (%s)\n",
            i + 1,
            lyr->layerName,
            VK_VERSION_MAJOR(version),
            VK_VERSION_MINOR(version),
            lyr->description
        );
        names[i] = lyr->layerName;
    }

    *layers = names;
    *count = cnt;
}

void createInstance(Elham *e) {
    VkInstance instance;
    uint32_t validationLayerCount = 0;
    cstr_t *validationLayers;
    checkValidationLayerSupport(&validationLayerCount, &validationLayers);

//    uint32_t extensionCount = 0;
//    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
//    VkExtensionProperties *extensions = calloc(extensionCount, sizeof(VkExtensionProperties));
//    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, extensions);
//    printf("Instance extensions:\n");
//    for (int i = 0; i < extensionCount; i++) {
//        VkExtensionProperties *ext = extensions + i;
//        uint32_t version = ext->specVersion;
//        printf(
//            "%02d %s %d\n",
//            i + 1,
//            ext->extensionName,
//            version
//        );
//    }
//    free(extensions);

    printf("Create Vulkan instance...");
    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ElhamC";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo inf = {0};
    inf.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inf.pApplicationInfo = &app;
    inf.enabledExtensionCount = 0;
    inf.ppEnabledExtensionNames = NULL;
    inf.enabledLayerCount = 1;
    char const *validations[] = {"VK_LAYER_KHRONOS_validation"}; //, "VK_LAYER_LUNARG_api_dump"};
    inf.ppEnabledLayerNames = validations;
    if (vkCreateInstance(&inf, NULL, &instance) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    e->instance = instance;
}

void pickPhysicalDevice(Elham *engine) {
    printf("Pick up a GPU...\n");
    VkInstance instance = engine->instance;

    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    VkPhysicalDevice *devices = malloc(deviceCount * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices);
    printf("Physical devices:\n");
    for (int i = 0; i < deviceCount; i++) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);

        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(devices[i], &deviceFeatures);

        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            gpu = devices[i];
        }

        printf(
            "%02d %s %s\n",
            i + 1,
            deviceProperties.deviceName,
            deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "(Discrete)" : ""
        );
    }
    if (gpu == VK_NULL_HANDLE) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    engine->gpu = gpu;
}

uint32_t pickQueueFamily(VkPhysicalDevice gpu, VkQueueFlagBits bits) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, NULL);
    VkQueueFamilyProperties *queueFamilies = malloc(count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, queueFamilies);
    for (int i = 0; i < count; i++) {
        if (queueFamilies[i].queueFlags & bits) {
            free(queueFamilies);
            return i;
        }
    }

    free(queueFamilies);
    printf("failed.\n");
    exit(EXIT_FAILURE);
}

void pickGraphicsQueueFamily(Elham *e) {
    printf("Pick graphics queue...");
    e->graphicsQueueFamilyIndex = pickQueueFamily(e->gpu, VK_QUEUE_GRAPHICS_BIT);
    printf("done.\n");
}

void pickComputeQueueFamily(Elham *e) {
    VkPhysicalDevice gpu = e->gpu;
    printf("Pick compute queue...");
    e->ycbcr.queueFamilyIndex = pickQueueFamily(e->gpu, VK_QUEUE_COMPUTE_BIT);
    printf("done.\n");
}

void createDevice(Elham *e) {
    VkPhysicalDevice gpu = e->gpu;
    uint32_t qfi = e->graphicsQueueFamilyIndex;
    VkDevice device;

    printf("Create device...");
    VkDeviceQueueCreateInfo queueInfo = {0};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = qfi;
    queueInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueInfo.pQueuePriorities = &queuePriority;
    VkDeviceCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pQueueCreateInfos = &queueInfo;
    info.queueCreateInfoCount = 1;
    VkPhysicalDeviceFeatures deviceFeatures = {0};
    info.pEnabledFeatures = &deviceFeatures;
    if (vkCreateDevice(gpu, &info, NULL, &device) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }

    e->device = device;
    vkGetDeviceQueue(e->device, e->graphicsQueueFamilyIndex, 0, &e->graphicQueue);
    vkGetDeviceQueue(e->device, e->ycbcr.queueFamilyIndex, 0, &e->ycbcr.queue);

    printf("done.\n");
}

void readFile(char const *fileName, const char **buffer, long *length) {
    FILE *f;
    char *buff;
    long len;

    f = fopen(fileName, "rb");
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    rewind(f);

    buff = (char *) malloc(len * sizeof(char));
    fread(buff, len, 1, f);
    fclose(f);

    *buffer = buff;
    *length = len;
}

VkShaderModule createShader(VkDevice device, char const *filename) {
    char const *code;
    long _size;
    readFile(filename, &code, &_size);

    VkShaderModuleCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = _size;
    createInfo.pCode = (const uint32_t *) code;
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, NULL, &shaderModule) != VK_SUCCESS) {
        printf("Failed to create shader module.");
    }

    return shaderModule;
}

void createRenderPass(Elham *e) {
    VkDevice device = e->device;
    VkFormat format = e->format;

    printf("Create render pass...");
    VkAttachmentDescription src = {0};
    src.format = format;
    src.samples = VK_SAMPLE_COUNT_1_BIT;
    src.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    src.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    src.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    src.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference srcRef = {0};
    srcRef.attachment = 0;
    srcRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &srcRef;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &src;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    VkRenderPass renderPass;
    if (vkCreateRenderPass(device, &info, NULL, &renderPass) != VK_SUCCESS) {
        printf("failed.\n");
    }
    printf("done.\n");

    e->renderPass = renderPass;
}

void createPipelineLayout(Elham *e) {
    VkDevice device = e->device;

    printf("Create pipeline layout...");
    VkPipelineLayout pipelineLayout;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = NULL;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = NULL;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &pipelineLayout) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    e->pipelineLayout = pipelineLayout;
}

void createPipeline(Elham *e) {
    VkDevice device = e->device;
    VkShaderModule vertShader = e->vertShader;
    VkShaderModule fragShader = e->fragShader;
    VkPipelineLayout pipelineLayout = e->pipelineLayout;
    VkRenderPass renderPass = e->renderPass;

    printf("Create pipeline...");

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {0};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShader;
    vertShaderStageInfo.pName = "main";
    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {0};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShader;
    fragShaderStageInfo.pName = "main";
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkVertexInputBindingDescription vertBindDesc = {0};
    vertBindDesc.binding = 0;
    vertBindDesc.stride = sizeof(Vertex);
    vertBindDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertAttrDesc[2] = {}; //malloc(2 * sizeof(VkVertexInputAttributeDescription));
    vertAttrDesc[0].binding = 0;
    vertAttrDesc[0].location = 0;
    vertAttrDesc[0].format = VK_FORMAT_R32G32_SFLOAT;
    vertAttrDesc[0].offset = offsetof(Vertex, pos);

    vertAttrDesc[1].binding = 0;
    vertAttrDesc[1].location = 1;
    vertAttrDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertAttrDesc[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &vertBindDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = vertAttrDesc;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {0};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) e->rect.extent.width;
    viewport.height = (float) e->rect.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor = {0};
    scissor.extent.width = e->rect.extent.width;
    scissor.extent.height = e->rect.extent.height;
    VkPipelineViewportStateCreateInfo viewportState = {0};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {0};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = {0};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = NULL;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {0};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo colorBlending = {0};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    VkGraphicsPipelineCreateInfo pipelineInfo = {0};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = NULL;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = NULL;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    e->pipeline = pipeline;
}

void createImageView(Elham *e) {
    VkDevice device = e->device;
    VkImage image = e->srcImage;
    VkFormat format = e->format;

    VkImageView imageView;
    printf("Create image view...");
    VkImageViewCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &info, NULL, &imageView) != VK_SUCCESS) {
        printf("failed.\n");
    }
    printf("done.\n");
    e->srcImageView = imageView;
}

void createYImageView(Elham *e) {
    VkDevice device = e->device;
    VkImage image = e->ycbcr.y;
    VkFormat format = e->ycbcr.format;

    VkImageView view;
    printf("Create Y' image view...");
    VkImageViewCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &info, NULL, &view) != VK_SUCCESS) {
        printf("failed.\n");
    }
    printf("done.\n");
    e->ycbcr.yView = view;
}

void createCbImageView(Elham *e) {
    VkDevice device = e->device;
    VkImage image = e->ycbcr.cb;
    VkFormat format = e->ycbcr.format;

    VkImageView view;
    printf("Create Cb image view...");
    VkImageViewCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &info, NULL, &view) != VK_SUCCESS) {
        printf("failed.\n");
    }
    printf("done.\n");
    e->ycbcr.cbView = view;
}

void createCrImageView(Elham *e) {
    VkDevice device = e->device;
    VkImage image = e->ycbcr.cr;
    VkFormat format = e->ycbcr.format;

    VkImageView view;
    printf("Create Cr image view...");
    VkImageViewCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &info, NULL, &view) != VK_SUCCESS) {
        printf("failed.\n");
    }
    printf("done.\n");
    e->ycbcr.crView = view;
}

void createInImageView(Elham *e) {
    VkDevice device = e->device;
    VkImage image = e->dstImage;
    VkFormat format = e->format;

    VkImageView view;
    printf("Create Y'CbCr input image view...");
    VkImageViewCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &info, NULL, &view) != VK_SUCCESS) {
        printf("failed.\n");
    }
    printf("done.\n");
    e->ycbcr.inputView = view;
}

void createFramebuffer(Elham *e) {
    VkDevice device = e->device;
    VkRenderPass renderPass = e->renderPass;

    printf("Create framebuffer...");
    VkFramebuffer framebuffer;

    VkFramebufferCreateInfo framebufferInfo = {0};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &e->srcImageView;
    framebufferInfo.width = e->width;
    framebufferInfo.height = e->height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, NULL, &framebuffer) != VK_SUCCESS) {
        printf("Failed.\n");
    }

    e->framebuffer = framebuffer;
    printf("done.\n");
}

uint32_t findMemoryType(VkPhysicalDevice gpu, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(gpu, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    printf("Failed to find suitable memory type.");
    exit(EXIT_FAILURE);
}

void createSrcImage(Elham *e) {
    VkPhysicalDevice gpu = e->gpu;
    VkDevice device = e->device;
    VkFormat format = e->format;

    printf("Create source image...");
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(gpu, format, &formatProperties);
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) {
        printf("Can not copy from a non-linear image\n.");
        exit(EXIT_FAILURE);
    }
    if (!(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
        printf("(Can not use a non-linear image as color attachment) ");
    }

    VkImageCreateInfo imageInfo = {0};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = e->width;
    imageInfo.extent.height = e->height;
    imageInfo.extent.depth = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage _image;
    if (vkCreateImage(device, &imageInfo, NULL, &_image) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    printf("Allocating memory for source image...");
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, _image, &memRequirements);
    VkMemoryAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(gpu, memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory _mem;
    if (vkAllocateMemory(device, &allocInfo, NULL, &_mem) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    vkBindImageMemory(device, _image, _mem, 0);

    e->srcImage = _image;
    e->srcImageMemory = _mem;
}

void createCommandPool(Elham *e) {
    VkDevice device = e->device;
    uint32_t qfi = e->graphicsQueueFamilyIndex;

    printf("Create command pool...");
    VkCommandPool commandPool;
    VkCommandPoolCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.queueFamilyIndex = qfi;
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // =0
    if (vkCreateCommandPool(device, &info, NULL, &commandPool) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    e->commandPool = commandPool;
}

void insertImageMemoryBarrier(
    VkCommandBuffer buffer,
    VkImage image,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    VkImageLayout oldImageLayout,
    VkImageLayout newImageLayout,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask) {
    VkImageSubresourceRange subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldImageLayout;
    barrier.newLayout = newImageLayout;
    barrier.image = image;
    barrier.subresourceRange = subresourceRange;

    vkCmdPipelineBarrier(
        buffer,
        srcStageMask,
        dstStageMask,
        0,
        0, NULL,
        0, NULL,
        1, &barrier);
}


void createCommandBuffer(Elham *e) {
    VkDevice device = e->device;
    VkCommandPool commandPool = e->commandPool;

    printf("Create command buffer...");
    VkCommandBuffer buffer;
    VkCommandBufferAllocateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = commandPool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &info, &buffer) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");
    e->renderCommandBuffer = buffer;
}

void createCopyCommandBuffer(Elham *e) {
    VkDevice device = e->device;
    VkCommandPool commandPool = e->commandPool;

    printf("Create command buffer...");
    VkCommandBuffer buffer;
    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocInfo, &buffer) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");
    e->copyCommandBuffer = buffer;
}

void recordRenderCommands(Elham *e) {
    VkCommandBuffer buffer = e->renderCommandBuffer;
    VkRenderPass renderPass = e->renderPass;
    VkRect2D rect = e->rect;
    VkPipeline pipeline = e->pipeline;
    VkFramebuffer framebuffer = e->framebuffer;
    size_t vertexCount = e->vertexCount;
    VkBuffer vertexBuffer = e->vertexBuffer;

    printf("Begin command buffer...");
    VkCommandBufferBeginInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = 0;
    info.pInheritanceInfo = NULL;
    if (vkBeginCommandBuffer(buffer, &info) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    printf("Recording commands...");
    VkRenderPassBeginInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea = rect;
    VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    vkCmdBeginRenderPass(buffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(buffer, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(buffer, vertexCount, 1, 0, 0);
    vkCmdEndRenderPass(buffer);

    if (vkEndCommandBuffer(buffer) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");
}

void createDstImage(Elham *e) {
    VkImageUsageFlagBits usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    VkPhysicalDevice gpu = e->gpu;
    VkDevice device = e->device;
    VkFormat format = e->format;

    VkImage image;
    VkDeviceMemory memory;

    printf("Create destination image...");
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(gpu, format, &formatProperties);
    if (!(formatProperties.linearTilingFeatures & usage)) {
        printf("Linear tiling can't be copied into + passed to compute shader.\n");
        exit(EXIT_FAILURE);
    }

    VkImageCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = e->rect.extent.width;
    info.extent.height = e->rect.extent.height;
    info.extent.depth = 1;
    info.arrayLayers = 1;
    info.mipLevels = 1;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_LINEAR;
    info.usage = usage;
    if (vkCreateImage(device, &info, NULL, &image) != VK_SUCCESS) {
        printf("failed.");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, image, &req);

    VkMemoryAllocateInfo alloc = {0};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = findMemoryType(gpu, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    printf("Allocating memory for destination image...");
    if (vkAllocateMemory(device, &alloc, NULL, &memory) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    e->dstImage = image;
    e->dstImageMemory = memory;
}

void createYImage(Elham *e) {
    VkPhysicalDevice gpu = e->gpu;
    VkDevice device = e->device;
    VkFormat format = e->ycbcr.format;

    VkImage image;
    VkDeviceMemory memory;

    printf("Create Y' image...");
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(gpu, format, &formatProperties);
    if (!(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
        printf("Can not store image with this format.\n");
        exit(EXIT_FAILURE);
    }

    VkImageCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = e->rect.extent.width;
    info.extent.height = e->rect.extent.height;
    info.extent.depth = 1;
    info.arrayLayers = 1;
    info.mipLevels = 1;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_LINEAR;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT;
    if (vkCreateImage(device, &info, NULL, &image) != VK_SUCCESS) {
        printf("failed.");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, image, &req);

    VkMemoryAllocateInfo alloc = {0};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = findMemoryType(gpu, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    printf("Allocating memory for Y' image...");
    if (vkAllocateMemory(device, &alloc, NULL, &memory) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    e->ycbcr.y = image;
    e->ycbcr.yMemory = memory;
}

void createCbImage(Elham *e) {
    VkPhysicalDevice gpu = e->gpu;
    VkDevice device = e->device;
    VkFormat format = e->ycbcr.format;

    VkImage image;
    VkDeviceMemory memory;

    printf("Create Cb image...");
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(gpu, format, &formatProperties);
    if (!(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
        printf("Can not store image with this format.\n");
        exit(EXIT_FAILURE);
    }

    VkImageCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = (e->rect.extent.width) / 2;
    info.extent.height = (e->rect.extent.height) / 2;
    info.extent.depth = 1;
    info.arrayLayers = 1;
    info.mipLevels = 1;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_LINEAR;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT;
    if (vkCreateImage(device, &info, NULL, &image) != VK_SUCCESS) {
        printf("failed.");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, image, &req);

    VkMemoryAllocateInfo alloc = {0};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = findMemoryType(gpu, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    printf("Allocating memory for Cb image...");
    if (vkAllocateMemory(device, &alloc, NULL, &memory) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    e->ycbcr.cb = image;
    e->ycbcr.cbMemory = memory;
}

void createCrImage(Elham *e) {
    VkPhysicalDevice gpu = e->gpu;
    VkDevice device = e->device;
    VkFormat format = e->ycbcr.format;

    VkImage image;
    VkDeviceMemory memory;

    printf("Create Cr image...");
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(gpu, format, &formatProperties);
    if (!(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
        printf("Can not store image with this format.\n");
        exit(EXIT_FAILURE);
    }

    VkImageCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = (e->rect.extent.width) / 2;
    info.extent.height = (e->rect.extent.height) / 2;
    info.extent.depth = 1;
    info.arrayLayers = 1;
    info.mipLevels = 1;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_LINEAR;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT;
    if (vkCreateImage(device, &info, NULL, &image) != VK_SUCCESS) {
        printf("failed.");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, image, &req);

    VkMemoryAllocateInfo alloc = {0};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = findMemoryType(gpu, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    printf("Allocating memory for Cr image...");
    if (vkAllocateMemory(device, &alloc, NULL, &memory) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    e->ycbcr.cr = image;
    e->ycbcr.crMemory = memory;
}


void submit(VkCommandBuffer cmdBuffer, VkQueue queue, VkFence fence) {
    VkSubmitInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.commandBufferCount = 1;
    info.pCommandBuffers = &cmdBuffer;
    if (vkQueueSubmit(queue, 1, &info, fence) != VK_SUCCESS) {
        printf("Failed to submit to queue.");
        exit(EXIT_FAILURE);
    }
}

void block(VkDevice device, VkFence const *fence) {
    if (vkWaitForFences(device, 1, fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        printf("Failed waiting for fence.");
        exit(EXIT_FAILURE);
    }
    vkResetFences(device, 1, fence);
}

void process(Elham *e) {
    const char *data;

    if (vkMapMemory(e->device, e->dstImageMemory, 0, VK_WHOLE_SIZE, 0, (void **) &data) != VK_SUCCESS) {
        printf("Failed to map memory for destination image.");
        exit(EXIT_FAILURE);
    }

    VkImageSubresource subResource = {0};
    subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(e->device, e->dstImage, &subResource, &layout);

    e->callback(data + layout.offset, layout.rowPitch);
    vkUnmapMemory(e->device, e->dstImageMemory);
}

void saveRaw(const char *data, VkDeviceSize rowPitch) {
    static unsigned long frameNumber = 0;
    char filename[] = "output/0000";
    sprintf(filename, "output/%04lu", frameNumber);
    FILE *f = fopen(filename, "w");
    for (int32_t y = 0; y < height; y++) {
        unsigned int *row = (unsigned int *) data;
        fwrite(row, 4, width, f);
        data += rowPitch;
    }
    fclose(f);

    frameNumber++;
}

void saveYCbCr(
    void *dataY, VkDeviceSize rowPitchY,
    void *dataCb, VkDeviceSize rowPitchCb,
    void *dataCr, VkDeviceSize rowPitchCr
    ) {
    static unsigned long frameNumber = 0;
    char filename[] = "output/0000.yuv";
    sprintf(filename, "output/%04lu.yuv", frameNumber);
    FILE *f = fopen(filename, "w");
    for (int32_t y = 0; y < height; y++) {
        uint8_t *row = (uint8_t *) dataY;
        fwrite(row, 1, width, f);
        dataY += rowPitchY;
    }
    for (int32_t y = 0; y < (height / 2); y++) {
        uint8_t *row = (uint8_t *) dataCb;
        fwrite(row, 1, (width / 2), f);
        dataCb += rowPitchCb;
    }
    for (int32_t y = 0; y < (height / 2); y++) {
        uint8_t *row = (uint8_t *) dataCr;
        fwrite(row, 1, (width / 2), f);
        dataCr += rowPitchCr;
    }
    fclose(f);

    frameNumber++;
}

void saveYCbCr2(
    const char *dataY, VkDeviceSize rowPitchY,
    const char *dataCb, VkDeviceSize rowPitchCb,
    const char *dataCr, VkDeviceSize rowPitchCr
    ) {
    static unsigned long frameNumber = 0;
    char filename[] = "output/0000.y";
    sprintf(filename, "output/%04lu.y", frameNumber);
    FILE *f = fopen(filename, "w");
    for (int32_t y = 0; y < height; y++) {
        uint8_t *row = (uint8_t *) dataY;
        fwrite(row, 1, width, f);
        dataY += rowPitchY;
    }
    fclose(f);

    char filename2[] = "output/0000.cb";
    sprintf(filename2, "output/%04lu.cb", frameNumber);
    FILE *f2 = fopen(filename2, "w");
    for (int32_t y = 0; y < (height / 2); y++) {
        uint8_t *row = (uint8_t *) dataCb;
        fwrite(row, 1, (width / 2), f2);
        dataCb += rowPitchCb;
    }
    fclose(f2);

    char filename3[] = "output/0000.cr";
    sprintf(filename3, "output/%04lu.cr", frameNumber);
    FILE *f3 = fopen(filename3, "w");
    for (int32_t y = 0; y < (height / 2); y++) {
        uint8_t *row = (uint8_t *) dataCr;
        fwrite(row, 1, (width / 2), f3);
        dataCr += rowPitchCr;
    }
    fclose(f3);

    frameNumber++;
}

VkFence createFence(VkDevice device) {
    VkFence fence;
    VkFenceCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &info, NULL, &fence) != VK_SUCCESS) {
        printf("Failed to create fence.");
        exit(EXIT_FAILURE);
    }
    return fence;
}

bool finished = false;

void memoryMApY(const Elham *e, const char **yData, VkSubresourceLayout *layout);

void memoryMapCb(const Elham *e, const char **cbData, VkSubresourceLayout *layoutCb);

void memoryMapCr(const Elham *e, const char **crData, VkSubresourceLayout *layoutCr);

void handleSigint() {
    printf("SIGINT received, finishing ...\n");
    finished = true;
}

void setDimensions(Elham *e, uint32_t width, uint32_t height) {
    e->width = width;
    e->height = height;
    e->rect = (struct VkRect2D) {.offset={.x=0, .y=0}, .extent={.width=width, .height=height}};
}

void createVertexBuffer(Elham *e, size_t count) {
    VkDevice device = e->device;
    VkPhysicalDevice gpu = e->gpu;

    printf("Create vertex buffer...");
    VkBuffer buff = VK_NULL_HANDLE;
    VkBufferCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = sizeof(Vertex) * count;
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &info, NULL, &buff) != VK_SUCCESS) {
        printf("failed to create buffer.\n");
        exit(EXIT_FAILURE);
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buff, &req);
    VkMemoryAllocateInfo aloc = {0};
    aloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    aloc.allocationSize = req.size;
    aloc.memoryTypeIndex = findMemoryType(gpu, req.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory mem;
    if (vkAllocateMemory(device, &aloc, NULL, &mem) != VK_SUCCESS) {
        printf("failed to allocate vertex buffer memory.\n");
        exit(EXIT_FAILURE);
    }
    vkBindBufferMemory(device, buff, mem, 0);
    printf("done.\n");

    e->vertexCount = count;
    e->vertexBufferMemory = mem;
    e->vertexBuffer = buff;
}

void recordCopyCommand(Elham *e) {
    VkCommandBufferBeginInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = 0;
    info.pInheritanceInfo = NULL;
    printf("Begin command buffer for Copy...");
    if (vkBeginCommandBuffer(e->copyCommandBuffer, &info) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");

    insertImageMemoryBarrier(
        e->copyCommandBuffer,
        e->dstImage,
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageCopy copy = {0};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.layerCount = 1;
    copy.extent.width = width;
    copy.extent.height = height;
    copy.extent.depth = 1;

    vkCmdCopyImage(
        e->copyCommandBuffer,
        e->srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        e->dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy);

    insertImageMemoryBarrier(
        e->copyCommandBuffer,
        e->dstImage,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_MEMORY_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);

    printf("End command buffer for Copy...");
    if (vkEndCommandBuffer(e->copyCommandBuffer) != VK_SUCCESS) {
        printf("failed.\n");
        exit(EXIT_FAILURE);
    }
    printf("done.\n");
}

void fillVertexBuffer(Elham *e, Vertex vertices[]) {
    void *data;
    vkMapMemory(e->device, e->vertexBufferMemory, 0, sizeof(Vertex) * e->vertexCount, 0, &data);
    memcpy(data, vertices, (size_t) sizeof(Vertex) * 3);
    vkUnmapMemory(e->device, e->vertexBufferMemory);
}

void createFences(Elham *e) {
    printf("Create fences...");
    vkGetDeviceQueue(e->device, e->graphicsQueueFamilyIndex, 0, &e->graphicQueue);
    e->renderFence = createFence(e->device);
    e->copyFence = createFence(e->device);
    printf("done.\n");
}

void frame(Elham *e) {
    submit(e->renderCommandBuffer, e->graphicQueue, e->renderFence);
    block(e->device, &e->renderFence);
    submit(e->copyCommandBuffer, e->graphicQueue, e->copyFence);
    block(e->device, &e->copyFence);
    process(e);
}


void postFrame() {
    static unsigned int frames = 0;

    frames++;
    if(frames >= 1)  { finished = true; }

}


void ycbcr(Elham *e) {
    printf("Y'CbCr...");

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &e->ycbcr.commandBuffer;
    VK_CHECK_RESULT(vkQueueSubmit(e->ycbcr.queue, 1, &submitInfo, e->ycbcr.fence))
    block(e->device, &e->ycbcr.fence);
    printf("done.\n");
}

void memoryMapCr(const Elham *e, const char **crData, VkSubresourceLayout *layoutCr) {
    if (vkMapMemory((*e).device, (*e).ycbcr.crMemory, 0, VK_WHOLE_SIZE, 0, (void **) crData) != VK_SUCCESS) {
        printf("Failed to map memory for Cr image.");
        exit(EXIT_FAILURE);
    }

    VkImageSubresource subResourceCr = {0};
    subResourceCr.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkGetImageSubresourceLayout((*e).device, (*e).ycbcr.cr, &subResourceCr, layoutCr);
}

void memoryMapCb(const Elham *e, const char **cbData, VkSubresourceLayout *layoutCb) {
    if (vkMapMemory((*e).device, (*e).ycbcr.cbMemory, 0, VK_WHOLE_SIZE, 0, (void **) cbData) != VK_SUCCESS) {
        printf("Failed to map memory for Cb image.");
        exit(EXIT_FAILURE);
    }

    VkImageSubresource subResourceCb = {0};
    subResourceCb.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkGetImageSubresourceLayout((*e).device, (*e).ycbcr.cb, &subResourceCb, layoutCb);
}

void memoryMApY(const Elham *e, const char **yData, VkSubresourceLayout *layout) {
    if (vkMapMemory((*e).device, (*e).ycbcr.yMemory, 0, VK_WHOLE_SIZE, 0, (void **) yData) != VK_SUCCESS) {
        printf("Failed to map memory for Y' image.");
        exit(EXIT_FAILURE);
    }

    VkImageSubresource subResourceY = {0};
    subResourceY.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkGetImageSubresourceLayout((*e).device, (*e).ycbcr.y, &subResourceY, layout);
}

void cleanup(Elham *e) {
    VkDevice device = e->device;
    VkInstance instance = e->instance;

    printf("Cleaning up...");
    vkDestroyBuffer(device, e->vertexBuffer, NULL);

    vkFreeMemory(device, e->vertexBufferMemory, NULL);

    vkDestroyFence(device, e->copyFence, NULL);
    vkDestroyFence(device, e->renderFence, NULL);

    vkFreeMemory(device, e->ycbcr.yMemory, NULL);
    vkDestroyImage(device, e->ycbcr.y, NULL);

    vkDestroyImage(device, e->srcImage, NULL);
    vkFreeMemory(device, e->srcImageMemory, NULL);

    vkDestroyImage(device, e->dstImage, NULL);
    vkFreeMemory(device, e->dstImageMemory, NULL);

    vkDestroyCommandPool(device, e->commandPool, NULL);
    vkDestroyFramebuffer(device, e->framebuffer, NULL);
    vkDestroyImageView(device, e->srcImageView, NULL);
    vkDestroyPipeline(device, e->pipeline, NULL);
    vkDestroyRenderPass(device, e->renderPass, NULL);
    vkDestroyPipelineLayout(device, e->pipelineLayout, NULL);
    vkDestroyShaderModule(device, e->vertShader, NULL);
    vkDestroyShaderModule(device, e->fragShader, NULL);
    vkDestroyShaderModule(device, e->ycbcr.shader, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    printf("done.\n");
}

void ycbcrCreateCommandBuffer(Elham *e) {
    VkCommandPoolCreateInfo cmdPoolInfo = {0};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.flags = 0;
    cmdPoolInfo.queueFamilyIndex = e->ycbcr.queueFamilyIndex;
    VK_CHECK_RESULT(vkCreateCommandPool(e->device, &cmdPoolInfo, NULL, &e->ycbcr.commandPool))

    VkCommandBufferAllocateInfo cmdBuffIno = {0};
    cmdBuffIno.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBuffIno.commandPool = e->ycbcr.commandPool;
    cmdBuffIno.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBuffIno.commandBufferCount = 1;
    VK_CHECK_RESULT(vkAllocateCommandBuffers(e->device, &cmdBuffIno, &e->ycbcr.commandBuffer))
    VkCommandBuffer buff = e->ycbcr.commandBuffer;

    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK_RESULT(vkBeginCommandBuffer(buff, &beginInfo))

    vkCmdBindPipeline(buff, VK_PIPELINE_BIND_POINT_COMPUTE, e->ycbcr.pipeline);
    vkCmdBindDescriptorSets(buff, VK_PIPELINE_BIND_POINT_COMPUTE, e->ycbcr.pipelineLayout, 0, 1, &e->ycbcr.descriptorSet, 0, NULL);

    insertImageMemoryBarrier(
        buff,
        (*e).ycbcr.y,
        VK_ACCESS_SHADER_WRITE_BIT,
        0,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    insertImageMemoryBarrier(
        buff,
        (*e).ycbcr.cb,
        VK_ACCESS_SHADER_WRITE_BIT,
        0,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    insertImageMemoryBarrier(
        buff,
        (*e).ycbcr.cr,
        VK_ACCESS_SHADER_WRITE_BIT,
        0,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkCmdDispatch(buff, (*e).width / 2, (*e).height / 2, 1);

    insertImageMemoryBarrier(
        buff,
        (*e).ycbcr.cb,
        0,
        VK_ACCESS_MEMORY_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    insertImageMemoryBarrier(
        buff,
        (*e).ycbcr.cr,
        0,
        VK_ACCESS_MEMORY_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    insertImageMemoryBarrier(
        buff,
        (*e).ycbcr.y,
        0,
        VK_ACCESS_MEMORY_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    VK_CHECK_RESULT(vkEndCommandBuffer(buff)) // end recording commands.
}

void ycbcrCreateDescriptorSet(Elham *e) {
    VkDevice device = e->device;

    e->ycbcr.format = VK_FORMAT_R8_UNORM;

    createYImage(e);
    createCbImage(e);
    createCrImage(e);

    // layout
    VkDescriptorSetLayoutBinding in = {0};
    in.binding = 0;
    in.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    in.descriptorCount = 1;
    in.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding y = {0};
    y.binding = 1;
    y.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    y.descriptorCount = 1;
    y.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding cb = {0};
    cb.binding = 2;
    cb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    cb.descriptorCount = 1;
    cb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding cr = {0};
    cr.binding = 3;
    cr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    cr.descriptorCount = 1;
    cr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding bindings[4] = {in, y, cb, cr};
    VkDescriptorSetLayoutCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 4;
    info.pBindings = bindings;
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &info, NULL, &e->ycbcr.descriptorSetLayout))

    // pool
    VkDescriptorPool pool;
    VkDescriptorPoolSize poolSize = {0};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSize.descriptorCount = 4;
    VkDescriptorPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, NULL, &pool))

    // set
    VkDescriptorSetAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &e->ycbcr.descriptorSetLayout;
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &e->ycbcr.descriptorSet))

    createInImageView(e);
    VkDescriptorImageInfo inInfo = {0};
    inInfo.imageView = (*e).ycbcr.inputView;
    inInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    createYImageView(e);
    VkDescriptorImageInfo yInfo = {0};
    yInfo.imageView = (*e).ycbcr.yView;
    yInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    createCbImageView(e);

    VkDescriptorImageInfo cbInfo = {0};
    cbInfo.imageView = (*e).ycbcr.cbView;
    cbInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    createCrImageView(e);
    VkDescriptorImageInfo crInfo = {0};
    crInfo.imageView = (*e).ycbcr.crView;
    crInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    // init set (connect resources to bindings)
    VkWriteDescriptorSet writeI = {0};
    writeI.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeI.dstSet = e->ycbcr.descriptorSet;
    writeI.dstBinding = 0;
    writeI.descriptorCount = 1;
    writeI.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writeI.pImageInfo = &inInfo;

    VkWriteDescriptorSet writeY = {0};
    writeY.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeY.dstSet = e->ycbcr.descriptorSet;
    writeY.dstBinding = 1;
    writeY.descriptorCount = 1;
    writeY.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writeY.pImageInfo = &yInfo;

    VkWriteDescriptorSet writeCb = {0};
    writeCb.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeCb.dstSet = e->ycbcr.descriptorSet;
    writeCb.dstBinding = 2;
    writeCb.descriptorCount = 1;
    writeCb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writeCb.pImageInfo = &cbInfo;

    VkWriteDescriptorSet writeCr = {0};
    writeCr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeCr.dstSet = e->ycbcr.descriptorSet;
    writeCr.dstBinding = 3;
    writeCr.descriptorCount = 1;
    writeCr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writeCr.pImageInfo = &crInfo;

    VkWriteDescriptorSet writes[4] = {writeI, writeY, writeCb, writeCr};
    vkUpdateDescriptorSets(device, 4, writes, 0, NULL);
}

void ycbcrCreatePipeline(Elham *e) {
    VkPipelineLayoutCreateInfo layoutInfo = {0};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &e->ycbcr.descriptorSetLayout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(e->device, &layoutInfo, NULL, &e->ycbcr.pipelineLayout))

    VkPipelineShaderStageCreateInfo stageInfo = {0};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = e->ycbcr.shader;
    stageInfo.pName = "main";
    VkComputePipelineCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage = stageInfo;
    info.layout = e->ycbcr.pipelineLayout;
    VK_CHECK_RESULT(vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &info, NULL, &e->ycbcr.pipeline))
}

void encode(Elham *e) {

}

int main(int argc, const char *argv[]) {
    Elham e;
    char const *vertexShader = "shaders/vert.spv";
    char const *fragmentShader = "shaders/frag.spv";
    char const *ycbcrShader = "shaders/ycbcr.spv";

    e.format = VK_FORMAT_R8G8B8A8_UNORM;
    e.callback = saveRaw;

    setDimensions(&e, width, height);

    // Vulkan
    createInstance(&e);
    pickPhysicalDevice(&e);
    pickGraphicsQueueFamily(&e);
    pickComputeQueueFamily(&e);
    createDevice(&e);

    // Render
    createRenderPass(&e);
    createSrcImage(&e);

    createImageView(&e);
    createFramebuffer(&e);

    createCommandPool(&e);
    createCommandBuffer(&e);
    createPipelineLayout(&e);

    printf("Create vertex shader...");
    e.vertShader = createShader(e.device, vertexShader);
    printf("done.\n");

    printf("Create fragment shader...");
    e.fragShader = createShader(e.device, fragmentShader);
    printf("done.\n");

    createPipeline(&e);

    createVertexBuffer(&e, 3);
    recordRenderCommands(&e);

    // Copy
    createDstImage(&e);
    createCopyCommandBuffer(&e);
    recordCopyCommand(&e);

    createFences(&e);

    // Y'CbCr
    ycbcrCreateDescriptorSet(&e);
    printf("Create Y'CbCr shader...");
    e.ycbcr.shader = createShader(e.device, ycbcrShader);
    printf("done.\n");
    ycbcrCreatePipeline(&e);
    ycbcrCreateCommandBuffer(&e);
    e.ycbcr.fence = createFence(e.device);

    printf("Installing signal handler...");
    signal(SIGINT | SIGHUP | SIGTERM, handleSigint);
    printf("done.\n");

    x265_param *param = x265_param_alloc();
    x265_param_default_preset(param, "ultrafast", NULL);
    param->bRepeatHeaders=1;
    x265_param_parse(param, "fps", "60/1");
    param->sourceWidth = e.width;
    param->sourceHeight = e.height;
    param->forceFlush = 1;
    x265_encoder* encoder = x265_encoder_open(param);
    x265_picture *picIn = x265_picture_alloc();
    x265_picture_init(param, picIn);
//    picIn->bitDepth = 24;

    Vec2 center = {0, 0};
    float speed = 1.0f / 5; // full rotation in 5 frames
    clock_t t;
    printf("Entering animation...\n");
    unsigned frames = 0;
    while (!finished) {
//        t = clock() / CLOCKS_PER_SEC;
        for (unsigned i = 0; i < 3; i++) {
            rotateVec2(center,  frames * speed, &(vertices[i].pos));
        }
        fillVertexBuffer(&e, vertices);
        frame(&e);
        ycbcr(&e);
        // Encode
        printf("Encoding frame #%05d...", frames);
        const char *y;
        VkSubresourceLayout layoutY;
        memoryMApY(&e, &y, &layoutY);
        const char *cb;
        VkSubresourceLayout layoutCb;
        memoryMapCb(&e, &cb, &layoutCb);
        const char *cr;
        VkSubresourceLayout layoutCr;
        memoryMapCr(&e, &cr, &layoutCr);
        // YUV420p = 8bpp for Y', 4bpp for Cb and Cr each = 1 byte every 2 pixels
        picIn->stride[0] = layoutY.rowPitch;
        picIn->stride[1] = layoutCb.rowPitch;
        picIn->stride[2] = layoutCr.rowPitch;
        picIn->planes[0] = y;
        picIn->planes[1] = cb;
        picIn->planes[2] = cr;
        x265_nal *pNals=NULL;
        uint32_t iNal=0;
        printf("Here\n");
        int ret = x265_encoder_encode(encoder,&pNals,&iNal,picIn,NULL);
        if(ret == 1) {
            printf("done\n");
        } else if(ret == 0)  {
            ret = x265_encoder_encode(encoder,&pNals,&iNal,NULL,NULL);
            printf("ret2 = %d, iNal2 = %d \n", ret,  iNal);
            if(ret == 1) {
                char filename[] = "output/0000.h265";
                sprintf(filename, "output/%04lu.h265", frames);
                FILE *f = fopen(filename, "w");
                for(int j=0; j < iNal; j++){
                    fwrite(pNals[j].payload,1,pNals[j].sizeBytes,f);
                    printf("%d\n",j);
                    fclose(f);
                }
                printf("done\n");
            } else {
                printf("failed : %d\n", ret);
            }
        } else {
            printf("failed : %d.\n", ret);
        }
//        for(int j=0; j < iNal; j++){
//            fwrite(pNals[j].payload,1,pNals[j].sizeBytes,fp_dst);
//        }
//        while(1){
//            ret=x265_encoder_encode(encoder,&pNals,&iNal,NULL,NULL);
//            if(ret==0){
//                break;
//            }
//            printf("Flush 1 frame.\n");
//
//            for(int j=0;j<iNal;j++){
//                fwrite(pNals[j].payload,1,pNals[j].sizeBytes,fp_dst);
//            }
//        }

        vkUnmapMemory(e.device, e.ycbcr.yMemory);
        vkUnmapMemory(e.device, e.ycbcr.cbMemory);
        vkUnmapMemory(e.device, e.ycbcr.crMemory);
        postFrame();
        frames ++;
    }

    cleanup(&e);

    return EXIT_SUCCESS;
}
