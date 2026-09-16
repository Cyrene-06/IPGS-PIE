cmake_minimum_required(VERSION 3.16)
project(PlantSimulationSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(CTest)

option(PLANTSIM_ENABLE_AVX2 "Enable AVX2 instructions for local Release builds" ON)
option(PLANTSIM_ENABLE_IPO "Enable interprocedural optimization for Release builds" ON)

set(PLANTSIM_IPO_SUPPORTED FALSE)
if(PLANTSIM_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT PLANTSIM_IPO_SUPPORTED OUTPUT PLANTSIM_IPO_ERROR LANGUAGES CXX)
    if(NOT PLANTSIM_IPO_SUPPORTED)
        message(WARNING "Release IPO/LTO is unavailable: ${PLANTSIM_IPO_ERROR}")
    endif()
endif()

find_package(Qt5 COMPONENTS Core Widgets OpenGL Network WebSockets REQUIRED)
find_package(OpenGL REQUIRED)

# Eigen is vendored because it is header-only.
set(EIGEN_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/eigen)
if(NOT EXISTS ${EIGEN_INCLUDE_DIR}/Eigen/Dense)
    message(FATAL_ERROR "Eigen not found. Place Eigen in third_party/eigen")
endif()

add_library(PlantDataModel STATIC
    src/Plant/PlantModel.cpp
    include/Plant/PlantModel.h
    include/Plant/PlantTypes.h
    include/Algorithm/PlantNode.h
)
target_include_directories(PlantDataModel PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(PlantDataModel PUBLIC Qt5::Core)

add_library(PlantLSystem STATIC
    src/Algorithm/LSystem.cpp
    src/Algorithm/TurtleInterpreter.cpp
    src/Algorithm/PlantSkeletonPresets.cpp
    include/Algorithm/LSystem.h
    include/Algorithm/TurtleInterpreter.h
    include/Algorithm/PlantSkeletonPresets.h
)
target_include_directories(PlantLSystem PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(PlantLSystem PUBLIC PlantDataModel Qt5::Core)

add_library(PlantImplicitSurface STATIC
    src/Implicit/MetaballField.cpp
    include/Implicit/MetaballField.h
)
target_include_directories(PlantImplicitSurface PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(PlantImplicitSurface PUBLIC PlantDataModel Qt5::Core)


# 第13周：质量点—PBD 植物物理模型
add_library(PlantPhysics STATIC
    src/Physics/PlantPhysicsSolver.cpp
    include/Physics/PlantPhysicsSolver.h
)
target_include_directories(PlantPhysics PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(PlantPhysics PUBLIC PlantDataModel Qt5::Core)
# 第9/11周：生长时间模型 + 环境与向光性/向地性参数
add_library(PlantGrowth STATIC
    src/Engine/GrowthTimeline.cpp
    src/Engine/GrowthClock.cpp
    src/Engine/DynamicBranchingSystem.cpp
    src/Engine/EnvironmentParams.cpp
    src/Engine/GrowthEventManager.cpp
    src/Engine/GrowthKeyframeStore.cpp
    src/Engine/GrowthDataRecorder.cpp
    src/Engine/WindField.cpp
    include/Engine/DynamicBranchingSystem.h
    include/Engine/EnvironmentParams.h
    include/Engine/GrowthEventManager.h
    include/Engine/GrowthKeyframeStore.h
    include/Engine/GrowthDataRecorder.h
    include/Engine/WindField.h
    include/Engine/GrowthTimeline.h
    include/Engine/GrowthClock.h
    include/Engine/GrowthStateReport.h
)
target_include_directories(PlantGrowth PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(PlantGrowth PUBLIC PlantDataModel Qt5::Core)

add_library(PlantGeometry STATIC
    src/Geometry/MarchingCubes.cpp
    src/Geometry/MeshProcessing.cpp
    src/Geometry/LeafGenerator.cpp
    src/Geometry/MeshExporter.cpp
    include/Geometry/MarchingCubes.h
    include/Geometry/MarchingCubesTables.h
    include/Geometry/MeshProcessing.h
    include/Geometry/LeafGenerator.h
    include/Geometry/MeshExporter.h
)
target_include_directories(PlantGeometry PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(PlantGeometry PUBLIC PlantImplicitSurface Qt5::Core)

add_library(PlantEditing STATIC
    src/Editing/RayPicker.cpp
    src/Editing/SelectionManager.cpp
    src/Editing/GizmoRenderer.cpp
    src/Editing/ScaleTool.cpp
    src/Editing/BendTool.cpp
    src/Editing/EditHistory.cpp
    src/Editing/RotateTool.cpp
    src/Editing/NodeParameterEditor.cpp
    include/Editing/EditTypes.h
    include/Editing/EditHistory.h
    include/Editing/RayPicker.h
    include/Editing/SelectionManager.h
    include/Editing/GizmoRenderer.h
    include/Editing/ScaleTool.h
    include/Editing/BendTool.h
    include/Editing/RotateTool.h
    include/Editing/NodeParameterEditor.h
)
target_include_directories(PlantEditing PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(PlantEditing PUBLIC PlantDataModel Qt5::Core)

add_executable(PlantSimulationSystem
    src/main.cpp
    src/Engine/EngineWindow.cpp
    src/Engine/EngineWindow.h
    src/Engine/SimulationEngine.cpp
    include/Engine/SimulationEngine.h
    src/Networking/WebSocketServer.cpp
    src/Rendering/Camera.cpp
    src/Rendering/Light.h
    src/Rendering/Mesh.cpp
    src/Rendering/Shader.cpp
    src/Rendering/Renderer.cpp
)
target_include_directories(PlantSimulationSystem PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(PlantSimulationSystem PRIVATE
    PlantLSystem
    PlantImplicitSurface
    PlantGrowth
    PlantPhysics
    PlantGeometry
    PlantEditing
    Qt5::Widgets
    Qt5::OpenGL
    Qt5::Network
    Qt5::WebSockets
    OpenGL::GL
)

add_executable(PlantStructureDemo
    src/Tools/PlantStructureDemo.cpp
)
target_link_libraries(PlantStructureDemo PRIVATE PlantDataModel Qt5::Core)

add_executable(PlantEditDemo
    src/Tools/PlantEditDemo.cpp
)
target_link_libraries(PlantEditDemo PRIVATE PlantEditing PlantDataModel Qt5::Core)

add_executable(LSystemSkeletonDemo
    src/Tools/LSystemSkeletonDemo.cpp
)
target_link_libraries(LSystemSkeletonDemo PRIVATE PlantLSystem PlantDataModel Qt5::Core)

add_executable(MetaballFieldDemo
    src/Tools/MetaballFieldDemo.cpp
)
target_link_libraries(MetaballFieldDemo PRIVATE
    PlantImplicitSurface
    PlantLSystem
    PlantDataModel
    Qt5::Widgets
    Qt5::Core
)

add_executable(MarchingCubesDemo
    src/Tools/MarchingCubesDemo.cpp
)
target_link_libraries(MarchingCubesDemo PRIVATE
    PlantGeometry
    PlantImplicitSurface
    PlantLSystem
    PlantDataModel
    Qt5::Gui
    Qt5::Core
)

add_executable(PlantModelExportDemo
    src/Tools/PlantModelExportDemo.cpp
)
target_link_libraries(PlantModelExportDemo PRIVATE
    PlantGeometry
    PlantImplicitSurface
    PlantLSystem
    PlantDataModel
    Qt5::Gui
    Qt5::Core
)

add_executable(GrowthTimelineDemo
    src/Tools/GrowthTimelineDemo.cpp
)
target_link_libraries(GrowthTimelineDemo PRIVATE
    PlantGrowth
    PlantImplicitSurface
    PlantLSystem
    PlantDataModel
    Qt5::Gui
    Qt5::Core
)

add_executable(GrowthSurfaceCheck
    src/Tools/GrowthSurfaceCheck.cpp
)
target_link_libraries(GrowthSurfaceCheck PRIVATE
    PlantGrowth
    PlantImplicitSurface
    PlantLSystem
    PlantGeometry
    PlantDataModel
    Qt5::Gui
    Qt5::Core
)

add_executable(DynamicBranchingDemo
    src/Tools/DynamicBranchingDemo.cpp
)
target_link_libraries(DynamicBranchingDemo PRIVATE
    PlantGrowth
    PlantDataModel
    Qt5::Core
)


# 第13周：质量点、固定根节点与长度约束自动化验证 CLI
add_executable(PlantPhysicsDemo
    src/Tools/PlantPhysicsDemo.cpp
)
target_link_libraries(PlantPhysicsDemo PRIVATE
    PlantPhysics
    PlantDataModel
    Qt5::Core
)
# 第11周：向光性与向地性自动化验证 CLI
add_executable(TropismDemo
    src/Tools/TropismDemo.cpp
)
target_link_libraries(TropismDemo PRIVATE
    PlantGrowth
    PlantDataModel
    Qt5::Core
)

if(BUILD_TESTING)
    add_executable(PlantNumericalRegression
        src/Tests/PlantNumericalRegression.cpp
    )
    target_link_libraries(PlantNumericalRegression PRIVATE
        PlantDataModel
        PlantImplicitSurface
        PlantGeometry
        PlantGrowth
        Qt5::Core
    )

    add_executable(PlantEditEngineRegression
        src/Tests/PlantEditEngineRegression.cpp
        src/Engine/SimulationEngine.cpp
        include/Engine/SimulationEngine.h
    )
    target_include_directories(PlantEditEngineRegression PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(PlantEditEngineRegression PRIVATE
        PlantLSystem
        PlantImplicitSurface
        PlantGrowth
        PlantPhysics
        PlantGeometry
        PlantEditing
        Qt5::Core
    )

    add_executable(ScenePersistenceRegression
        src/Tests/ScenePersistenceRegression.cpp
        src/Engine/SimulationEngine.cpp
        include/Engine/SimulationEngine.h
    )
    target_include_directories(ScenePersistenceRegression PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(ScenePersistenceRegression PRIVATE
        PlantLSystem
        PlantImplicitSurface
        PlantGrowth
        PlantPhysics
        PlantGeometry
        PlantEditing
        Qt5::Core
    )

    add_test(NAME PlantNumericalRegression COMMAND PlantNumericalRegression)
    add_test(NAME PlantEditRegression COMMAND PlantEditDemo)
    add_test(NAME PlantEditEngineRegression COMMAND PlantEditEngineRegression)
    add_test(NAME ScenePersistenceRegression COMMAND ScenePersistenceRegression)
    add_test(NAME PlantPhysicsRegression COMMAND PlantPhysicsDemo)
    add_test(NAME DynamicBranchingRegression COMMAND DynamicBranchingDemo)
    add_test(NAME TropismRegression COMMAND TropismDemo)
    set_tests_properties(PlantNumericalRegression PlantPhysicsRegression
                         DynamicBranchingRegression TropismRegression
                         PROPERTIES LABELS "numerical;headless" TIMEOUT 30)
    set_tests_properties(PlantEditRegression PlantEditEngineRegression
                         PROPERTIES LABELS "editing;headless" TIMEOUT 90)
    set_tests_properties(ScenePersistenceRegression
                         PROPERTIES LABELS "persistence;headless" TIMEOUT 120)
endif()

# Apply a consistent warning/optimization policy to every project target.
# Debug keeps its debuggability; Release uses optimized code, optional AVX2, and
# CMake-managed IPO/LTO when the active compiler/linker supports it.
function(plantsim_configure_target target)
    # Eigen is a vendored dependency. Mark it as a system include so /W4 reports
    # actionable diagnostics from this project rather than template internals.
    target_include_directories(${target} SYSTEM PUBLIC ${EIGEN_INCLUDE_DIR})

    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus)
        target_compile_options(${target} PRIVATE
            "$<$<CONFIG:Release>:/O2>"
            "$<$<CONFIG:Release>:/Ob2>"
            "$<$<CONFIG:Release>:/Oi>"
            "$<$<CONFIG:Release>:/Ot>")
        if(PLANTSIM_ENABLE_AVX2)
            target_compile_options(${target} PRIVATE "$<$<CONFIG:Release>:/arch:AVX2>")
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(${target} PRIVATE "$<$<CONFIG:Release>:-O3>")
    endif()

    if(PLANTSIM_IPO_SUPPORTED)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endfunction()

set(PLANTSIM_PROJECT_TARGETS
    PlantDataModel
    PlantLSystem
    PlantImplicitSurface
    PlantPhysics
    PlantGrowth
    PlantGeometry
    PlantEditing
    PlantSimulationSystem
    PlantStructureDemo
    PlantEditDemo
    LSystemSkeletonDemo
    MetaballFieldDemo
    MarchingCubesDemo
    PlantModelExportDemo
    GrowthTimelineDemo
    GrowthSurfaceCheck
    DynamicBranchingDemo
    PlantPhysicsDemo
    TropismDemo
)
if(BUILD_TESTING)
    list(APPEND PLANTSIM_PROJECT_TARGETS
        PlantNumericalRegression
        PlantEditEngineRegression
        ScenePersistenceRegression
    )
endif()
foreach(target IN LISTS PLANTSIM_PROJECT_TARGETS)
    plantsim_configure_target(${target})
endforeach()
