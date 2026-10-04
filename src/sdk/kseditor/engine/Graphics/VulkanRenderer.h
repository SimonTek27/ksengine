#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include <QObject>
#include <QWindow>
#include <QImage>
#include <QColor>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QMatrix4x4>
#include <QPoint>
#include <QPointF>
#include <memory>

#include <QVulkanWindow>
#include <QVulkanInstance>

namespace ks {

class VulkanTexture;
class VulkanFramebuffer;

class VulkanRenderer : public QObject {
    Q_OBJECT

public:
    static VulkanRenderer* instance();

    explicit VulkanRenderer(QObject* parent = nullptr);
    ~VulkanRenderer();

    bool initialize();
    bool isInitialized() const { return m_initialized; }

    void beginFrame();
    void endFrame();

    void setViewport(int x, int y, int width, int height);
    void clear(const QColor& color);

    void drawMesh(int vertexCount, int firstVertex);
    void drawIndexedMesh(int indexCount, int firstIndex);

    void setProjectionMatrix(const QMatrix4x4& matrix);
    void setViewMatrix(const QMatrix4x4& matrix);
    void setModelMatrix(const QMatrix4x4& matrix);

    void bindShader(const QString& name);
    void unbindShader();
    void setPipeline(VkPipeline pipeline);

    void setUniform(const QString& name, const QVariant& value);
    void setTexture(const QString& name, const QImage& image);

    struct Vertex {
        QVector3D position;
        QVector3D normal;
        QVector2D uv;
        QVector4D color;
    };

    struct Mesh {
        QString name;
        QVector<Vertex> vertices;
        QVector<quint32> indices;
        QMatrix4x4 transform;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    };



    void createMesh(const QString& name, const QVector<Vertex>& vertices, const QVector<quint32>& indices);
    Mesh* getMesh(const QString& name);
    void destroyMesh(const QString& name);
    const QMap<QString, Mesh>& allMeshes() const { return m_meshes; }

    static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkInstance vulkanInstance() const { return m_instance; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    VkCommandPool commandPool() const { return m_commandPool; }
    uint32_t graphicsQueueFamilyIndex() const { return m_graphicsFamily; }

    bool createDevice(VkInstance instance, VkSurfaceKHR surface = VK_NULL_HANDLE);
    void destroyDevice();

    // Swap chain
    bool createSwapChain(VkSurfaceKHR surface, int width, int height);
    void destroySwapChain();
    bool recreateSwapChain(int width, int height);
    bool acquireNextImage(uint32_t& imageIndex);
    bool presentImage(uint32_t imageIndex);
    VkRenderPass renderPass() const { return m_renderPass; }
    int swapChainImageCount() const { return m_swapChainImages.size(); }
    VkExtent2D swapChainExtent() const { return m_swapChainExtent; }
    int viewportWidth() const { return m_viewportWidth; }
    int viewportHeight() const { return m_viewportHeight; }

    // Offscreen rendering (for preview generation)
    bool createOffscreenRenderTarget(int width, int height, VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT);
    void destroyOffscreenRenderTarget();
    bool renderOffscreen(int width, int height, const std::function<void()>& drawCommands, QImage& outImage);

    // Shader loader access
    class VulkanShaderLoader* shaderLoader() const { return m_shaderLoader.get(); }

signals:
    void initialized();
    void frameRendered();
    void error(const QString& message);

private:
    bool m_initialized = false;
    bool m_ownsInstance = false;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    uint32_t m_graphicsFamily = 0;
    QMap<QString, Mesh> m_meshes;
    QMatrix4x4 m_projectionMatrix;
    QMatrix4x4 m_viewMatrix;
    QMatrix4x4 m_modelMatrix;
    QString m_currentShader;
    int m_viewportX = 0, m_viewportY = 0;
    int m_viewportWidth = 0, m_viewportHeight = 0;
    QColor m_clearColor = Qt::black;
    QMap<QString, QVariant> m_uniforms;
    QMap<QString, QImage> m_textures;
    QImage frameBuffer() const { return m_frameBuffer; }

    struct Stats {
        int drawCalls = 0;
        int verticesProcessed = 0;
        int indicesProcessed = 0;
    } m_stats;

    QImage m_frameBuffer;

    // Real Vulkan command recording
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkPipeline m_currentPipeline = VK_NULL_HANDLE;
    bool m_renderPassActive = false;

    // Swap chain
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    VkFormat m_swapChainFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D m_swapChainExtent = {0, 0};
    QVector<VkImage> m_swapChainImages;
    QVector<VkImageView> m_swapChainImageViews;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    QVector<VulkanTexture*> m_swapChainDepthTextures;
    QVector<VulkanFramebuffer*> m_swapChainFramebuffers;
    uint32_t m_currentImageIndex = 0;

    // Offscreen rendering
    VkRenderPass m_offscreenRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_offscreenFramebuffer = VK_NULL_HANDLE;
    VkImage m_offscreenColorImage = VK_NULL_HANDLE;
    VkImageView m_offscreenColorView = VK_NULL_HANDLE;
    VkDeviceMemory m_offscreenColorMemory = VK_NULL_HANDLE;
    VkImage m_offscreenDepthImage = VK_NULL_HANDLE;
    VkImageView m_offscreenDepthView = VK_NULL_HANDLE;
    VkDeviceMemory m_offscreenDepthMemory = VK_NULL_HANDLE;
    VkExtent2D m_offscreenExtent = {0, 0};
    VkFormat m_offscreenColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkCommandBuffer m_offscreenCommandBuffer = VK_NULL_HANDLE;
    VkFence m_offscreenFence = VK_NULL_HANDLE;

    // Synchronization
    VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;

    // Shader loader
    std::unique_ptr<class VulkanShaderLoader> m_shaderLoader;
};

class VulkanWindow : public QWindow {
    Q_OBJECT

public:
    explicit VulkanWindow(QWindow* parent = nullptr);
    ~VulkanWindow();

    void setRenderer(VulkanRenderer* renderer);

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void renderFrame();

private:
    VulkanRenderer* m_renderer = nullptr;
    QSize m_lastSize;
#if QT_CONFIG(vulkan)
    QVulkanInstance* m_vulkanInstance = nullptr;
#endif
};

class VulkanRenderPass : public QObject {
    Q_OBJECT

public:
    explicit VulkanRenderPass(QObject* parent = nullptr);
    ~VulkanRenderPass();

    void setVertexShader(const QString& source);
    void setFragmentShader(const QString& source);
    void setComputeShader(const QString& source);

    void setDevice(VkDevice device);
    void setColorFormat(VkFormat format);
    void setDepthFormat(VkFormat format);
    void setEnableDepth(bool enable);

    bool compile();
    void destroy();

    VkRenderPass renderPass() const { return m_renderPass; }
    VkPipeline pipeline() const { return m_pipeline; }
    VkPipelineLayout pipelineLayout() const { return m_pipelineLayout; }
    bool isCompiled() const { return m_compiled; }

signals:
    void compiled();
    void error(const QString& message);

private:
    QString m_error;
    QString m_vertexShader;
    QString m_fragmentShader;
    QString m_computeShader;
    bool m_compiled = false;

    VkDevice m_device = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkShaderModule m_vertModule = VK_NULL_HANDLE;
    VkShaderModule m_fragModule = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkFormat m_colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;
    bool m_enableDepth = true;
};

class VulkanBuffer : public QObject {
    Q_OBJECT

public:
    enum Type {
        VertexBuffer,
        IndexBuffer,
        UniformBuffer,
        StorageBuffer
    };

    explicit VulkanBuffer(Type type, QObject* parent = nullptr);
    ~VulkanBuffer();

    void setDevice(VkDevice device, VkPhysicalDevice physicalDevice);

    void allocate(quint64 size, const void* data = nullptr);
    void update(quint64 offset, quint64 size, const void* data);
    void* map();
    void unmap();

    bool isValid() const { return m_valid; }
    quint64 size() const { return m_size; }

    VkBuffer buffer() const { return m_buffer; }
    VkDeviceMemory memory() const { return m_memory; }

signals:
    void allocated();
    void updated();

private:
    Type m_type;
    quint64 m_size = 0;
    bool m_valid = false;
    QByteArray m_data;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    void* m_mappedPtr = nullptr;
};

class VulkanTexture : public QObject {
    Q_OBJECT

public:
    enum Format {
        RGBA8,
        RGBA16F,
        RGBA32F,
        D32F,
        BC7
    };

    explicit VulkanTexture(QObject* parent = nullptr);
    ~VulkanTexture();

    void setDevice(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue = VK_NULL_HANDLE);

    void createFromImage(const QImage& image);
    void createRenderTarget(int width, int height, Format format);
    void generateMipmaps();

    int width() const { return m_width; }
    int height() const { return m_height; }
    Format format() const { return m_format; }
    QImage imageData() const;
    QVector<QImage> mipData() const;

    VkImage image() const { return m_image; }
    VkImageView imageView() const { return m_imageView; }
    VkSampler sampler() const { return m_sampler; }
    VkFormat vulkanFormat() const { return m_vkFormat; }
    uint32_t mipLevels() const { return m_mipLevels; }

signals:
    void created();

private:
    int m_width = 0;
    int m_height = 0;
    Format m_format = RGBA8;
    QImage m_imageData;
    QVector<QImage> m_mipData;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkFormat m_vkFormat = VK_FORMAT_R8G8B8A8_SRGB;
    VkImageAspectFlags m_aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t m_mipLevels = 1;
};

class VulkanFramebuffer : public QObject {
    Q_OBJECT

public:
    explicit VulkanFramebuffer(QObject* parent = nullptr);
    ~VulkanFramebuffer();

    void setDevice(VkDevice device, VkPhysicalDevice physicalDevice);
    void setRenderPass(VkRenderPass renderPass);

    void attachColor(VulkanTexture* texture);
    void attachDepth(VulkanTexture* texture);

    bool isValid() const { return m_valid; }
    bool isBound() const;
    void bind();
    void unbind();
    static VulkanFramebuffer* activeFramebuffer();

    VkFramebuffer framebuffer() const { return m_framebuffer; }
    VkRenderPass renderPass() const { return m_renderPass; }

signals:
    void created();

private:
    void recreateFramebuffer();

    bool m_valid = false;
    bool m_bound = false;
    VulkanTexture* m_colorTexture = nullptr;
    VulkanTexture* m_depthTexture = nullptr;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkImageView m_colorImageView = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;
};

}
