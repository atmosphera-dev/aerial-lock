// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

namespace InterActive {
inline constexpr double LabelContrastTarget = 7;
}

// Shared by the typed output groups, profiles, and validator. Numeric bounds
// apply to application and component inputs, not to structural layout math.
#define IA_COLOR_TOKENS(X) \
    X(canvas, "#F4F5F7", "#202124") \
    X(content, "#FFFFFF", "#27292D") \
    X(popup, "#FFFFFF", "#303237") \
    X(text, "#25282D", "#ECEEF2") \
    X(secondaryText, "#616873", "#B0B6C0") \
    X(placeholderText, "#616873", "#B0B6C0") \
    X(disabledText, "#858C96", "#777F8B") \
    X(disabledSurface, "#F0F1F3", "#2B2D31") \
    X(control, "#F0F2F4", "#35373B") \
    X(controlHover, "#E7EAEE", "#3E4146") \
    X(controlPressed, "#DDE2E7", "#484C52") \
    X(selection, "#E3E8ED", "#41464E") \
    X(inactiveSelection, "#EDF0F3", "#32353A") \
    X(outline, "#7D8591", "#868F9B") \
    X(separator, "#DFE3E8", "#41454C") \
    X(accent, "#5C9CF5", "#5C9CF5") \
    X(onAccent, "#FFFFFF", "#152538") \
    X(accentText, "#2762A8", "#86B7EF") \
    X(error, "#E06C75", "#E06C75") \
    X(onError, "#FFFFFF", "#202124") \
    X(errorText, "#B8323D", "#FF9097") \
    X(warning, "#F5A742", "#F5A742") \
    X(onWarning, "#202124", "#202124") \
    X(warningText, "#855800", "#E8C274") \
    X(success, "#2C704F", "#84CCA1") \
    X(successText, "#2C704F", "#84CCA1") \
    X(tooltipBackground, "#282B30", "#E5E9EE") \
    X(tooltipText, "#F4F5F7", "#202124") \
    X(inverseSurface, "#1F2022", "#1F2022") \
    X(inverseText, "#EEF0F4", "#EEF0F4") \
    X(inverseSecondary, "#AEB5BF", "#AEB5BF") \
    X(scrim, "#3D000000", "#7A000000") \
    X(shadow, "#29000000", "#5C000000") \
    X(focus, "#2762A8", "#86B7EF") \
    X(link, "#2762A8", "#86B7EF") \
    X(transparent, "#00000000", "#00000000") \
    X(accentHover, "#2762A8", "#86B7EF") \
    X(accentPressed, "#2762A8", "#86B7EF") \
    X(errorHover, "#B8323D", "#FF9097") \
    X(errorPressed, "#B8323D", "#FF9097") \
    X(warningHover, "#F5A742", "#F5A742") \
    X(warningPressed, "#F5A742", "#F5A742") \
    X(primarySurface, "#FFFFFF", "#27292D") \
    X(primaryHover, "#FFFFFF", "#27292D") \
    X(primaryPressed, "#FFFFFF", "#27292D") \
    X(primaryText, "#2762A8", "#86B7EF") \
    X(primaryOutline, "#7D8591", "#868F9B") \
    X(destructiveSurface, "#FFFFFF", "#27292D") \
    X(destructiveHover, "#FFFFFF", "#27292D") \
    X(destructivePressed, "#FFFFFF", "#27292D") \
    X(destructiveText, "#B8323D", "#FF9097") \
    X(destructiveOutline, "#7D8591", "#868F9B") \
    X(cautionSurface, "#F5A742", "#F5A742") \
    X(cautionHover, "#F5A742", "#F5A742") \
    X(cautionPressed, "#F5A742", "#F5A742") \
    X(cautionText, "#202124", "#202124") \
    X(cautionOutline, "#7D8591", "#868F9B") \
    X(errorSurface, "#FFFFFF", "#27292D") \
    X(warningSurface, "#FFFFFF", "#27292D") \
    X(successSurface, "#FFFFFF", "#27292D")

#define IA_METRIC_TOKENS(X) \
    X(spacingXS, 4, 0, 128) \
    X(spacingS, 8, 0, 128) \
    X(spacingM, 12, 0, 128) \
    X(spacingL, 16, 0, 256) \
    X(spacingXL, 24, 0, 256) \
    X(controlHeight, 30, 0, 512) \
    X(toolHeight, 28, 0, 512) \
    X(controlRadius, 4, 0, 128) \
    X(popupRadius, 8, 0, 128) \
    X(iconSize, 16, 1, 256) \
    X(indicatorSize, 16, 1, 256) \
    X(choiceGap, 2, 0, 128) \
    X(choiceCenterRadius, 2, 0, 128) \
    X(switchWidth, 30, 1, 256) \
    X(switchHeight, 18, 1, 256) \
    X(thumbSize, 14, 1, 256) \
    X(trackHeight, 4, 1, 128) \
    X(sliderThumbSize, 16, 1, 256) \
    X(borderWidth, 1, 0, 16) \
    X(focusWidth, 2, 0, 16) \
    X(pressInsetDepth, 8, 0, 64) \
    X(pressContentShift, 1, 0, 8) \
    X(rowHeight, 30, 0, 512) \
    X(popupPadding, 8, 0, 128) \
    X(menuMinWidth, 180, 0, 2048) \
    X(dialogMinWidth, 180, 0, 2048) \
    X(targetSize, 28, 0, 512) \
    X(horizontalPadding, 12, 0, 128) \
    X(verticalPadding, 6, 0, 128) \
    X(spacing, 8, 0, 128) \
    X(scrollBarWidth, 8, 1, 128) \
    X(scrollBarMinimumLength, 24, 1, 512) \
    X(progressHeight, 4, 1, 128) \
    X(busySize, 20, 1, 256) \
    X(shadowRadius, 12, 0, 128) \
    X(shadowOffset, 2, 0, 128) \
    X(hairlineWidth, 1, 0, 8) \
    X(fieldWidth, 180, 0, 2048) \
    X(sliderLength, 180, 0, 2048) \
    X(progressWidth, 180, 0, 2048)

#define IA_TYPE_SIZE_TOKENS(X) \
    X(controlSize, 13, 6, 128) \
    X(supportingSize, 12, 6, 128) \
    X(headingSize, 16, 6, 128)

#define IA_TYPE_WEIGHT_TOKENS(X) \
    X(controlWeight, 400, 1, 1000) \
    X(emphasisWeight, 600, 1, 1000)

#define IA_MOTION_INT_TOKENS(X) \
    X(feedbackDuration, 120, 0, 10000) \
    X(popupDuration, 150, 0, 10000) \
    X(busyDuration, 1000, 0, 10000) \
    X(tooltipDelay, 500, 0, 60000) \
    X(tooltipTimeout, 5000, 0, 60000) \
    X(easing, 6, 0, 40)

#define IA_MOTION_REAL_TOKENS(X) \
    X(hoverBlend, 0.08, 0, 1) \
    X(pressedBlend, 0.16, 0, 1) \
    X(pressInsetOpacity, 0.28, 0, 1) \
    X(semanticBlend, 0.08, 0, 1) \
    X(disabledOpacity, 1, 0, 1)

#define IA_ICON_URL_TOKENS(X) \
    X(check, "check.svg") \
    X(minus, "minus.svg") \
    X(chevronDown, "chevron-down.svg") \
    X(chevronRight, "chevron-right.svg") \
    X(close, "close.svg") \
    X(search, "search.svg")

// The public facade and its private indexed records use one property order.
#define IA_CONTROL_COLOR_PROPERTIES(X) \
    X(QColor, backgroundColor, BackgroundColor, colors, control) \
    X(QColor, fillColor, FillColor, colors, accent) \
    X(QColor, foregroundColor, ForegroundColor, colors, text) \
    X(QColor, hoverBackgroundColor, HoverBackgroundColor, colors, controlHover) \
    X(QColor, pressedBackgroundColor, PressedBackgroundColor, colors, controlPressed) \
    X(QColor, disabledBackgroundColor, DisabledBackgroundColor, colors, disabledSurface) \
    X(QColor, disabledForegroundColor, DisabledForegroundColor, colors, disabledText) \
    X(QColor, inactiveBackgroundColor, InactiveBackgroundColor, colors, inactiveSelection) \
    X(QColor, inactiveForegroundColor, InactiveForegroundColor, colors, text) \
    X(QColor, selectedBackgroundColor, SelectedBackgroundColor, colors, selection) \
    X(QColor, selectedForegroundColor, SelectedForegroundColor, colors, text) \
    X(QColor, borderColor, BorderColor, colors, outline) \
    X(QColor, disabledBorderColor, DisabledBorderColor, colors, outline) \
    X(QColor, focusColor, FocusColor, colors, focus)

#define IA_CONTROL_NONCOLOR_PROPERTIES(X) \
    X(qreal, controlRadius, ControlRadius, metrics, controlRadius) \
    X(QString, fontFamily, FontFamily, typography, family) \
    X(qreal, fontPixelSize, FontPixelSize, typography, controlSize) \
    X(int, fontWeight, FontWeight, typography, controlWeight) \
    X(qreal, minimumHeight, MinimumHeight, metrics, controlHeight) \
    X(qreal, horizontalPadding, HorizontalPadding, metrics, horizontalPadding) \
    X(qreal, verticalPadding, VerticalPadding, metrics, verticalPadding) \
    X(qreal, spacing, Spacing, metrics, spacing)

#define IA_CONTROL_PROPERTIES(X) \
    IA_CONTROL_COLOR_PROPERTIES(X) \
    IA_CONTROL_NONCOLOR_PROPERTIES(X)
