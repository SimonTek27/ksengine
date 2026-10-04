cmake_minimum_required(VERSION 3.22)
project(kseditor_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOTEST_TARGETS_WATCH "")

if(MSVC)
    set(QT6_PATH "C:/Qt/6.11.1/msvc2022_64" CACHE PATH "Qt6 path")
else()
    set(QT6_PATH "C:/Qt/6.11.1/mingw_64" CACHE PATH "Qt6 path")
endif()
list(PREPEND CMAKE_PREFIX_PATH "${QT6_PATH}")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../external/vcpkg/installed/x64-windows")
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../external/vcpkg/installed/x64-windows")
endif()

find_package(Qt6 COMPONENTS Core Test Network Widgets Multimedia OpenGL OpenGLWidgets Quick Qml QuickWidgets Quick3D Charts 3DCore 3DExtras 3DRender WebSockets REQUIRED)
find_package(Vulkan QUIET)
find_package(Python3 COMPONENTS Development QUIET)
include(CTest)

# Locate the prebuilt kseditor_lib produced by the main project build.
find_library(KSEDITOR_LIB
    NAMES kseditor_lib.lib libkseditor_lib.a
    HINTS
        # The default top-level build is configured in the repository root.
        # Keep the legacy build-directory hints below for existing setups.
        "${CMAKE_CURRENT_SOURCE_DIR}/../Release"
        "${CMAKE_CURRENT_SOURCE_DIR}/../build/Release"
        "${CMAKE_CURRENT_SOURCE_DIR}/../build/lib/Release"
        "${CMAKE_CURRENT_SOURCE_DIR}/../build"
        "${CMAKE_CURRENT_SOURCE_DIR}/../bin/Release"
    NO_DEFAULT_PATH
)
if(KSEDITOR_LIB)
    message(STATUS "Found kseditor_lib: ${KSEDITOR_LIB}")
else()
    message(WARNING "kseditor_lib not found - build the main project (cmake --build build) first")
endif()

set(VCPKG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external/vcpkg/installed/x64-windows")
if(EXISTS "${VCPKG_DIR}/share/cgal/CGALConfig.cmake")
    list(APPEND CMAKE_PREFIX_PATH "${VCPKG_DIR}")
    set(CGAL_DIR "${VCPKG_DIR}/share/cgal")
    find_package(CGAL QUIET)
    if(CGAL_FOUND)
        add_compile_definitions(HAS_CGAL=1)
        include_directories("${VCPKG_DIR}/include")
        message(STATUS "CGAL found for test targets: ${CGAL_VERSION}")
    else()
        add_compile_definitions(HAS_CGAL=0)
        message(STATUS "CGAL not found for test targets")
    endif()
else()
    add_compile_definitions(HAS_CGAL=0)
    message(STATUS "vcpkg/CGAL not found, boolean ops tests will be limited")
endif()

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(ENGINE "${SRC}/engine")
set(KS "${SRC}/sdk/kseditor")
# Legacy name: old src/core split between engine/ and sdk/kseditor/modules/.
set(COMMON "${KS}/modules")
set(MODULES "${KS}/modules")
set(PHYSICS "${MODULES}/PhysicsEditor")
set(PLUGIN_PHYSICS "${KS}/plugins/simulators/kunos/assettocorsa/physics")

# ${KS} (the Qt-rich overlay src/sdk/kseditor) must come before ${SRC}/
# ${ENGINE}: quoted includes like "engine/mesh/X.h" would otherwise resolve
# to the qt-free stub headers under src/engine and fail with namespace
# mismatches (same rule as the kslib/kseditor targets).
set(KS_TEST_INCLUDES
    ${KS} ${SRC} ${ENGINE} ${COMMON} ${MODULES}
    ${ENGINE}/Config ${ENGINE}/sys ${ENGINE}/Tools ${ENGINE}/tools ${ENGINE}/FileFormat
    ${ENGINE}/assets ${ENGINE}/material ${ENGINE}/mesh ${ENGINE}/Graphics
    ${ENGINE}/animation ${ENGINE}/physics ${ENGINE}/AI ${ENGINE}/scene
    ${ENGINE}/Scripting ${ENGINE}/Math ${ENGINE}/Audio
    ${COMMON}/editor ${COMMON}/modmanager ${COMMON}/tools ${COMMON}/help ${COMMON}/vcs
    ${COMMON}/modellingEditor ${COMMON}/workshop ${COMMON}/cockpitInstruments
    ${COMMON}/fontEditor ${COMMON}/LicensePlatesEditor ${COMMON}/ShowroomEditor
    ${COMMON}/PaintEditor ${COMMON}/paint ${COMMON}/sound ${COMMON}/moduleManager
    ${PHYSICS} ${PHYSICS}/telemetry ${PHYSICS}/SetupEditor ${PLUGIN_PHYSICS}
    ${MODULES}/editor/ppfiltersEditor
)

# Bullet is configured by the top-level project, but this is an independent
# CMake project. Detect its headers here rather than relying on an unset
# top-level cache variable.
set(BULLET_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/../external/bullet/src/btBulletDynamicsCommon.h")
if(EXISTS "${BULLET_HEADER}")
    set(HAS_BULLET TRUE)
    message(STATUS "Bullet headers found for tests")
    add_compile_definitions(HAS_BULLET=1)
else()
    set(HAS_BULLET FALSE)
    message(STATUS "Bullet not found for tests")
    add_compile_definitions(HAS_BULLET=0)
endif()

# VHACD (convex decomposition, from Bullet Extras) - matches main build detection
set(VHACD_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/../external/bullet/Extras/VHACD/public/VHACD.h")
set(VHACD_LIB_CANDIDATES
    "${CMAKE_CURRENT_SOURCE_DIR}/../vhacd/Release/VHACD.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../build/vhacd/Release/VHACD.lib"
)
set(VHACD_LIB "")
foreach(_vhacd_lib IN LISTS VHACD_LIB_CANDIDATES)
    if(EXISTS "${_vhacd_lib}")
        set(VHACD_LIB "${_vhacd_lib}")
        break()
    endif()
endforeach()
if(EXISTS "${VHACD_HEADER}" AND EXISTS "${VHACD_LIB}")
    set(HAS_VHACD TRUE)
    add_compile_definitions(HAS_VHACD=1)
    message(STATUS "VHACD found for tests: ${VHACD_LIB}")
else()
    set(HAS_VHACD FALSE)
    message(STATUS "VHACD not found for tests")
    add_compile_definitions(HAS_VHACD=0)
endif()

macro(ks_add_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    target_link_libraries(${TARGET_NAME} PRIVATE Qt6::Core Qt6::Test Qt6::Network Qt6::Widgets Qt6::Multimedia Qt6::OpenGL ${KSEDITOR_LIB})
    target_include_directories(${TARGET_NAME} PRIVATE ${KS_TEST_INCLUDES})
    if(WIN32)
        target_compile_definitions(${TARGET_NAME} PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    endif()
    add_test(NAME ${TARGET_NAME}
             COMMAND ${TARGET_NAME} -v2 -o ${CMAKE_BINARY_DIR}/${TARGET_NAME}.xml,xunitxml)
    set_tests_properties(${TARGET_NAME} PROPERTIES TIMEOUT 60)
endmacro()

ks_add_test(test_boolean_ops  kseditor/test_BooleanOps.cpp
    ${KS}/engine/mesh/BooleanOps.cpp
)
if(CGAL_FOUND)
    target_link_libraries(test_boolean_ops PRIVATE CGAL::CGAL)
endif()
ks_add_test(test_all_physics kseditor/test_PhysicsSimulator.cpp
    ${PHYSICS}/PhysicsSimulator.cpp
    ${PHYSICS}/PhysicsProfiler.cpp
    ${PLUGIN_PHYSICS}/PacejkaTireModel.cpp
    ${PLUGIN_PHYSICS}/EngineModel.cpp
    ${PLUGIN_PHYSICS}/DifferentialModel.cpp
    ${PLUGIN_PHYSICS}/BrakeThermalModel.cpp
    ${PLUGIN_PHYSICS}/SuspensionModel.cpp
    ${PLUGIN_PHYSICS}/AeroModel.cpp
    ${KS}/engine/physics/PhysicsEngine.cpp
    ${PLUGIN_PHYSICS}/HybridSystem.cpp
)
ks_add_test(test_all_ai      kseditor/test_AllAI.cpp
    ${MODULES}/editor/AIEditor/AITelemetryTrainer.cpp
    ${MODULES}/editor/AIEditor/AiBehaviorModel.cpp
    ${MODULES}/editor/AIEditor/MultiCarAI.cpp
)
ks_add_test(test_ai_behavior  kseditor/test_AI.cpp
    ${MODULES}/editor/AIEditor/AiBehaviorModel.cpp
)
ks_add_test(test_telemetry_feedback kseditor/test_TelemetryFeedback.cpp
    ${PHYSICS}/telemetry/TelemetryFeedbackBridge.cpp
    ${PHYSICS}/telemetry/TelemetryQmlBridge.cpp
    ${PHYSICS}/PhysicsSimulator.cpp
    ${PHYSICS}/PhysicsProfiler.cpp
    ${PLUGIN_PHYSICS}/PacejkaTireModel.cpp
    ${PLUGIN_PHYSICS}/EngineModel.cpp
    ${PLUGIN_PHYSICS}/DifferentialModel.cpp
    ${PLUGIN_PHYSICS}/BrakeThermalModel.cpp
    ${PLUGIN_PHYSICS}/SuspensionModel.cpp
    ${PLUGIN_PHYSICS}/AeroModel.cpp
    ${KS}/engine/physics/PhysicsEngine.cpp
    ${PLUGIN_PHYSICS}/HybridSystem.cpp
)
ks_add_test(test_physics_profiler kseditor/test_PhysicsProfiler.cpp
    ${PHYSICS}/PhysicsProfiler.cpp
)
ks_add_test(test_laptime_validation kseditor/test_LapTimeValidation.cpp
    ${PHYSICS}/telemetry/LapTimeValidation.cpp
)
ks_add_test(test_license_plates kseditor/test_LicensePlates.cpp
    ${COMMON}/editor/EditorModule.cpp
    ${PHYSICS}/../LicensePlatesEditor/LicensePlateEditorModule.cpp
    ${PHYSICS}/../LicensePlatesEditor/QRCodeWriter.cpp
)
ks_add_test(test_display_editor kseditor/test_CockpitInstruments.cpp
    ${MODULES}/cockpitInstruments/CockpitInstruments.cpp
    ${MODULES}/cockpitInstruments/CockpitInstrumentsQmlBridge.cpp
    ${MODULES}/cockpitInstruments/LuaExpressionEvaluator.cpp
)
ks_add_test(test_ppfilter_color_grading kseditor/test_PPFilterColorGrading.cpp
    ${MODULES}/editor/ppfiltersEditor/PPFilterColorGrading.cpp
)
ks_add_test(test_modmanager_deps kseditor/test_ModManagerDeps.cpp)
ks_add_test(test_modmanager_features kseditor/test_ModManagerFeatures.cpp
    ${COMMON}/modmanager/ModCollection.cpp
)
# FontAtlasGenerator/AtlasConfig (modules/fontEditor/FontGenerator.h) no
# longer exist anywhere in src — the font system was refactored to the
# FontCreator API. Test target disabled until rewritten against it.
# ks_add_test(test_font_generator kseditor/test_FontGenerator.cpp)
ks_add_test(test_audio_effects_advanced kseditor/test_AudioEffectsAdvanced.cpp)
ks_add_test(test_content_repair kseditor/test_ContentRepair.cpp
    ${COMMON}/modmanager/ContentRepair.cpp
    ${COMMON}/modmanager/ContentRepairTool.cpp
    ${KS}/engine/sys/LogManager.cpp
    ${KS}/engine/Config/EditorConfig.cpp
    ${MODULES}/editor/EditorModule.cpp
    ${ENGINE}/assets/Paths.cpp
    ${ENGINE}/assets/SimInstallDetector.cpp
    ${KS}/plugins/simulators/kunos/assettocorsa/KsAssettoCorsaContentPath.cpp
)
ks_add_test(test_csp_config kseditor/test_CspConfig.cpp
    ${KS}/engine/Config/CspConfigParser.cpp
)
ks_add_test(test_kn5_parser kseditor/test_KN5Parser.cpp
    ${KS}/plugins/simulators/kunos/assettocorsa/acFiles/KN5Parser.cpp
    ${KS}/plugins/simulators/kunos/assettocorsa/acFiles/KN5Baker.cpp
    ${KS}/plugins/simulators/kunos/assettocorsa/acFiles/KN5Decrypt.cpp
    ${ENGINE}/FileFormat/Kn5Reader.cpp
)
ks_add_test(test_workshop kseditor/test_Workshop.cpp
    ${COMMON}/workshop/WorkshopManager.cpp
    ${KS}/engine/sys/LogManager.cpp
)
ks_add_test(test_physics_mesh kseditor/test_PhysicsMeshGenerator.cpp
    ${MODULES}/modellingEditor/PhysicsMeshGenerator.cpp
    ${KS}/engine/mesh/MeshOperations.cpp
    ${KS}/engine/mesh/BooleanOps.cpp
)
if(CGAL_FOUND)
    target_link_libraries(test_physics_mesh PRIVATE CGAL::CGAL)
endif()
if(HAS_BULLET)
    target_link_libraries(test_physics_mesh PRIVATE
        BulletDynamics BulletCollision LinearMath
    )
    target_include_directories(test_physics_mesh PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../external/bullet/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../external/bullet/Extras/VHACD/public"
    )
endif()
if(HAS_VHACD)
    target_link_libraries(test_physics_mesh PRIVATE "${VHACD_LIB}")
    target_include_directories(test_physics_mesh PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../external/bullet/Extras/VHACD/public"
    )
    # VHACD depends on Bullet (LinearMath / ConvexHullComputer)
endif()
# Test subdivision - uses fallback when OpenSubdiv not available
ks_add_test(test_subdivision kseditor/test_SubdivisionSurface.cpp
    ${KS}/engine/mesh/SubdivisionSurface.cpp
)
ks_add_test(test_texture_paint kseditor/test_TexturePaintSystem.cpp
    ${KS}/engine/material/TexturePaintSystem.cpp
)
ks_add_test(test_lod_generator kseditor/test_LODGenerator.cpp
    ${KS}/engine/mesh/MeshOperations.cpp
    ${KS}/engine/mesh/BooleanOps.cpp
)
if(CGAL_FOUND)
    target_link_libraries(test_lod_generator PRIVATE CGAL::CGAL)
endif()
ks_add_test(test_asset_dedup kseditor/test_AssetDedup.cpp
    ${KS}/engine/assets/AssetManager.cpp
    ${KS}/engine/assets/AssetFileWatcher.cpp
    ${KS}/engine/assets/AssetSearchEngine.cpp
)
ks_add_test(test_sculpt_mode_minimal kseditor/test_SculptMode_minimal.cpp
)

ks_add_test(test_sculpt_mode kseditor/test_SculptMode.cpp
    ${KS}/engine/mesh/SculptMode.cpp
)
ks_add_test(test_mesh_operations kseditor/test_MeshOperations.cpp
    ${KS}/engine/mesh/MeshOperations.cpp
)
if(CGAL_FOUND)
    target_sources(test_mesh_operations PRIVATE ${KS}/engine/mesh/BooleanOps.cpp)
    target_link_libraries(test_mesh_operations PRIVATE CGAL::CGAL)
endif()
ks_add_test(test_modifiers kseditor/test_Modifiers.cpp
    ${KS}/engine/mesh/MeshOperations.cpp
    ${KS}/engine/mesh/ModifierSystem.cpp
    ${KS}/engine/mesh/SkeletonSystem.cpp
    ${KS}/engine/mesh/SubdivisionSurface.cpp
    ${MODULES}/modellingEditor/AdditionalModifiers.cpp
)
target_include_directories(test_modifiers PRIVATE ${MODULES}/modellingEditor)
if(CGAL_FOUND)
    target_sources(test_modifiers PRIVATE ${KS}/engine/mesh/BooleanOps.cpp)
    target_link_libraries(test_modifiers PRIVATE CGAL::CGAL)
endif()
ks_add_test(test_advanced_sculpt kseditor/test_AdvancedSculpt.cpp
    ${KS}/engine/mesh/AdvancedSculpt.cpp
    ${KS}/engine/mesh/SculptMode.cpp
)
ks_add_test(test_constraints kseditor/test_Constraints.cpp
    ${MODULES}/modellingEditor/ConstraintSystem.cpp
    ${KS}/engine/Graphics/SceneGraph.cpp
    ${KS}/engine/Graphics/SceneObject.cpp
)
ks_add_test(test_controller_system kseditor/test_ControllerSystem.cpp
    ${MODULES}/modellingEditor/ControllerSystem.cpp
    ${MODULES}/modellingEditor/SceneParamAccess.cpp
    ${KS}/engine/Graphics/SceneGraph.cpp
    ${KS}/engine/Graphics/SceneObject.cpp
)
ks_add_test(test_wire_parameters kseditor/test_WireParameterSystem.cpp
    ${MODULES}/modellingEditor/WireParameterSystem.cpp
    ${MODULES}/modellingEditor/SceneParamAccess.cpp
    ${KS}/engine/Graphics/SceneGraph.cpp
    ${KS}/engine/Graphics/SceneObject.cpp
)
ks_add_test(test_skin_wrap kseditor/test_SkinWrapSystem.cpp
    ${MODULES}/modellingEditor/SkinWrapSystem.cpp
    ${KS}/engine/Graphics/SceneGraph.cpp
    ${KS}/engine/Graphics/SceneObject.cpp
)
ks_add_test(test_ray_trace kseditor/test_RayTrace.cpp
    ${MODULES}/modellingEditor/RayTraceRenderer.cpp
)
ks_add_test(test_light_system kseditor/test_LightSystem.cpp
    ${MODULES}/modellingEditor/LightSystem.cpp
)
ks_add_test(test_bake_texture kseditor/test_BakeTexture.cpp
    ${MODULES}/modellingEditor/TextureBaker.cpp
    ${MODULES}/modellingEditor/Geometry3D.cpp
)
ks_add_test(test_animation_system kseditor/test_AnimationSystem.cpp
    ${KS}/engine/animation/AnimationSystem.cpp
    ${COMMON}/editor/TimelineEditor.cpp
)
ks_add_test(test_material_system kseditor/test_MaterialSystem.cpp
    ${KS}/engine/material/MaterialSystem.cpp
)
ks_add_test(test_node_material_editor kseditor/test_NodeMaterialEditor.cpp
    ${MODULES}/modellingEditor/NodeMaterialEditor.cpp
)
ks_add_test(test_usda_exporter kseditor/test_USDAExporter.cpp
    ${KS}/engine/FileFormat/USDAExporter.cpp
)
ks_add_test(test_theme_system kseditor/test_ThemeSystem.cpp
    ${KS}/engine/Tools/ThemeSystem.cpp
)
ks_add_test(test_undo_redo kseditor/test_UndoRedo.cpp
    ${KS}/engine/sys/UndoStack.cpp
)
ks_add_test(test_validation_system kseditor/test_ValidationSystem.cpp
    ${KS}/engine/Tools/QualitySystem.cpp
)
ks_add_test(test_state_machine kseditor/test_StateMachine.cpp
    ${KS}/engine/sys/StateMachine.cpp
)
ks_add_test(test_command_palette kseditor/test_CommandPalette.cpp
    ${MODULES}/modellingEditor/CommandPalette.cpp
)
ks_add_test(test_cache_manager kseditor/test_CacheManager.cpp
    ${KS}/engine/Tools/CacheManager.cpp
)
ks_add_test(test_settings_system kseditor/test_SettingsSystem.cpp
    ${KS}/sys/SettingsManager.cpp
)
ks_add_test(test_backup_system kseditor/test_BackupSystem.cpp
    ${KS}/engine/Tools/BackupSystem.cpp
)
ks_add_test(test_recent_files kseditor/test_RecentFilesManager.cpp
    ${KS}/engine/assets/RecentFilesManager.cpp
)
ks_add_test(test_notification_system kseditor/test_NotificationSystem.cpp
    ${KS}/engine/Tools/NotificationSystem.cpp
)
ks_add_test(test_auto_save kseditor/test_AutoSave.cpp
    ${KS}/engine/Tools/AutoSave.cpp
)
ks_add_test(test_showroom_editor kseditor/test_ShowroomEditor.cpp
    ${MODULES}/ShowroomEditor/ShowroomEditorModule.cpp
    ${MODULES}/ShowroomEditor/ShowroomSystem.cpp
    ${MODULES}/ShowroomEditor/ShowroomViewport3D.cpp
    ${COMMON}/editor/EditorModule.cpp
    ${KS}/engine/Config/EditorConfig.cpp
    ${KS}/engine/sys/LogManager.cpp
)

# kseditor_lib is a monolithic static library, so any test executable may pull in
# CGAL (BooleanOps) or 7-Zip (archive) object files via symbol references. Link
# those dependencies to every test target to avoid unresolved-external link errors.
set(SEVENZIP_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/Release")
if(NOT EXISTS "${SEVENZIP_LIB_DIR}/7zip_archive.lib")
    set(SEVENZIP_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build/lib/Release")
endif()
set(SEVENZIP_LIBS
    "${SEVENZIP_LIB_DIR}/7zip_archive.lib"
    "${SEVENZIP_LIB_DIR}/7zip_crypto.lib"
    "${SEVENZIP_LIB_DIR}/7zip_compress.lib"
    "${SEVENZIP_LIB_DIR}/7zip_common.lib"
    "${SEVENZIP_LIB_DIR}/7zip_cpp_common.lib"
    "${SEVENZIP_LIB_DIR}/7zip_win.lib"
    "${SEVENZIP_LIB_DIR}/7zip_c.lib"
)
set(BULLET_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../bullet/Release")
if(NOT EXISTS "${BULLET_LIB_DIR}/BulletCollision.lib")
    set(BULLET_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build/bullet/Release")
endif()
get_property(ALL_TEST_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t ${ALL_TEST_TARGETS})
    if(_t MATCHES "^test_")
        if(CGAL_FOUND)
            target_link_libraries(${_t} PRIVATE CGAL::CGAL)
        endif()
        if(EXISTS "${SEVENZIP_LIB_DIR}/7zip_archive.lib")
            target_link_libraries(${_t} PRIVATE ${SEVENZIP_LIBS})
        endif()
        if(HAS_VHACD AND EXISTS "${VHACD_LIB}")
            target_link_libraries(${_t} PRIVATE "${VHACD_LIB}")
            target_include_directories(${_t} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/../external/bullet/Extras/VHACD/public")
        endif()
        if(EXISTS "${BULLET_LIB_DIR}/BulletCollision.lib")
            target_link_libraries(${_t} PRIVATE
                "${BULLET_LIB_DIR}/BulletCollision.lib"
                "${BULLET_LIB_DIR}/BulletDynamics.lib"
                "${BULLET_LIB_DIR}/LinearMath.lib")
        endif()
        target_link_libraries(${_t} PRIVATE Qt6::Quick Qt6::Qml Qt6::QuickWidgets Qt6::Quick3D Qt6::Charts
            Qt6::3DCore Qt6::3DExtras Qt6::3DRender Qt6::WebSockets)
        if(Python3_FOUND)
            target_link_libraries(${_t} PRIVATE Python3::Python)
        endif()
        if(Vulkan_FOUND)
            target_link_libraries(${_t} PRIVATE Vulkan::Vulkan)
        endif()
        target_link_libraries(${_t} PRIVATE opengl32 dwmapi)
        if(MSVC)
            # 7-Zip static lib duplicates IID symbols already present in kseditor_lib
            # (same situation handled in the main build via /FORCE:MULTIPLE).
            target_link_options(${_t} PRIVATE "LINKER:/FORCE:MULTIPLE")
        endif()
    endif()
endforeach()

# Plugin smoke test — loads ksAssettoCorsa.dll at runtime via QLibrary and
# verifies all C API exports. Does NOT link kseditor_lib; purely dynamic.
if(WIN32)
    add_executable(test_ksAssettoCorsa kseditor/test_ksAssettoCorsa.cpp)
    target_link_libraries(test_ksAssettoCorsa PRIVATE Qt6::Core Qt6::Test)
    target_include_directories(test_ksAssettoCorsa PRIVATE ${KS_TEST_INCLUDES})
    target_compile_definitions(test_ksAssettoCorsa PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    add_test(NAME test_ksAssettoCorsa
             COMMAND test_ksAssettoCorsa -v2 -o ${CMAKE_BINARY_DIR}/test_ksAssettoCorsa.xml,xunitxml)
    set_tests_properties(test_ksAssettoCorsa PROPERTIES TIMEOUT 30)
endif()
