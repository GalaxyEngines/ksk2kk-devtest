#include <vulkan/vulkan.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/glm.hpp>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>
#include <stb_image.h>

struct Vertex {
    glm::vec3 Position;  // 顶点位置
    glm::vec3 Normal;    // 顶点法线
    glm::vec2 TexCoords; // 纹理坐标
    glm::vec3 Tangent;   // 切线
    glm::vec3 Bitangent; // 副切线
};

struct Texture {
    VkImage image;                 // Vulkan 图像对象
    VkDeviceMemory imageMemory;    // 图像内存
    VkImageView imageView;         // 图像视图
    VkSampler sampler;             // 纹理采样器
    std::string type;              // 纹理类型
    std::string path;              // 纹理路径
};

class ModelLoader {
public:
    // 构造函数，初始化 Vulkan 设备、物理设备、图形队列和命令池
    ModelLoader(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, VkCommandPool commandPool)
        : device(device), physicalDevice(physicalDevice), graphicsQueue(graphicsQueue), commandPool(commandPool) {}

    // 析构函数，确保释放所有 Vulkan 资源
    ~ModelLoader() {
        cleanup();  // 确保所有 Vulkan 资源被释放
    }

    // 加载模型文件
    bool loadModel(const std::string& filePath) {
        try {
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(filePath,
                aiProcess_Triangulate |
                aiProcess_FlipUVs |
                aiProcess_CalcTangentSpace |
                aiProcess_OptimizeMeshes |
                aiProcess_JoinIdenticalVertices);

            // 检查模型是否成功加载
            if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
                logError("加载模型出错: " + std::string(filePath));
                return false;
            }
            // 处理节点
            processNode(scene->mRootNode, scene);
            return true;
        }
        catch (const std::exception& e) {
            logError("加载模型时发生异常: " + std::string(e.what()));
            return false;
        }
    }

private:
    VkDevice device;  // Vulkan 设备
    VkPhysicalDevice physicalDevice;  // Vulkan 物理设备
    VkQueue graphicsQueue;  // Vulkan 图形队列
    VkCommandPool commandPool;  // Vulkan 命令池
    std::unordered_map<std::string, Texture> loadedTextures;  // 已加载纹理的哈希映射
    std::vector<VkBuffer> vertexBuffers;  // 顶点缓冲区
    std::vector<VkDeviceMemory> vertexBufferMemories;  // 顶点缓冲区内存
    std::vector<VkBuffer> indexBuffers;  // 索引缓冲区
    std::vector<VkDeviceMemory> indexBufferMemories;  // 索引缓冲区内存
    QMutex mutex;  // 线程安全的互斥锁

    // 递归处理节点
    void processNode(aiNode* node, const aiScene* scene) {
        // 处理节点中的每个网格
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            processMesh(mesh, scene);
        }
        // 递归处理子节点
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            processNode(node->mChildren[i], scene);
        }
    }

    // 处理网格
    void processMesh(aiMesh* mesh, const aiScene* scene) {
        std::vector<Vertex> vertices;  // 存储顶点数据
        std::vector<uint32_t> indices;  // 存储索引数据

        // 遍历每个顶点，获取顶点属性
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex;
            vertex.Position = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);  // 顶点位置
            if (mesh->HasNormals()) {
                vertex.Normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);  // 顶点法线
            }
            // 处理纹理坐标
            if (mesh->mTextureCoords[0]) {
                vertex.TexCoords = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
                // 处理切线和副切线
                if (mesh->HasTangentsAndBitangents()) {
                    vertex.Tangent = glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);
                    vertex.Bitangent = glm::vec3(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z);
                }
            }
            else {
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);  // 如果没有纹理坐标，则使用默认值
            }
            vertices.push_back(vertex);  // 存储顶点
        }

        // 处理索引
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(face.mIndices[j]);  // 存储索引
            }
        }

        // 如果网格有材质，加载材质纹理
        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            loadMaterialTextures(material, scene);
        }

        // 创建顶点缓冲区和索引缓冲区
        createVertexBuffer(vertices);
        createIndexBuffer(indices);
    }

    // 加载材质的纹理
    void loadMaterialTextures(aiMaterial* material, const aiScene* scene) {
        loadTexture(material, aiTextureType_DIFFUSE, "texture_diffuse");  // 加载漫反射纹理
        loadTexture(material, aiTextureType_NORMALS, "texture_normal");   // 加载法线纹理
        loadTexture(material, aiTextureType_SPECULAR, "texture_specular");  // 加载高光纹理
    }

    // 加载单个纹理
    void loadTexture(aiMaterial* material, aiTextureType type, const std::string& typeName) {
        for (unsigned int i = 0; i < material->GetTextureCount(type); i++) {
            aiString str;
            material->GetTexture(type, i, &str);

            // 检查纹理是否已经加载
            if (loadedTextures.find(str.C_Str()) != loadedTextures.end()) {
                continue;  // 如果纹理已加载，跳过
            }

            // 创建 Vulkan 纹理对象
            Texture texture = createVulkanTexture(str.C_Str());
            texture.type = typeName;
            texture.path = str.C_Str();

            // 使用互斥锁保护纹理加载操作
            QMutexLocker locker(&mutex);
            loadedTextures[str.C_Str()] = texture;
        }
    }

    // 创建 Vulkan 纹理
    Texture createVulkanTexture(const char* path) {
        Texture texture{};
        int width, height, channels;
        unsigned char* pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);  // 加载图像像素
        if (!pixels) {
            logError("加载纹理失败: " + std::string(path));  // 如果加载失败，记录错误
            return texture;
        }

        VkDeviceSize imageSize = width * height * 4;  // 计算图像大小

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;

        // 创建暂存缓冲区，用于传输纹理数据
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer, stagingBufferMemory);

        // 将纹理数据拷贝到暂存缓冲区
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);

        stbi_image_free(pixels);  // 释放图像数据

        // 创建 Vulkan 图像对象
        createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture.image, texture.imageMemory);

        // 创建 Vulkan 图像视图
        createImageView(texture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, texture.imageView);

        // 创建 Vulkan 纹理采样器
        createSampler(texture.sampler);

        // 清理暂存缓冲区
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        return texture;
    }

    // 创建 Vulkan 缓冲区
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
        VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory);
        vkBindBufferMemory(device, buffer, bufferMemory, 0);
    }

    // 创建 Vulkan 图像对象
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = tiling;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(device, &imageInfo, nullptr, &image);

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory);
        vkBindImageMemory(device, image, imageMemory, 0);
    }

    // 创建 Vulkan 图像视图
    void createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask, VkImageView& imageView) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectMask;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        vkCreateImageView(device, &viewInfo, nullptr, &imageView);
    }

    // 创建 Vulkan 纹理采样器
    void createSampler(VkSampler& sampler) {
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;

        vkCreateSampler(device, &samplerInfo, nullptr, &sampler);
    }

    // 查找内存类型
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("无法找到合适的内存类型！");
    }

    // 清理 Vulkan 资源
    void cleanup() {
        for (auto& texturePair : loadedTextures) {
            vkDestroySampler(device, texturePair.second.sampler, nullptr);
            vkDestroyImageView(device, texturePair.second.imageView, nullptr);
            vkDestroyImage(device, texturePair.second.image, nullptr);
            vkFreeMemory(device, texturePair.second.imageMemory, nullptr);
        }

        for (size_t i = 0; i < vertexBuffers.size(); i++) {
            vkDestroyBuffer(device, vertexBuffers[i], nullptr);
            vkFreeMemory(device, vertexBufferMemories[i], nullptr);
        }

        for (size_t i = 0; i < indexBuffers.size(); i++) {
            vkDestroyBuffer(device, indexBuffers[i], nullptr);
            vkFreeMemory(device, indexBufferMemories[i], nullptr);
        }
    }

    // 记录错误日志
    void logError(const std::string& message) {
        std::cerr << "错误: " << message << std::endl;
    }
};

