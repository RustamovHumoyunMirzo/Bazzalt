#ifndef ENGINE_H
#define ENGINE_H

#include <cstdint>

class Engine {
public:
    Engine();
    ~Engine();

    // Prevent copying to prevent accidental double-frees of resources
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Lifecycle Management
    bool Init();
    void Update();
    void Shutdown();

    // Loop Control & Status
    bool ShouldClose() const;
    void RequestClose();

    // Frame timing helpers
    float GetDeltaTime() const { return m_deltaTime; }
    uint64_t GetFrameCount() const { return m_frameCount; }

private:
    bool m_isInitialized = false;
    bool m_shouldClose = false;

    // Timing tracking
    float m_deltaTime = 0.016f; // Default ~60 FPS initial frame time
    uint64_t m_frameCount = 0;
};

#endif // ENGINE_H