# Each declaration supplies the default implementation and its replacement seam.
# Add a new private runtime module here. Host/test staging is automatic.
bazzalt_load_runtime_module(NETWORK BazzaltNetwork bazzalt_network
    "${CMAKE_CURRENT_LIST_DIR}/Network.cmake" src/Network/NetworkBackend.cpp)
bazzalt_load_runtime_module(PHYSICS BazzaltPhysics bazzalt_physics
    "${CMAKE_CURRENT_LIST_DIR}/Physics.cmake"
    src/Physics/JoltWorld.cpp src/Physics/PhysicsGeometry.cpp)
bazzalt_load_runtime_module(AI BazzaltAI bazzalt_ai
    "${CMAKE_CURRENT_LIST_DIR}/AI.cmake" src/Navigation/NavigationWorld.cpp
    src/AI/BehaviorTreeRuntime.cpp src/AI/BehaviorAgentRuntime.cpp src/AI/BehaviorModule.cpp)
bazzalt_load_runtime_module(GUI BazzaltGui bazzalt_gui
    "${CMAKE_CURRENT_LIST_DIR}/GUI.cmake" src/GUI/GuiPaint.cpp src/GUI/FontFace.cpp
    src/GUI/GuiStyles.cpp src/GUI/GuiModule.cpp src/GUI/GuiSystem.cpp
    src/GUI/GuiRenderer.cpp src/GUI/GuiSerialization.cpp)
