#ifndef MODELLOADER_H
#define MODELLOADER_H

#include <vulkan/vulkan.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/glm.hpp>
#include <QMutex>
#include <QMutexLocker>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>
#include <stb_image.h>

// 结构体声明
struct Vertex {
    glm::vec3 Position;  // 顶点位置
    glm::vec3 Normal;    // 顶点法线
    glm::vec2 TexCoords; // 纹理坐标
    glm::vec3 Tangent;   // 切线
    glm::vec3 Bitangent; // 副切线
};

struct Texture {
    VkImage image;                 // Vulkan图像对象
    VkDeviceMemory imageMemory;    // 图像内存
    VkImageView imageView;         // 图像视图
    VkSampler sampler;             // 纹理采样器
    std::string type;              // 纹理类型
    std::string path;              // 纹理路径
};

// 类声明
class ModelLoader {
public:
    // 构造函数，初始化 Vulkan 设备、物理设备、图形队列和命令池
    ModelLoader(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, VkCommandPool commandPool);

    // 析构函数，确保释放所有 Vulkan 资源
    ~ModelLoader();

    // 加载模型文件
    bool loadModel(const std::string& filePath);

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
    void processNode(aiNode* node, const aiScene* scene);

    // 处理网格
    void processMesh(aiMesh* mesh, const aiScene* scene);

    // 加载材质的纹理
    void loadMaterialTextures(aiMaterial* material, const aiScene* scene);

    // 加载单个纹理
    void loadTexture(aiMaterial* material, aiTextureType type, const std::string& typeName);

    // 创建 Vulkan 纹理
    Texture createVulkanTexture(const char* path);

    // 创建顶点缓冲区
    void createVertexBuffer(const std::vector<Vertex>& vertices);

    // 创建索引缓冲区
    void createIndexBuffer(const std::vector<uint32_t>& indices);

    // 清理 Vulkan 资源
    void cleanup();

    // 错误记录函数
    void logError(const std::string& message);
};

// 函数声明
void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
VkImageView createImageView(VkImage image, VkFormat format);
VkSampler createTextureSampler();

#endif // MODELLOADER_H
