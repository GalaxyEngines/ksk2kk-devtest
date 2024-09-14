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
    glm::vec3 Position;  // ����λ��
    glm::vec3 Normal;    // ���㷨��
    glm::vec2 TexCoords; // ��������
    glm::vec3 Tangent;   // ����
    glm::vec3 Bitangent; // ������
};

struct Texture {
    VkImage image;                 // Vulkan ͼ�����
    VkDeviceMemory imageMemory;    // ͼ���ڴ�
    VkImageView imageView;         // ͼ����ͼ
    VkSampler sampler;             // ���������
    std::string type;              // ��������
    std::string path;              // ����·��
};

class ModelLoader {
public:
    // ���캯������ʼ�� Vulkan �豸�������豸��ͼ�ζ��к������
    ModelLoader(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, VkCommandPool commandPool)
        : device(device), physicalDevice(physicalDevice), graphicsQueue(graphicsQueue), commandPool(commandPool) {}

    // ����������ȷ���ͷ����� Vulkan ��Դ
    ~ModelLoader() {
        cleanup();  // ȷ������ Vulkan ��Դ���ͷ�
    }

    // ����ģ���ļ�
    bool loadModel(const std::string& filePath) {
        try {
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(filePath,
                aiProcess_Triangulate |
                aiProcess_FlipUVs |
                aiProcess_CalcTangentSpace |
                aiProcess_OptimizeMeshes |
                aiProcess_JoinIdenticalVertices);

            // ���ģ���Ƿ�ɹ�����
            if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
                logError("����ģ�ͳ���: " + std::string(filePath));
                return false;
            }
            // ����ڵ�
            processNode(scene->mRootNode, scene);
            return true;
        }
        catch (const std::exception& e) {
            logError("����ģ��ʱ�����쳣: " + std::string(e.what()));
            return false;
        }
    }

private:
    VkDevice device;  // Vulkan �豸
    VkPhysicalDevice physicalDevice;  // Vulkan �����豸
    VkQueue graphicsQueue;  // Vulkan ͼ�ζ���
    VkCommandPool commandPool;  // Vulkan �����
    std::unordered_map<std::string, Texture> loadedTextures;  // �Ѽ�������Ĺ�ϣӳ��
    std::vector<VkBuffer> vertexBuffers;  // ���㻺����
    std::vector<VkDeviceMemory> vertexBufferMemories;  // ���㻺�����ڴ�
    std::vector<VkBuffer> indexBuffers;  // ����������
    std::vector<VkDeviceMemory> indexBufferMemories;  // �����������ڴ�
    QMutex mutex;  // �̰߳�ȫ�Ļ�����

    // �ݹ鴦��ڵ�
    void processNode(aiNode* node, const aiScene* scene) {
        // ����ڵ��е�ÿ������
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            processMesh(mesh, scene);
        }
        // �ݹ鴦���ӽڵ�
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            processNode(node->mChildren[i], scene);
        }
    }

    // ��������
    void processMesh(aiMesh* mesh, const aiScene* scene) {
        std::vector<Vertex> vertices;  // �洢��������
        std::vector<uint32_t> indices;  // �洢��������

        // ����ÿ�����㣬��ȡ��������
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex;
            vertex.Position = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);  // ����λ��
            if (mesh->HasNormals()) {
                vertex.Normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);  // ���㷨��
            }
            // ������������
            if (mesh->mTextureCoords[0]) {
                vertex.TexCoords = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
                // �������ߺ͸�����
                if (mesh->HasTangentsAndBitangents()) {
                    vertex.Tangent = glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);
                    vertex.Bitangent = glm::vec3(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z);
                }
            }
            else {
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);  // ���û���������꣬��ʹ��Ĭ��ֵ
            }
            vertices.push_back(vertex);  // �洢����
        }

        // ��������
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(face.mIndices[j]);  // �洢����
            }
        }

        // ��������в��ʣ����ز�������
        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            loadMaterialTextures(material, scene);
        }

        // �������㻺����������������
        createVertexBuffer(vertices);
        createIndexBuffer(indices);
    }

    // ���ز��ʵ�����
    void loadMaterialTextures(aiMaterial* material, const aiScene* scene) {
        loadTexture(material, aiTextureType_DIFFUSE, "texture_diffuse");  // ��������������
        loadTexture(material, aiTextureType_NORMALS, "texture_normal");   // ���ط�������
        loadTexture(material, aiTextureType_SPECULAR, "texture_specular");  // ���ظ߹�����
    }

    // ���ص�������
    void loadTexture(aiMaterial* material, aiTextureType type, const std::string& typeName) {
        for (unsigned int i = 0; i < material->GetTextureCount(type); i++) {
            aiString str;
            material->GetTexture(type, i, &str);

            // ��������Ƿ��Ѿ�����
            if (loadedTextures.find(str.C_Str()) != loadedTextures.end()) {
                continue;  // ��������Ѽ��أ�����
            }

            // ���� Vulkan �������
            Texture texture = createVulkanTexture(str.C_Str());
            texture.type = typeName;
            texture.path = str.C_Str();

            // ʹ�û���������������ز���
            QMutexLocker locker(&mutex);
            loadedTextures[str.C_Str()] = texture;
        }
    }

    // ���� Vulkan ����
    Texture createVulkanTexture(const char* path) {
        Texture texture{};
        int width, height, channels;
        unsigned char* pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);  // ����ͼ������
        if (!pixels) {
            logError("��������ʧ��: " + std::string(path));  // �������ʧ�ܣ���¼����
            return texture;
        }

        VkDeviceSize imageSize = width * height * 4;  // ����ͼ���С

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;

        // �����ݴ滺���������ڴ�����������
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer, stagingBufferMemory);

        // ���������ݿ������ݴ滺����
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);

        stbi_image_free(pixels);  // �ͷ�ͼ������

        // ���� Vulkan ͼ�����
        createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture.image, texture.imageMemory);

        // ���� Vulkan ͼ����ͼ
        createImageView(texture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, texture.imageView);

        // ���� Vulkan ���������
        createSampler(texture.sampler);

        // �����ݴ滺����
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        return texture;
    }

    // ���� Vulkan ������
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

    // ���� Vulkan ͼ�����
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

    // ���� Vulkan ͼ����ͼ
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

    // ���� Vulkan ���������
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

    // �����ڴ�����
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("�޷��ҵ����ʵ��ڴ����ͣ�");
    }

    // ���� Vulkan ��Դ
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

    // ��¼������־
    void logError(const std::string& message) {
        std::cerr << "����: " << message << std::endl;
    }
};

