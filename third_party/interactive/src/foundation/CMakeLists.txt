# SPDX-License-Identifier: LGPL-3.0-or-later
set_source_files_properties(ControlState.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(interactive_foundation
    SHARED
    URI InterActive.Foundation
    VERSION 1.0
    # QColor and QFont value-type metadata is provided by QtQuick.
    DEPENDENCIES QtQuick
    SOURCES
        Appearance.h Appearance.cpp
        AppearanceSource_p.h
        TokenDefinitions.h TokenGroups.h TokenGroups.cpp
        TokenSchema.h TokenSchema.cpp
        TokenValidation_p.h TokenValidation_p.cpp
        PaletteBuilder_p.h PaletteBuilder_p.cpp
        ColorMath_p.h ColorMath_p.cpp
        PaletteState_p.h PaletteState_p.cpp
        AppearanceTrace_p.h AppearanceTrace_p.cpp
        LocalColorOverrides_p.h LocalColorOverrides_p.cpp
        FontEnvironment_p.h FontEnvironment_p.cpp
        AirmailProfiles.h AirmailProfiles.cpp
        ControlAppearance.h ControlAppearance.cpp
        SeparatorAppearance.h SeparatorAppearance.cpp
        StateTable.h StateTable.cpp
        Presentation.h Presentation.cpp
        ThemeIconItem.h ThemeIconItem.cpp
        ColorPlaneSurface.h ColorPlaneSurface.cpp
        IconSource.h
        ScreenGeometry.h ScreenGeometry.cpp
    QML_FILES Icon.qml IconLabel.qml Separator.qml ControlState.qml ControlBackground.qml SegmentBackground.qml ChoiceIndicator.qml ChoiceRow.qml ValueTrack.qml SegmentGroup.qml PopupDirection.qml
    RESOURCES
        icons/check.svg icons/minus.svg icons/chevron-down.svg
        icons/chevron-right.svg icons/close.svg icons/search.svg icons/color-wheel.svg
)

set(material_color_utilities_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/material-color-utilities")
target_sources(interactive_foundation PRIVATE
    "${material_color_utilities_dir}/cpp/cam/hct.cc"
    "${material_color_utilities_dir}/cpp/cam/hct_solver.cc"
    "${material_color_utilities_dir}/cpp/cam/cam.cc"
    "${material_color_utilities_dir}/cpp/utils/utils.cc"
    "${material_color_utilities_dir}/cpp/contrast/contrast.cc"
)
target_include_directories(interactive_foundation PRIVATE "${material_color_utilities_dir}")

qt_add_shaders(interactive_foundation ia_icon_shaders
    PREFIX "/qt/qml/InterActive/Foundation"
    FILES shaders/icon-tint.frag
)

include(GenerateExportHeader)
generate_export_header(interactive_foundation)
add_library(InterActive::Foundation ALIAS interactive_foundation)
set_target_properties(interactive_foundation PROPERTIES
    EXPORT_NAME Foundation
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
target_include_directories(interactive_foundation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include/InterActive/Foundation>
)
target_link_libraries(interactive_foundation PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
